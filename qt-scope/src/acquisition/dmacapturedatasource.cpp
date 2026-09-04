#include "acquisition/dmacapturedatasource.h"

#include <QtGlobal>

#include <algorithm>
#include <cerrno>
#include <cstring>

#ifdef Q_OS_LINUX
#  include <fcntl.h>
#  include <poll.h>
#  include <unistd.h>
#endif

DmaCaptureDataSource::DmaCaptureDataSource(QObject *parent)
    : IDataSource(parent)
{
    m_rawSegment.resize(m_segmentBytes);
}

DmaCaptureDataSource::~DmaCaptureDataSource()
{
    closeInput();
}

void DmaCaptureDataSource::setPath(const QString &path)
{
    if (m_running) {
        emit errorOccurred(QStringLiteral("请先停止采集，再修改采集路径"));
        return;
    }
    m_path = path.trimmed();
}

void DmaCaptureDataSource::setSampleRate(double sampleRateHz)
{
    if (m_running || sampleRateHz <= 0.0) {
        return;
    }
    m_sampleRateHz = sampleRateHz;
}

void DmaCaptureDataSource::setSegmentBytes(int bytes)
{
    if (m_running || bytes <= 0) {
        return;
    }
    m_segmentBytes = bytes;
    m_rawSegment.resize(m_segmentBytes);
}

void DmaCaptureDataSource::setZeroCode(float zeroCode)
{
    if (!m_running) {
        m_zeroCode = zeroCode;
    }
}

void DmaCaptureDataSource::setVoltsPerCode(float voltsPerCode)
{
    if (!m_running && voltsPerCode > 0.0F) {
        m_voltsPerCode = voltsPerCode;
    }
}

DataStreamInfo DmaCaptureDataSource::streamInfo() const
{
    DataStreamInfo info;
    info.sourceName = m_deviceInput
        ? QStringLiteral("AC880 AD0 · Linux DMA 1 MiB 段")
        : QStringLiteral("AC880 AD0 · Windows 1 MiB 采样回放");
    info.channelCount = 1;
    info.sampleRateHz = m_sampleRateHz;
    info.channelNames = QStringList() << QStringLiteral("AD0");
    info.unit = QStringLiteral("V");
    return info;
}

bool DmaCaptureDataSource::openInput()
{
    if (m_path.isEmpty()) {
        emit errorOccurred(QStringLiteral("未设置采集设备或二进制采样文件"));
        return false;
    }

#ifdef Q_OS_LINUX
    m_deviceInput = m_path.startsWith(QStringLiteral("/dev/"));
    if (m_deviceInput) {
        m_deviceFd = ::open(m_path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
        if (m_deviceFd < 0) {
            emit errorOccurred(QStringLiteral("无法打开 %1：%2")
                                   .arg(m_path, QString::fromLocal8Bit(std::strerror(errno))));
            return false;
        }
        return true;
    }
#else
    m_deviceInput = false;
#endif

    m_file.setFileName(m_path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(QStringLiteral("无法打开采样文件 %1：%2")
                               .arg(m_path, m_file.errorString()));
        return false;
    }
    if (m_file.size() < m_segmentBytes) {
        emit errorOccurred(QStringLiteral("采样文件至少需要 %1 字节，当前为 %2 字节")
                               .arg(m_segmentBytes).arg(m_file.size()));
        closeInput();
        return false;
    }
    return true;
}

void DmaCaptureDataSource::closeInput()
{
    if (m_timer) {
        m_timer->stop();
    }
    if (m_file.isOpen()) {
        m_file.close();
    }
#ifdef Q_OS_LINUX
    if (m_deviceFd >= 0) {
        ::close(m_deviceFd);
        m_deviceFd = -1;
    }
#endif
    m_deviceInput = false;
}

bool DmaCaptureDataSource::readFileSegment()
{
    qint64 offset = 0;
    while (offset < m_segmentBytes) {
        const qint64 count = m_file.read(m_rawSegment.data() + offset,
                                         m_segmentBytes - offset);
        if (count > 0) {
            offset += count;
            continue;
        }
        if (m_file.atEnd()) {
            if (!m_file.seek(0)) {
                emit errorOccurred(QStringLiteral("采样文件回卷失败"));
                return false;
            }
            continue;
        }
        emit errorOccurred(QStringLiteral("读取采样文件失败：%1").arg(m_file.errorString()));
        return false;
    }
    return true;
}

bool DmaCaptureDataSource::readDeviceSegment()
{
#ifdef Q_OS_LINUX
    struct pollfd descriptor = {m_deviceFd, POLLIN, 0};
    const int ready = ::poll(&descriptor, 1, 100);
    if (ready == 0) {
        return false;
    }
    if (ready < 0) {
        if (errno == EINTR || errno == EAGAIN) {
            return false;
        }
        emit errorOccurred(QStringLiteral("DMA 设备 poll 失败：%1")
                               .arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        emit errorOccurred(QStringLiteral("DMA 设备状态异常，revents=0x%1")
                               .arg(descriptor.revents, 0, 16));
        return false;
    }

    qint64 offset = 0;
    while (offset < m_segmentBytes) {
        const ssize_t count = ::read(m_deviceFd, m_rawSegment.data() + offset,
                                     static_cast<size_t>(m_segmentBytes - offset));
        if (count > 0) {
            offset += count;
            continue;
        }
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) {
            return false;
        }
        emit errorOccurred(QStringLiteral("DMA 设备 read 未返回完整 1 MiB 段：%1")
                               .arg(count < 0
                                        ? QString::fromLocal8Bit(std::strerror(errno))
                                        : QStringLiteral("EOF")));
        return false;
    }
    return true;
#else
    Q_UNUSED(m_deviceFd)
    return false;
#endif
}

void DmaCaptureDataSource::emitSegment()
{
    auto block = QSharedPointer<DataBlock>::create();
    block->firstSampleIndex = m_nextSampleIndex;
    block->monotonicTimestampNs = m_clock.nsecsElapsed();
    block->channelCount = 1;
    block->frameCount = m_segmentBytes;
    block->sampleRateHz = m_sampleRateHz;
    block->interleaved.resize(m_segmentBytes);
    for (int index = 0; index < m_segmentBytes; ++index) {
        const auto code = static_cast<unsigned char>(m_rawSegment.at(index));
        block->interleaved[index] =
            (static_cast<float>(code) - m_zeroCode) * m_voltsPerCode;
    }
    m_nextSampleIndex += m_segmentBytes;
    ++m_segmentSequence;
    emit blockReady(block);
}

void DmaCaptureDataSource::start()
{
    if (m_running) {
        return;
    }
    if (!openInput()) {
        return;
    }

    m_nextSampleIndex = 0;
    m_segmentSequence = 0;
    m_clock.restart();
    if (!m_timer) {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout,
                this, &DmaCaptureDataSource::readNextSegment);
    }
    m_running = true;
    emit streamInfoChanged(streamInfo());
    emit runningChanged(true);
    // One 1 MiB segment takes about 21 ms at 50 MSa/s. A 5 ms wakeup keeps
    // latency bounded without spinning the acquisition event loop at 1 kHz.
    m_timer->start(m_deviceInput ? 5 : 20);
}

void DmaCaptureDataSource::stop()
{
    if (!m_running && !m_file.isOpen() && m_deviceFd < 0) {
        return;
    }
    m_running = false;
    closeInput();
    emit runningChanged(false);
}

void DmaCaptureDataSource::readNextSegment()
{
    if (!m_running) {
        return;
    }
    const bool ready = m_deviceInput ? readDeviceSegment() : readFileSegment();
    if (ready) {
        emitSegment();
    }
}
