#include "acquisition/tcpdatasource.h"

#include "protocol/scopelinkprotocol.h"

#include <QTcpSocket>

namespace {
constexpr int kSampleRateHz = 100;
constexpr int kChannelCount = 2;
constexpr int kFramesPerBlock = 10;
}

TcpDataSource::TcpDataSource(const QString &host, quint16 port, QObject *parent)
    : IDataSource(parent), m_host(host.trimmed()), m_port(port)
{
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &TcpDataSource::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &TcpDataSource::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &TcpDataSource::onReadyRead);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_socket, &QTcpSocket::errorOccurred, this, &TcpDataSource::onSocketError);
#else
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &TcpDataSource::onSocketError);
#endif
}

TcpDataSource::~TcpDataSource() { stop(); }

void TcpDataSource::start()
{
    if (m_running || m_socket->state() != QAbstractSocket::UnconnectedState) return;
    if (m_host.isEmpty() || m_port == 0) {
        emit errorOccurred(QStringLiteral("Wi-Fi 地址或端口无效"));
        return;
    }
    m_receiveBuffer.clear(); m_pendingSamples.clear(); m_pendingPings.clear();
    m_nextSampleIndex = 0; m_receivedBytes = 0; m_transmittedBytes = 0;
    m_receivedSamples = 0; m_clock.restart();
    m_socket->connectToHost(m_host, m_port);
    emit linkStateChanged(false, QStringLiteral("%1:%2").arg(m_host).arg(m_port),
                          QStringLiteral("正在连接 Wi-Fi TCP 服务…"));
}

void TcpDataSource::stop()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        if (m_running) writeCommand(QByteArrayLiteral("STREAM OFF"));
        m_socket->disconnectFromHost();
        if (m_socket->state() != QAbstractSocket::UnconnectedState)
            m_socket->abort();
    }
    const bool wasRunning = m_running; m_running = false;
    if (wasRunning) emit runningChanged(false);
}

void TcpDataSource::onConnected()
{
    m_running = true; m_clock.restart();
    emit streamInfoChanged(streamInfo()); emit runningChanged(true);
    emit linkStateChanged(true, QStringLiteral("%1:%2").arg(m_host).arg(m_port),
                          QStringLiteral("Wi-Fi TCP 已连接"));
    requestInfo();
    setStreaming(m_streamingRequested);
    setBoardFftEnabled(m_boardFftEnabled);
    requestPing();
}

void TcpDataSource::onDisconnected()
{
    const bool wasRunning = m_running; m_running = false;
    if (wasRunning) emit runningChanged(false);
    emit linkStateChanged(false, QStringLiteral("%1:%2").arg(m_host).arg(m_port),
                          QStringLiteral("Wi-Fi TCP 已断开"));
}

void TcpDataSource::onSocketError()
{
    emit errorOccurred(QStringLiteral("Wi-Fi TCP：%1").arg(m_socket->errorString()));
}

void TcpDataSource::onReadyRead() { processIncoming(m_socket->readAll()); }

void TcpDataSource::requestPing()
{
    if (!m_running) return;
    const quint32 sequence = m_nextPingSequence++;
    m_pendingPings.insert(sequence, m_clock.nsecsElapsed());
    writeCommand(QByteArrayLiteral("PING ") + QByteArray::number(sequence));
}

void TcpDataSource::requestInfo() { if (m_running) writeCommand(QByteArrayLiteral("INFO")); }

void TcpDataSource::setStreaming(bool enabled)
{
    m_streamingRequested = enabled;
    if (m_running) writeCommand(enabled ? QByteArrayLiteral("STREAM ON") : QByteArrayLiteral("STREAM OFF"));
}

void TcpDataSource::setBoardFftEnabled(bool enabled)
{
    m_boardFftEnabled = enabled;
    if (m_running) writeCommand(enabled ? QByteArrayLiteral("FFT ON") : QByteArrayLiteral("FFT OFF"));
}

DataStreamInfo TcpDataSource::streamInfo() const
{
    DataStreamInfo info;
    info.sourceName = QStringLiteral("STM32F103VET6 / ESP8266 Wi-Fi / %1:%2").arg(m_host).arg(m_port);
    info.channelCount = kChannelCount; info.sampleRateHz = kSampleRateHz;
    info.channelNames = QStringList() << QStringLiteral("DAC 输出估算（V）")
                                      << QStringLiteral("ADC 外部输入（V）");
    info.unit = QStringLiteral("V"); return info;
}

void TcpDataSource::writeCommand(const QByteArray &command)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) return;
    const QByteArray framed = command + QByteArrayLiteral("\r\n");
    m_socket->write(framed); m_socket->flush(); m_transmittedBytes += quint64(framed.size());
    emit protocolLineReceived(QString::fromLatin1(command), true);
    emit statisticsChanged(m_receivedBytes, m_transmittedBytes, m_receivedSamples);
}

void TcpDataSource::processIncoming(const QByteArray &bytes)
{
    m_receivedBytes += quint64(bytes.size()); m_receiveBuffer.append(bytes);
    if (m_receiveBuffer.size() > 65536) m_receiveBuffer = m_receiveBuffer.right(8192);
    int newline = -1;
    while ((newline = m_receiveBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_receiveBuffer.left(newline).trimmed();
        m_receiveBuffer.remove(0, newline + 1);
        if (!line.isEmpty()) processLine(line);
    }
    emit statisticsChanged(m_receivedBytes, m_transmittedBytes, m_receivedSamples);
}

void TcpDataSource::processLine(const QByteArray &line)
{
    const ScopeLinkMessage message = ScopeLinkProtocol::parseLine(line);
    if (message.type != ScopeLinkMessage::Type::Sample || !message.valid || (message.sequence % 25U) == 0U)
        emit protocolLineReceived(QString::fromUtf8(line), false);
    if (!message.valid) return;
    if (message.type == ScopeLinkMessage::Type::Pong) {
        const auto found = m_pendingPings.constFind(message.sequence);
        if (found != m_pendingPings.constEnd()) {
            const double ms = double(m_clock.nsecsElapsed() - found.value()) / 1000000.0;
            m_pendingPings.remove(message.sequence);
            emit pingResult(message.sequence, ms, message.deviceMilliseconds);
        }
        return;
    }
    if (message.type != ScopeLinkMessage::Type::Sample) return;
    m_pendingSamples += message.channels; ++m_receivedSamples; emitSampleBlockIfReady();
}

void TcpDataSource::emitSampleBlockIfReady()
{
    const int requiredValues = kFramesPerBlock * kChannelCount;
    while (m_pendingSamples.size() >= requiredValues) {
        auto block = QSharedPointer<DataBlock>::create();
        block->firstSampleIndex = m_nextSampleIndex; block->monotonicTimestampNs = m_clock.nsecsElapsed();
        block->channelCount = kChannelCount; block->frameCount = kFramesPerBlock;
        block->sampleRateHz = kSampleRateHz; block->interleaved = m_pendingSamples.mid(0, requiredValues);
        m_pendingSamples.remove(0, requiredValues); m_nextSampleIndex += kFramesPerBlock; emit blockReady(block);
    }
}
