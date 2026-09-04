#include "acquisition/serialdatasource.h"

#include "protocol/scopelinkprotocol.h"

#ifdef ZYNQ_SCOPE_HAS_CH347
#  include "ch347_spi_host.h"
#  include "scope_spi_client.h"
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <array>
#include <chrono>
#include <vector>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace {
constexpr int kBaudRate = 115200;
constexpr int kSampleRateHz = 250;
constexpr int kChannelCount = 2;
constexpr int kFramesPerBlock = 2;

QString defaultCh347DllPath()
{
    const QString besideExecutable = QCoreApplication::applicationDirPath()
        + QStringLiteral("/CH347DLLA64.DLL");
    if (QFileInfo::exists(besideExecutable)) {
        return QDir::toNativeSeparators(besideExecutable);
    }
    return QStringLiteral("C:\\WCH.CN\\CH341PAR\\WIN 1X\\CH347DLLA64.DLL");
}

#ifdef ZYNQ_SCOPE_HAS_CH347
quint32 readLe32(const std::vector<std::uint8_t> &bytes, std::size_t offset)
{
    if (offset + 4U > bytes.size()) {
        return 0;
    }
    return static_cast<quint32>(bytes[offset])
        | (static_cast<quint32>(bytes[offset + 1U]) << 8)
        | (static_cast<quint32>(bytes[offset + 2U]) << 16)
        | (static_cast<quint32>(bytes[offset + 3U]) << 24);
}
#endif

#ifdef Q_OS_WIN
QString windowsErrorMessage(DWORD code)
{
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    const QString result = length > 0
        ? QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed()
        : QStringLiteral("Windows error %1").arg(code);
    if (buffer) {
        LocalFree(buffer);
    }
    return result;
}

HANDLE nativeHandle(void *handle)
{
    return reinterpret_cast<HANDLE>(handle);
}
#endif
}

SerialDataSource::SerialDataSource(const QString &portName, QObject *parent)
    : IDataSource(parent)
    , m_portName(portName.trimmed())
    , m_ch347DllPath(defaultCh347DllPath())
{
    m_uploadTimer = new QTimer(this);
    m_uploadTimer->setSingleShot(true);
    m_uploadTimer->setInterval(500);
    connect(m_uploadTimer, &QTimer::timeout,
            this, &SerialDataSource::handleUploadTimeout);
}

SerialDataSource::~SerialDataSource()
{
    closeTransport();
}

QString SerialDataSource::portName() const
{
    return m_portName;
}

QStringList SerialDataSource::availablePortNames()
{
    QStringList ports;
#ifdef Q_OS_WIN
    wchar_t target[512] = {};
    for (int index = 1; index <= 256; ++index) {
        const QString candidate = QStringLiteral("COM%1").arg(index);
        if (QueryDosDeviceW(reinterpret_cast<LPCWSTR>(candidate.utf16()),
                            target, static_cast<DWORD>(std::size(target))) > 0) {
            ports.append(candidate);
        }
    }
#endif
    return ports;
}

void SerialDataSource::setPortName(const QString &portName)
{
    const QString requested = portName.trimmed();
    if (m_running) {
        emit errorOccurred(QStringLiteral("串口已连接，请先断开再切换端口"));
        return;
    }
    m_portName = requested;
}

void SerialDataSource::setTransportMode(int mode)
{
    if (m_running) {
        emit errorOccurred(QStringLiteral("请先停止当前通信，再切换串口/WiFi模式"));
        return;
    }
    if (mode == static_cast<int>(TransportMode::TcpServer)) {
        m_transportMode = TransportMode::TcpServer;
    } else if (mode == static_cast<int>(TransportMode::Ch347Spi)) {
        m_transportMode = TransportMode::Ch347Spi;
    } else {
        m_transportMode = TransportMode::Serial;
    }
}

void SerialDataSource::setTcpListenAddress(const QString &address)
{
    if (m_running) {
        emit errorOccurred(QStringLiteral("请先停止监听，再修改监听地址"));
        return;
    }
    m_tcpListenAddress = address.trimmed().isEmpty()
        ? QStringLiteral("0.0.0.0") : address.trimmed();
}

void SerialDataSource::setTcpListenPort(int port)
{
    if (m_running) {
        emit errorOccurred(QStringLiteral("请先停止监听，再修改端口"));
        return;
    }
    if (port > 0 && port <= 65535) {
        m_tcpListenPort = static_cast<quint16>(port);
    }
}

void SerialDataSource::setCh347DllPath(const QString &path)
{
    if (m_running) {
        emit errorOccurred(QStringLiteral("请先断开 CH347，再修改 DLL 路径"));
        return;
    }
    const QString requested = QDir::toNativeSeparators(path.trimmed());
    if (!requested.isEmpty()) {
        m_ch347DllPath = requested;
    }
}

void SerialDataSource::setCh347DeviceIndex(int index)
{
    if (!m_running) {
        m_ch347DeviceIndex = qBound(0, index, 15);
    }
}

void SerialDataSource::setCh347ClockIndex(int index)
{
    if (!m_running) {
        m_ch347ClockIndex = qBound(0, index, 7);
    }
}

void SerialDataSource::setHighSpeedMode(bool enabled)
{
    if (m_highSpeedMode == enabled) {
        return;
    }
    m_highSpeedMode = enabled;
    if (!m_running) {
        return;
    }
#ifdef ZYNQ_SCOPE_HAS_CH347
    if (m_transportMode == TransportMode::Ch347Spi && m_protocolReady) {
        std::string error;
        if (enabled) {
            emit protocolLineReceived(QStringLiteral("HS START (SPI 0x30)"), true);
            if (!m_ch347Client->hsStart(&error)) {
                logCh347Error(QStringLiteral("HS_START"), error);
                m_highSpeedMode = false;
                return;
            }
            m_pendingSamples.clear();
            m_nextSampleIndex = 0;
            /* A 2048-byte chunk is produced every ~2.55 ms at 384 kS/s;
             * the 10 ms normal-mode poll would only pull ~100 chunks/s and
             * let the double buffer overflow. Poll as fast as the USB
             * WriteRead allows (~2.3 ms per chunk); pollCh347HighSpeed adds
             * a 250us inter-pull spin so pulls never go back-to-back. */
            if (m_pollTimer) {
                m_pollTimer->start(3);
            }
            emit streamInfoChanged(streamInfo());
        } else {
            emit protocolLineReceived(QStringLiteral("HS STOP (SPI 0x31)"), true);
            m_ch347Client->hsStop(&error);
            if (m_pollTimer) {
                m_pollTimer->start(10);
            }
            emit streamInfoChanged(streamInfo());
        }
        return;
    }
#endif
    emit errorOccurred(QStringLiteral("高速模式仅支持 CH347 SPI 传输"));
}

void SerialDataSource::start()
{
    if (m_running) {
        return;
    }

    if (m_transportMode == TransportMode::Serial && m_portName.isEmpty()) {
        const QStringList ports = availablePortNames();
        if (ports.contains(QStringLiteral("COM5"), Qt::CaseInsensitive)) {
            m_portName = QStringLiteral("COM5");
        } else if (!ports.isEmpty()) {
            m_portName = ports.first();
        }
    }

    QString errorMessage;
    if (!openTransport(&errorMessage)) {
        const QString message = QStringLiteral("无法启动 %1：%2")
                                    .arg(endpointName(), errorMessage);
        emit errorOccurred(message);
        emit linkStateChanged(false, endpointName(), message);
        emit runningChanged(false);
        return;
    }

    if (!m_pollTimer) {
        m_pollTimer = new QTimer(this);
        m_pollTimer->setTimerType(Qt::PreciseTimer);
        connect(m_pollTimer, &QTimer::timeout,
                this, &SerialDataSource::pollSerialPort);
    }

    m_running = true;
    m_protocolReady = false;
    emit streamInfoChanged(streamInfo());
    emit runningChanged(true);
    if (m_transportMode == TransportMode::Ch347Spi) {
#ifdef ZYNQ_SCOPE_HAS_CH347
        initializeCh347Session();
        m_pollTimer->start(10);
#endif
    } else if (m_transportMode == TransportMode::Serial) {
        m_pollTimer->start(5);
        emit linkStateChanged(true, m_portName,
                              QStringLiteral("%1 已打开，等待板端 MAIN READY")
                                  .arg(m_portName));
    } else {
        emit linkStateChanged(true, endpointName(),
                              QStringLiteral("WiFi TCP 正在监听 %1，等待 ESP8266 连接")
                                  .arg(endpointName()));
    }
}

void SerialDataSource::stop()
{
    if (!m_running && !m_handle && !m_tcpServer && !m_tcpSocket) {
        return;
    }

    if (m_protocolReady && transportReady()) {
#ifdef ZYNQ_SCOPE_HAS_CH347
        if (m_transportMode == TransportMode::Ch347Spi && m_ch347Client) {
            std::string ignored;
            if (m_highSpeedMode) {
                m_ch347Client->hsStop(&ignored);
            } else {
                m_ch347Client->setStreaming(false, &ignored);
            }
        } else
#endif
        {
            writeCommand(QByteArrayLiteral("STREAM OFF"));
        }
    }
    if (m_uploadStage != UploadStage::Idle) {
        finishUpload(false, QStringLiteral("通信断开，LUT 上传已中止"), true);
    }
    if (m_pollTimer) {
        m_pollTimer->stop();
    }
    closeTransport();
    const bool wasRunning = m_running;
    m_running = false;
    m_protocolReady = false;
    if (wasRunning) {
        emit runningChanged(false);
    }
    if (m_transportMode == TransportMode::Ch347Spi) {
        emit linkStateChanged(false, endpointName(),
                              QStringLiteral("CH347 SPI 已断开"));
        return;
    }
    emit linkStateChanged(false, endpointName(),
                          m_transportMode == TransportMode::TcpServer
                              ? QStringLiteral("WiFi TCP 监听已停止")
                              : QStringLiteral("串口已断开"));
}

void SerialDataSource::requestPing()
{
    if (!m_protocolReady || !transportReady()) {
        if (m_running) {
            return;
        }
        emit errorOccurred(QStringLiteral("尚未连接开发板"));
        return;
    }
#ifdef ZYNQ_SCOPE_HAS_CH347
    if (m_transportMode == TransportMode::Ch347Spi && m_ch347Host) {
        const quint32 sequence = m_nextPingSequence++;
        const std::vector<std::uint8_t> request = {
            static_cast<std::uint8_t>(sequence & 0xFFU),
            static_cast<std::uint8_t>((sequence >> 8) & 0xFFU),
            static_cast<std::uint8_t>((sequence >> 16) & 0xFFU),
            static_cast<std::uint8_t>((sequence >> 24) & 0xFFU)
        };
        Ch347SpiHost::Response response;
        std::string error;
        QElapsedTimer roundTrip;
        roundTrip.start();
        emit protocolLineReceived(QStringLiteral("PING %1 (SPI 0x01)").arg(sequence), true);
        if (!m_ch347Host->transact(CH347_SPI_COMMAND_PING, request,
                                   &response, &error)
            || response.status != CH347_SPI_STATUS_OK
            || response.payload != request) {
            logCh347Error(QStringLiteral("PING"), error.empty()
                              ? std::string("invalid PING response") : error);
            return;
        }
        m_transmittedBytes += CH347_SPI_FRAME_SIZE * 2U;
        m_receivedBytes += CH347_SPI_FRAME_SIZE * 2U;
        emit protocolLineReceived(QStringLiteral("PONG %1").arg(sequence), false);
        emit pingResult(sequence, roundTrip.nsecsElapsed() / 1000000.0,
                        m_ch347DeviceMilliseconds);
        emit statisticsChanged(m_receivedBytes, m_transmittedBytes,
                               m_receivedSamples);
        return;
    }
#endif
    const qint64 now = m_clock.nsecsElapsed();
    constexpr qint64 kPingTimeoutNs = 3000000000LL;
    for (auto it = m_pendingPings.begin(); it != m_pendingPings.end();) {
        if (now - it.value() >= kPingTimeoutNs) {
            it = m_pendingPings.erase(it);
        } else {
            ++it;
        }
    }
    /* Do not build an unbounded queue while the board is not replying. */
    if (!m_pendingPings.isEmpty()) {
        return;
    }
    const quint32 sequence = m_nextPingSequence++;
    m_pendingPings.insert(sequence, now);
    writeCommand(QByteArrayLiteral("PING ") + QByteArray::number(sequence));
}

void SerialDataSource::requestInfo()
{
#ifdef ZYNQ_SCOPE_HAS_CH347
    if (m_transportMode == TransportMode::Ch347Spi
        && m_protocolReady && m_ch347Host) {
        Ch347SpiHost::Response response;
        std::string error;
        emit protocolLineReceived(QStringLiteral("GET_INFO (SPI 0x02)"), true);
        if (!m_ch347Host->transact(CH347_SPI_COMMAND_GET_INFO, {},
                                   &response, &error)
            || response.status != CH347_SPI_STATUS_OK
            || response.payload.size() < 32U) {
            logCh347Error(QStringLiteral("GET_INFO"), error.empty()
                              ? std::string("invalid GET_INFO response") : error);
            return;
        }
        const QString name = QString::fromUtf8(
            reinterpret_cast<const char *>(response.payload.data() + 32U),
            static_cast<int>(response.payload.size() - 32U));
        emit protocolLineReceived(
            QStringLiteral("INFO name=%1 device=0x%2 valid=%3 invalid=%4 duplicate=%5 short=%6 transport=%7")
                .arg(name)
                .arg(readLe32(response.payload, 28U), 8, 16, QLatin1Char('0'))
                .arg(readLe32(response.payload, 8U))
                .arg(readLe32(response.payload, 12U))
                .arg(readLe32(response.payload, 16U))
                .arg(readLe32(response.payload, 20U))
                .arg(readLe32(response.payload, 24U)), false);
        return;
    }
#endif
    if (m_protocolReady && transportReady()) {
        writeCommand(QByteArrayLiteral("INFO"));
    }
}

void SerialDataSource::setStreaming(bool enabled)
{
    m_streamingRequested = enabled;
#ifdef ZYNQ_SCOPE_HAS_CH347
    if (m_transportMode == TransportMode::Ch347Spi
        && m_protocolReady && m_ch347Client) {
        std::string error;
        emit protocolLineReceived(
            enabled ? QStringLiteral("STREAM ON (SPI 0x21)")
                    : QStringLiteral("STREAM OFF (SPI 0x21)"), true);
        if (!m_ch347Client->setStreaming(enabled, &error)) {
            logCh347Error(QStringLiteral("STREAM"), error);
        }
        return;
    }
#endif
    if (m_protocolReady && transportReady()) {
        writeCommand(enabled ? QByteArrayLiteral("STREAM ON")
                             : QByteArrayLiteral("STREAM OFF"));
    }
}

void SerialDataSource::configureGenerator(const QString &waveform,
                                          int frequencyMilliHz,
                                          int amplitudeCode,
                                          int offsetCode)
{
    const QString requested = waveform.trimmed().toUpper();
    static const QStringList supported = {
        QStringLiteral("DC"),
        QStringLiteral("TRI"),
        QStringLiteral("SIN"),
        QStringLiteral("SQUARE"),
        QStringLiteral("SAW")
    };
    if (!supported.contains(requested)) {
        emit errorOccurred(QStringLiteral("不支持的 DAC 波形：%1").arg(waveform));
        return;
    }

    m_generatorWaveform = requested;
    m_generatorFrequencyMilliHz = qBound(100, frequencyMilliHz, 2000000);
    m_generatorAmplitudeCode = qBound(0, amplitudeCode, 127);
    m_generatorOffsetCode = qBound(0, offsetCode, 255);

    if (!m_protocolReady || !transportReady()) {
        return;
    }

#ifdef ZYNQ_SCOPE_HAS_CH347
    if (m_transportMode == TransportMode::Ch347Spi && m_ch347Client) {
        const std::uint8_t waveformCode = requested == QStringLiteral("DC") ? 0U
            : (requested == QStringLiteral("TRI") ? 1U
            : (requested == QStringLiteral("SQUARE") ? 2U
            : (requested == QStringLiteral("SAW") ? 3U : 4U)));
        std::string error;
        emit protocolLineReceived(
            QStringLiteral("GEN %1 %2 %3 %4 (SPI 0x23)")
                .arg(requested).arg(m_generatorFrequencyMilliHz)
                .arg(m_generatorAmplitudeCode).arg(m_generatorOffsetCode), true);
        if (!m_ch347Client->setGenerator(
                waveformCode,
                static_cast<std::uint32_t>(m_generatorFrequencyMilliHz),
                static_cast<std::uint8_t>(m_generatorAmplitudeCode),
                static_cast<std::uint8_t>(m_generatorOffsetCode), &error)) {
            logCh347Error(QStringLiteral("SET_GENERATOR"), error);
        }
        return;
    }
#endif
    writeCommand(QByteArrayLiteral("GEN ")
                 + m_generatorWaveform.toLatin1() + ' '
                 + QByteArray::number(m_generatorFrequencyMilliHz) + ' '
                 + QByteArray::number(m_generatorAmplitudeCode) + ' '
                 + QByteArray::number(m_generatorOffsetCode));
}

void SerialDataSource::setBoardFftEnabled(bool enabled)
{
    m_boardFftEnabled = enabled;
#ifdef ZYNQ_SCOPE_HAS_CH347
    if (m_transportMode == TransportMode::Ch347Spi
        && m_protocolReady && m_ch347Client) {
        std::string error;
        emit protocolLineReceived(
            enabled ? QStringLiteral("FFT ON (SPI 0x25)")
                    : QStringLiteral("FFT OFF (SPI 0x25)"), true);
        if (!m_ch347Client->setFftEnabled(enabled, &error)) {
            logCh347Error(QStringLiteral("FFT_CONTROL"), error);
        }
        return;
    }
#endif
    if (m_protocolReady && transportReady()) {
        writeCommand(enabled ? QByteArrayLiteral("FFT ON")
                             : QByteArrayLiteral("FFT OFF"));
    }
}

void SerialDataSource::uploadWaveform(const QByteArray &codes,
                                      int frequencyMilliHz,
                                      quint16 crc16)
{
    if (!m_protocolReady || !transportReady()) {
        emit waveformUploadFinished(false, QStringLiteral("开发板尚未连接"));
        return;
    }
    if (m_uploadStage != UploadStage::Idle) {
        emit waveformUploadFinished(false, QStringLiteral("已有 LUT 正在上传"));
        return;
    }
    if (codes.size() != 256 || frequencyMilliHz < 100 || frequencyMilliHz > 2000000) {
        emit waveformUploadFinished(false, QStringLiteral("LUT 长度或基波频率无效"));
        return;
    }
#ifdef ZYNQ_SCOPE_HAS_CH347
    if (m_transportMode == TransportMode::Ch347Spi && m_ch347Client) {
        const std::vector<std::uint8_t> waveform(codes.cbegin(), codes.cend());
        std::string error;
        emit protocolLineReceived(QStringLiteral("WAVE UPLOAD 256 bytes (SPI 0x27..0x29)"), true);
        emit waveformUploadProgress(0, codes.size());
        const bool ok = m_ch347Client->uploadWaveform(
            waveform, static_cast<std::uint32_t>(frequencyMilliHz), &error);
        if (ok) {
            emit waveformUploadProgress(codes.size(), codes.size());
            emit waveformUploadFinished(true, QStringLiteral("CH347 SPI LUT 上传完成"));
        } else {
            logCh347Error(QStringLiteral("WAVE_UPLOAD"), error);
            emit waveformUploadFinished(false, QString::fromStdString(error));
        }
        return;
    }
#endif
    m_uploadCodes = codes;
    m_uploadOffset = 0;
    m_uploadFrequencyMilliHz = frequencyMilliHz;
    m_uploadCrc16 = crc16;
    m_uploadStage = UploadStage::Begin;
    sendNextUploadCommand();
}

void SerialDataSource::handleUploadTimeout()
{
    finishUpload(false, QStringLiteral("LUT 分包应答超时（500 ms）"), true);
}

void SerialDataSource::pollSerialPort()
{
#ifdef ZYNQ_SCOPE_HAS_CH347
    if (m_transportMode == TransportMode::Ch347Spi) {
        pollCh347();
        return;
    }
#endif
#ifdef Q_OS_WIN
    if (!m_running || !m_handle) {
        return;
    }

    DWORD communicationErrors = 0;
    COMSTAT status = {};
    if (!ClearCommError(nativeHandle(m_handle), &communicationErrors, &status)) {
        const QString message = windowsErrorMessage(GetLastError());
        emit errorOccurred(QStringLiteral("读取 %1 失败：%2").arg(m_portName, message));
        stop();
        return;
    }
    if (status.cbInQue == 0) {
        return;
    }

    const DWORD wanted = std::min<DWORD>(status.cbInQue, 4096);
    QByteArray bytes(static_cast<int>(wanted), Qt::Uninitialized);
    DWORD received = 0;
    if (!ReadFile(nativeHandle(m_handle), bytes.data(), wanted, &received, nullptr)) {
        const QString message = windowsErrorMessage(GetLastError());
        emit errorOccurred(QStringLiteral("读取 %1 失败：%2").arg(m_portName, message));
        stop();
        return;
    }
    if (received > 0) {
        bytes.resize(static_cast<int>(received));
        processIncoming(bytes);
    }
#endif
}

void SerialDataSource::handleTcpNewConnection()
{
    while (m_tcpServer && m_tcpServer->hasPendingConnections()) {
        QTcpSocket *candidate = m_tcpServer->nextPendingConnection();
        if (!candidate) {
            continue;
        }
        if (m_tcpSocket) {
            candidate->disconnectFromHost();
            candidate->deleteLater();
            continue;
        }
        m_tcpSocket = candidate;
        connect(m_tcpSocket, &QTcpSocket::readyRead,
                this, &SerialDataSource::handleTcpReadyRead);
        connect(m_tcpSocket, &QTcpSocket::disconnected,
                this, &SerialDataSource::handleTcpDisconnected);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        connect(m_tcpSocket, &QTcpSocket::errorOccurred,
                this, &SerialDataSource::handleTcpError);
#else
        connect(m_tcpSocket,
                QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
                this, &SerialDataSource::handleTcpError);
#endif
        const QString peer = QStringLiteral("%1:%2")
                                 .arg(m_tcpSocket->peerAddress().toString())
                                 .arg(m_tcpSocket->peerPort());
        m_protocolReady = false;
        emit linkStateChanged(true, endpointName(),
                              QStringLiteral("ESP8266 已连接：%1，等待 WIFI READY")
                                  .arg(peer));
    }
}

void SerialDataSource::handleTcpReadyRead()
{
    if (m_tcpSocket) {
        processIncoming(m_tcpSocket->readAll());
    }
}

void SerialDataSource::handleTcpDisconnected()
{
    if (m_tcpSocket) {
        m_tcpSocket->deleteLater();
        m_tcpSocket = nullptr;
    }
    m_pendingPings.clear();
    m_protocolReady = false;
    if (m_running) {
        emit linkStateChanged(true, endpointName(),
                              QStringLiteral("ESP8266 已断开，继续监听 %1")
                                  .arg(endpointName()));
    }
}

void SerialDataSource::handleTcpError()
{
    if (m_tcpSocket) {
        emit errorOccurred(QStringLiteral("WiFi TCP：%1")
                               .arg(m_tcpSocket->errorString()));
    }
}

bool SerialDataSource::transportReady() const
{
    if (!m_running) {
        return false;
    }
    if (m_transportMode == TransportMode::TcpServer) {
        return m_tcpSocket
            && m_tcpSocket->state() == QAbstractSocket::ConnectedState;
    }
#ifdef ZYNQ_SCOPE_HAS_CH347
    if (m_transportMode == TransportMode::Ch347Spi) {
        return m_ch347Host && m_ch347Host->isOpen();
    }
#endif
    return m_handle != nullptr;
}

QString SerialDataSource::endpointName() const
{
    if (m_transportMode == TransportMode::Ch347Spi) {
        return QStringLiteral("CH347[%1] / CS1 / clock %2")
            .arg(m_ch347DeviceIndex).arg(m_ch347ClockIndex);
    }
    return m_transportMode == TransportMode::TcpServer
        ? QStringLiteral("%1:%2").arg(m_tcpListenAddress).arg(m_tcpListenPort)
        : m_portName;
}

void SerialDataSource::initializeSession()
{
    m_protocolReady = true;
    m_receiveBuffer.clear();
    m_pendingSamples.clear();
    m_pendingPings.clear();
    m_nextSampleIndex = 0;
    m_receivedBytes = 0;
    m_transmittedBytes = 0;
    m_receivedSamples = 0;
    m_clock.restart();
    emit streamInfoChanged(streamInfo());
    requestInfo();
    writeCommand(m_streamingRequested ? QByteArrayLiteral("STREAM ON")
                                      : QByteArrayLiteral("STREAM OFF"));
    configureGenerator(m_generatorWaveform,
                       m_generatorFrequencyMilliHz,
                       m_generatorAmplitudeCode,
                       m_generatorOffsetCode);
    setBoardFftEnabled(m_boardFftEnabled);
    requestPing();
}

DataStreamInfo SerialDataSource::streamInfo() const
{
    DataStreamInfo info;
    info.sourceName = m_transportMode == TransportMode::Ch347Spi
        ? QStringLiteral("STM32F103VET6 / CH347 USB-SPI / device %1")
              .arg(m_ch347DeviceIndex)
        : (m_transportMode == TransportMode::TcpServer
        ? QStringLiteral("STM32F103VET6 / ESP8266 WiFi TCP / %1")
              .arg(endpointName())
        : QStringLiteral("STM32F103VET6 / USART1 / %1").arg(m_portName));

    // The CH347 high-speed stream is intentionally not part of the Zynq
    // build. Keep the compatibility source compilable when that optional
    // backend is absent.
#ifdef ZYNQ_SCOPE_HAS_CH347
    if (m_highSpeedMode) {
        info.channelCount = 1;
        info.sampleRateHz = m_ch347HsSampleRateHz;
        info.channelNames = {QStringLiteral("AD9280 高速输入（V）")};
        info.unit = QStringLiteral("V");
        return info;
    }
#endif

    info.channelCount = kChannelCount;
    info.sampleRateHz = m_transportMode == TransportMode::Ch347Spi
#ifdef ZYNQ_SCOPE_HAS_CH347
        ? m_ch347SampleRateHz
#else
        ? kSampleRateHz
#endif
        : kSampleRateHz;
        info.channelNames = QStringList() << QStringLiteral("DAC 输出估算（V）")
                                           << QStringLiteral("ADC 外部输入（V）");
    info.unit = QStringLiteral("V");
    return info;
}

bool SerialDataSource::openTransport(QString *errorMessage)
{
#ifdef ZYNQ_SCOPE_HAS_CH347
    if (m_transportMode == TransportMode::Ch347Spi) {
        return openCh347(errorMessage);
    }
#endif
    if (m_transportMode == TransportMode::TcpServer) {
        QHostAddress address;
        if (m_tcpListenAddress == QStringLiteral("0.0.0.0")
            || m_tcpListenAddress.compare(QStringLiteral("Any"),
                                          Qt::CaseInsensitive) == 0) {
            address = QHostAddress::AnyIPv4;
        } else if (!address.setAddress(m_tcpListenAddress)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("无效的 IPv4 监听地址");
            }
            return false;
        }
        m_tcpServer = new QTcpServer(this);
        connect(m_tcpServer, &QTcpServer::newConnection,
                this, &SerialDataSource::handleTcpNewConnection);
        if (!m_tcpServer->listen(address, m_tcpListenPort)) {
            if (errorMessage) {
                *errorMessage = m_tcpServer->errorString();
            }
            m_tcpServer->deleteLater();
            m_tcpServer = nullptr;
            return false;
        }
        return true;
    }
#ifdef Q_OS_WIN
    if (m_portName.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未发现可用 COM 端口");
        }
        return false;
    }

    const QString path = QStringLiteral("\\\\.\\%1").arg(m_portName);
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),
                                GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (errorMessage) {
            *errorMessage = windowsErrorMessage(GetLastError());
        }
        return false;
    }

    DCB state = {};
    state.DCBlength = sizeof(state);
    if (!GetCommState(handle, &state)) {
        if (errorMessage) {
            *errorMessage = windowsErrorMessage(GetLastError());
        }
        CloseHandle(handle);
        return false;
    }
    state.BaudRate = kBaudRate;
    state.ByteSize = 8;
    state.Parity = NOPARITY;
    state.StopBits = ONESTOPBIT;
    state.fBinary = TRUE;
    state.fParity = FALSE;
    state.fOutxCtsFlow = FALSE;
    state.fOutxDsrFlow = FALSE;
    state.fDsrSensitivity = FALSE;
    state.fOutX = FALSE;
    state.fInX = FALSE;
    state.fDtrControl = DTR_CONTROL_DISABLE;
    state.fRtsControl = RTS_CONTROL_DISABLE;
    if (!SetCommState(handle, &state)) {
        if (errorMessage) {
            *errorMessage = windowsErrorMessage(GetLastError());
        }
        CloseHandle(handle);
        return false;
    }

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 100;
    SetCommTimeouts(handle, &timeouts);
    SetupComm(handle, 8192, 8192);
    PurgeComm(handle, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);
    EscapeCommFunction(handle, CLRDTR);
    EscapeCommFunction(handle, CLRRTS);
    m_handle = reinterpret_cast<void *>(handle);
    return true;
#else
    if (errorMessage) {
        *errorMessage = QStringLiteral("此连通性实验当前仅实现 Windows COM 传输");
    }
    return false;
#endif
}

#ifdef ZYNQ_SCOPE_HAS_CH347
bool SerialDataSource::openCh347(QString *errorMessage)
{
    if (!QFileInfo::exists(m_ch347DllPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未找到 64 位 CH347 DLL：%1")
                                .arg(m_ch347DllPath);
        }
        return false;
    }

    Ch347SpiHost::Options options;
    options.device_index = static_cast<std::uint32_t>(m_ch347DeviceIndex);
    options.spi_clock = static_cast<std::uint8_t>(m_ch347ClockIndex);
    options.usb_timeout_ms = 500U;
    options.poll_delay_ms = 1U;
    options.poll_attempts = 12U;

    auto host = std::make_unique<Ch347SpiHost>(options);
    std::string error;
    if (!host->open(m_ch347DllPath.toStdWString(), &error)) {
        if (errorMessage) {
            *errorMessage = QString::fromStdString(error);
        }
        return false;
    }
    Ch347SpiHost::DeviceInfo adapter;
    if (host->getDeviceInfo(&adapter, &error)
        && adapter.chip_mode == 3U) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("CH347 当前是 Mode %1；该模式的此接口为 JTAG/I2C，不能使用 SPI")
                                .arg(adapter.chip_mode);
        }
        return false;
    }
    auto client = std::make_unique<ScopeSpiClient>(*host);
    ScopeSpiStatus status;
    if (!client->getStatus(&status, &error)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("CH347 已打开（Mode %1，%2），但 STM32 SPI 无响应：%3")
                                .arg(adapter.chip_mode)
                                .arg(QString::fromStdString(adapter.product))
                                .arg(QString::fromStdString(error));
        }
        return false;
    }

    m_ch347DeviceMilliseconds = status.milliseconds;
    m_ch347DroppedSamples = status.dropped_samples;
    m_ch347SampleRateHz = status.sample_rate_hz == 0U
        ? static_cast<quint16>(kSampleRateHz) : status.sample_rate_hz;
    m_ch347Host = std::move(host);
    m_ch347Client = std::move(client);
    return true;
}

void SerialDataSource::initializeCh347Session()
{
    if (!m_ch347Host || !m_ch347Client) {
        return;
    }
    m_protocolReady = true;
    m_pendingSamples.clear();
    m_pendingPings.clear();
    m_nextSampleIndex = 0;
    m_receivedBytes = 0;
    m_transmittedBytes = 0;
    m_receivedSamples = 0;
    m_ch347ConsecutiveErrors = 0;
    m_lastCh347StatusNs = 0;
    m_clock.restart();
    emit streamInfoChanged(streamInfo());
    emit linkStateChanged(
        true, endpointName(),
        QStringLiteral("CH347 SPI 已连接：device %1，CS1，Mode 0，%2")
            .arg(m_ch347DeviceIndex)
            .arg(m_ch347ClockIndex == 7 ? QStringLiteral("468.75 kHz")
                                        : QStringLiteral("clock index %1").arg(m_ch347ClockIndex)));
    requestInfo();
    setStreaming(m_streamingRequested);
    configureGenerator(m_generatorWaveform,
                       m_generatorFrequencyMilliHz,
                       m_generatorAmplitudeCode,
                       m_generatorOffsetCode);
    setBoardFftEnabled(m_boardFftEnabled);
    requestPing();
}

void SerialDataSource::pollCh347()
{
    if (!m_running || !m_protocolReady || !m_ch347Client) {
        return;
    }

    if (m_highSpeedMode) {
        pollCh347HighSpeed();
        return;
    }

    std::vector<ScopeSpiSample> samples;
    std::uint16_t remaining = 0U;
    std::uint32_t dropped = 0U;
    std::string error;
    if (!m_ch347Client->readSamples(14U, &samples, &remaining,
                                    &dropped, &error)) {
        ++m_ch347ConsecutiveErrors;
        if (m_ch347ConsecutiveErrors == 1
            || (m_ch347ConsecutiveErrors % 25) == 0) {
            logCh347Error(QStringLiteral("READ_SAMPLES"), error);
        }
        return;
    }

    m_ch347ConsecutiveErrors = 0;
    m_transmittedBytes += CH347_SPI_FRAME_SIZE * 2U;
    m_receivedBytes += CH347_SPI_FRAME_SIZE * 2U;
    m_ch347DroppedSamples = dropped;
    for (const ScopeSpiSample &sample : samples) {
        m_pendingSamples.append(static_cast<float>(sample.dac_millivolts) / 1000.0F);
        m_pendingSamples.append(static_cast<float>(sample.adc_millivolts) / 1000.0F);
        ++m_receivedSamples;
    }
    if (!samples.empty()) {
        emitSampleBlockIfReady();
    }

    const qint64 now = m_clock.nsecsElapsed();
    if (m_lastCh347StatusNs == 0
        || now - m_lastCh347StatusNs >= 1000000000LL) {
        ScopeSpiStatus status;
        if (m_ch347Client->getStatus(&status, &error)) {
            m_lastCh347StatusNs = now;
            m_ch347DeviceMilliseconds = status.milliseconds;
            m_ch347DroppedSamples = status.dropped_samples;
            if (status.sample_rate_hz != 0U
                && status.sample_rate_hz != m_ch347SampleRateHz) {
                m_ch347SampleRateHz = status.sample_rate_hz;
                emit streamInfoChanged(streamInfo());
            }
            if (m_boardFftEnabled && status.fft_result_available != 0U) {
                ScopeSpiFftResult fft;
                if (m_ch347Client->readFft(&fft, &error)) {
                    emit boardFftResult(
                        fft.sequence,
                        static_cast<float>(fft.peak_millihz) / 1000.0F,
                        static_cast<float>(fft.amplitude_millivolts) / 1000.0F,
                        fft.compute_microseconds);
                }
            }
        }
    }
    Q_UNUSED(remaining)
    emit statisticsChanged(m_receivedBytes, m_transmittedBytes,
                           m_receivedSamples);
}

#ifdef ZYNQ_SCOPE_HAS_CH347
void SerialDataSource::pollCh347HighSpeed()
{
    /*
     * Mirrors the CLI's 250us spin between pulls: the STM32 main loop needs
     * time to swap the SPI2 TX DMA source between chunks. Pulling
     * back-to-back races the rearm and truncates RX transactions, which
     * makes the CH347 driver retry and storms the slave NSS line.
     */
    {
        const auto spinUntil = std::chrono::steady_clock::now()
            + std::chrono::microseconds(250);
        while (std::chrono::steady_clock::now() < spinUntil) {
        }
    }
    std::array<std::uint8_t, CH347_SPI_STREAM_FRAME_SIZE> chunk;
    std::string error;
    if (!m_ch347Client->hsPull(chunk.data(),
                               static_cast<std::uint32_t>(chunk.size()),
                               &error)) {
        ++m_ch347ConsecutiveErrors;
        if (m_ch347ConsecutiveErrors == 1
            || (m_ch347ConsecutiveErrors % 25) == 0) {
            logCh347Error(QStringLiteral("HS_PULL"), error);
        }
        return;
    }
    m_ch347ConsecutiveErrors = 0;
    m_transmittedBytes += CH347_SPI_STREAM_FRAME_SIZE;
    m_receivedBytes += CH347_SPI_STREAM_FRAME_SIZE;

    if (chunk[0] != CH347_SPI_STREAM_MAGIC_0
        || chunk[1] != CH347_SPI_STREAM_MAGIC_1) {
        return;
    }
    const std::uint16_t count = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(chunk[4])
        | (static_cast<std::uint16_t>(chunk[5]) << 8));
    if (count == 0U || count > CH347_SPI_STREAM_MAX_SAMPLES) {
        return;
    }

    /*
     * High-speed stream: single channel, AD9280 8-bit raw code on
     * PE8..PE15 mapped to 0..2 V. Each 2048-byte chunk carries up to
     * CH347_SPI_STREAM_MAX_SAMPLES frames; forward the full chunk as one
     * DataBlock so the GUI paints the raw waveform at high rate.
     */
    auto block = QSharedPointer<DataBlock>::create();
    block->firstSampleIndex = m_nextSampleIndex;
    block->monotonicTimestampNs = m_clock.nsecsElapsed();
    block->channelCount = 1;
    block->frameCount = count;
    block->sampleRateHz = m_ch347HsSampleRateHz;
    block->interleaved.reserve(count);
    for (std::uint16_t i = 0U; i < count; ++i) {
        const float volts = static_cast<float>(chunk[CH347_SPI_STREAM_HEADER_SIZE + i])
            * 2.0F / 255.0F;
        block->interleaved.append(volts);
    }
    m_nextSampleIndex += count;
    m_receivedSamples += count;
    emit blockReady(block);
    emit statisticsChanged(m_receivedBytes, m_transmittedBytes,
                           m_receivedSamples);
}
#endif

void SerialDataSource::logCh347Error(const QString &operation,
                                     const std::string &error)
{
    const QString message = QStringLiteral("CH347 %1 失败：%2")
                                .arg(operation, QString::fromStdString(error));
    emit protocolLineReceived(message, false);
    emit errorOccurred(message);
}
#endif

void SerialDataSource::closeTransport()
{
#ifdef ZYNQ_SCOPE_HAS_CH347
    m_ch347Client.reset();
    m_ch347Host.reset();
#endif
    if (m_tcpSocket) {
        m_tcpSocket->disconnect(this);
        m_tcpSocket->abort();
        m_tcpSocket->deleteLater();
        m_tcpSocket = nullptr;
    }
    if (m_tcpServer) {
        m_tcpServer->close();
        m_tcpServer->deleteLater();
        m_tcpServer = nullptr;
    }
#ifdef Q_OS_WIN
    if (m_handle) {
        CloseHandle(nativeHandle(m_handle));
        m_handle = nullptr;
    }
#else
    m_handle = nullptr;
#endif
}

void SerialDataSource::writeCommand(const QByteArray &command)
{
    const QByteArray framed = command + QByteArrayLiteral("\r\n");
    if (m_transportMode == TransportMode::TcpServer) {
        if (!m_tcpSocket
            || m_tcpSocket->state() != QAbstractSocket::ConnectedState) {
            return;
        }
        const qint64 written = m_tcpSocket->write(framed);
        if (written < 0) {
            emit errorOccurred(QStringLiteral("WiFi TCP 写入失败：%1")
                                   .arg(m_tcpSocket->errorString()));
            return;
        }
        m_tcpSocket->flush();
        m_transmittedBytes += static_cast<quint64>(written);
        emit protocolLineReceived(QString::fromLatin1(command), true);
        emit statisticsChanged(m_receivedBytes, m_transmittedBytes,
                               m_receivedSamples);
        return;
    }
#ifdef Q_OS_WIN
    if (!m_handle) {
        return;
    }
    DWORD written = 0;
    if (!WriteFile(nativeHandle(m_handle), framed.constData(),
                   static_cast<DWORD>(framed.size()), &written, nullptr)) {
        emit errorOccurred(QStringLiteral("写入 %1 失败：%2")
                               .arg(m_portName, windowsErrorMessage(GetLastError())));
        return;
    }
    m_transmittedBytes += written;
    emit protocolLineReceived(QString::fromLatin1(command), true);
    emit statisticsChanged(m_receivedBytes, m_transmittedBytes, m_receivedSamples);
#else
    Q_UNUSED(command)
#endif
}

void SerialDataSource::processIncoming(const QByteArray &bytes)
{
    m_receivedBytes += static_cast<quint64>(bytes.size());
    m_receiveBuffer.append(bytes);
    if (m_receiveBuffer.size() > 65536) {
        m_receiveBuffer = m_receiveBuffer.right(8192);
        emit errorOccurred(QStringLiteral("串口接收缓存溢出，已丢弃旧数据"));
    }

    int newline = -1;
    while ((newline = m_receiveBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_receiveBuffer.left(newline).trimmed();
        m_receiveBuffer.remove(0, newline + 1);
        if (!line.isEmpty()) {
            processLine(line);
        }
    }
    emit statisticsChanged(m_receivedBytes, m_transmittedBytes, m_receivedSamples);
}

void SerialDataSource::processLine(const QByteArray &line)
{
    if (line == QByteArrayLiteral("MAIN READY WIFI")) {
        emit protocolLineReceived(QString::fromLatin1(line), false);
        emit linkStateChanged(true, endpointName(),
                              QStringLiteral("板端主循环已就绪；WiFi 固件请切换到 TCP 监听模式"));
        return;
    }

    if (line == QByteArrayLiteral("MAIN READY")
        || line == QByteArrayLiteral("WIFI READY")
        || line == QByteArrayLiteral("WIFI RE-LINK READY")) {
        emit protocolLineReceived(QString::fromLatin1(line), false);
        if (!m_protocolReady) {
            emit linkStateChanged(true, endpointName(),
                                  QStringLiteral("板端主循环已就绪：%1")
                                      .arg(QString::fromLatin1(line)));
            /* ESP may forward READY before firmware returns to the polling
             * loop. Delay the first command burst until USART3 RX is active. */
            QPointer<SerialDataSource> self(this);
            QTimer::singleShot(750, this, [self]() {
                if (!self || self->m_protocolReady || !self->transportReady()) {
                    return;
                }
                self->initializeSession();
            });
        }
        return;
    }

    const ScopeLinkMessage message = ScopeLinkProtocol::parseLine(line);

    /* Keep the protocol log readable while all 100 samples/s still reach the plot. */
    if (message.type != ScopeLinkMessage::Type::Sample
        || !message.valid || (message.sequence % 25U) == 0U) {
        emit protocolLineReceived(QString::fromUtf8(line), false);
    }

    if (!message.valid) {
        return;
    }
    if (m_uploadStage != UploadStage::Idle
        && message.type == ScopeLinkMessage::Type::Error) {
        finishUpload(false, QStringLiteral("开发板拒绝 LUT：%1").arg(message.raw), true);
        return;
    }
    if (m_uploadStage != UploadStage::Idle
        && message.type == ScopeLinkMessage::Type::Ack) {
        if (m_uploadStage == UploadStage::Begin
            && message.raw.startsWith(QStringLiteral("ACK WAVE BEGIN"))) {
            m_uploadStage = UploadStage::Data;
            sendNextUploadCommand();
            return;
        }
        if (m_uploadStage == UploadStage::Data
            && message.raw.startsWith(QStringLiteral("ACK WAVE DATA"))) {
            m_uploadOffset += 32;
            emit waveformUploadProgress(m_uploadOffset, m_uploadCodes.size());
            m_uploadStage = m_uploadOffset < m_uploadCodes.size()
                ? UploadStage::Data : UploadStage::Commit;
            sendNextUploadCommand();
            return;
        }
        if (m_uploadStage == UploadStage::Commit
            && message.raw.startsWith(QStringLiteral("ACK WAVE COMMIT"))) {
            finishUpload(true, QStringLiteral("自定义 LUT 已原子切换"), false);
            return;
        }
    }
    if (message.type == ScopeLinkMessage::Type::Pong) {
        const auto found = m_pendingPings.constFind(message.sequence);
        if (found != m_pendingPings.constEnd()) {
            const double roundTripMs = static_cast<double>(m_clock.nsecsElapsed() - found.value())
                / 1000000.0;
            m_pendingPings.remove(message.sequence);
            emit pingResult(message.sequence, roundTripMs,
                            message.deviceMilliseconds);
        }
        return;
    }
    if (message.type == ScopeLinkMessage::Type::Fft) {
        emit boardFftResult(message.sequence,
                            message.peakFrequencyHz,
                            message.peakAmplitudeVolts,
                            message.computeMicroseconds);
        return;
    }
    if (message.type != ScopeLinkMessage::Type::Sample) {
        return;
    }

    m_pendingSamples += message.channels;
    ++m_receivedSamples;
    emitSampleBlockIfReady();
}

void SerialDataSource::sendNextUploadCommand()
{
    if (m_uploadStage == UploadStage::Begin) {
        writeCommand(QByteArrayLiteral("WAVE BEGIN 256 ")
                     + QByteArray::number(m_uploadCrc16, 16).toUpper());
    } else if (m_uploadStage == UploadStage::Data) {
        const QByteArray chunk = m_uploadCodes.mid(m_uploadOffset, 32);
        writeCommand(QByteArrayLiteral("WAVE DATA ")
                     + QByteArray::number(m_uploadOffset) + ' '
                     + chunk.toHex().toUpper());
    } else if (m_uploadStage == UploadStage::Commit) {
        writeCommand(QByteArrayLiteral("WAVE COMMIT ")
                     + QByteArray::number(m_uploadFrequencyMilliHz));
    } else {
        return;
    }
    m_uploadTimer->start();
}

void SerialDataSource::finishUpload(bool success,
                                    const QString &message,
                                    bool sendAbort)
{
    if (m_uploadTimer) {
        m_uploadTimer->stop();
    }
    if (sendAbort && m_protocolReady && transportReady()
        && m_uploadStage != UploadStage::Idle) {
        writeCommand(QByteArrayLiteral("WAVE ABORT"));
    }
    m_uploadStage = UploadStage::Idle;
    m_uploadCodes.clear();
    m_uploadOffset = 0;
    emit waveformUploadFinished(success, message);
}

void SerialDataSource::emitSampleBlockIfReady()
{
    const int requiredValues = kFramesPerBlock * kChannelCount;
    while (m_pendingSamples.size() >= requiredValues) {
        auto block = QSharedPointer<DataBlock>::create();
        block->firstSampleIndex = m_nextSampleIndex;
        block->monotonicTimestampNs = m_clock.nsecsElapsed();
        block->channelCount = kChannelCount;
        block->frameCount = kFramesPerBlock;
        block->sampleRateHz = m_transportMode == TransportMode::Ch347Spi
#ifdef ZYNQ_SCOPE_HAS_CH347
            ? m_ch347SampleRateHz
#else
            ? kSampleRateHz
#endif
            : kSampleRateHz;
        block->interleaved = m_pendingSamples.mid(0, requiredValues);
        m_pendingSamples.remove(0, requiredValues);
        m_nextSampleIndex += kFramesPerBlock;
        emit blockReady(block);
    }
}
