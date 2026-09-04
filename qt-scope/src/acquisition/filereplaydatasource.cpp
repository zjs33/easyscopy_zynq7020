#include "acquisition/filereplaydatasource.h"

#include "core/compilercompat.h"

#include <algorithm>

FileReplayDataSource::FileReplayDataSource(QObject *parent)
    : IDataSource(parent)
{
}

void FileReplayDataSource::loadData(const DataBlock &block,
                                    const QStringList &channelNames)
{
    stop();
    if (!block.isValid()) {
        m_data = {};
        m_channelNames.clear();
        emit errorOccurred(QStringLiteral("无法加载无效的回放数据"));
        return;
    }
    m_data = block;
    m_channelNames = channelNames;
    m_position = 0;
    emit streamInfoChanged(streamInfo());
}

void FileReplayDataSource::clearData()
{
    stop();
    m_data = {};
    m_channelNames.clear();
    m_position = 0;
}

void FileReplayDataSource::setPlaybackSpeed(double speed)
{
    m_speed = clampValue(speed, 0.1, 10.0);
    if (m_timer && m_running) {
        m_timer->start(std::max(1, qRound(m_blockDurationMs / m_speed)));
    }
}

void FileReplayDataSource::setLooping(bool looping)
{
    m_looping = looping;
}

void FileReplayDataSource::rewind()
{
    m_position = 0;
}

void FileReplayDataSource::start()
{
    if (m_running) {
        return;
    }
    if (!m_data.isValid()) {
        emit errorOccurred(QStringLiteral("尚未加载回放文件"));
        return;
    }
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::PreciseTimer);
        connect(m_timer, &QTimer::timeout,
                this, &FileReplayDataSource::emitNextBlock);
    }
    if (m_position >= m_data.frameCount) {
        m_position = 0;
    }
    m_running = true;
    emit streamInfoChanged(streamInfo());
    emit runningChanged(true);
    m_timer->start(std::max(1, qRound(m_blockDurationMs / m_speed)));
}

void FileReplayDataSource::stop()
{
    if (!m_running) {
        return;
    }
    m_running = false;
    if (m_timer) {
        m_timer->stop();
    }
    emit runningChanged(false);
}

void FileReplayDataSource::emitNextBlock()
{
    if (!m_running || !m_data.isValid()) {
        return;
    }
    const int nominalFrames = std::max(1, qRound(
        m_data.sampleRateHz * m_blockDurationMs / 1000.0));
    const int frames = std::min(nominalFrames, m_data.frameCount - m_position);
    auto block = QSharedPointer<DataBlock>::create();
    block->firstSampleIndex = m_data.firstSampleIndex + m_position;
    block->channelCount = m_data.channelCount;
    block->frameCount = frames;
    block->sampleRateHz = m_data.sampleRateHz;
    block->interleaved.resize(frames * m_data.channelCount);
    std::copy_n(m_data.interleaved.cbegin() + m_position * m_data.channelCount,
                frames * m_data.channelCount,
                block->interleaved.begin());
    m_position += frames;
    emit blockReady(block);

    if (m_position >= m_data.frameCount) {
        if (m_looping) {
            m_position = 0;
        } else {
            stop();
            emit playbackFinished();
        }
    }
}

DataStreamInfo FileReplayDataSource::streamInfo() const
{
    DataStreamInfo info;
    info.sourceName = QStringLiteral("文件回放");
    info.channelCount = m_data.channelCount;
    info.sampleRateHz = m_data.sampleRateHz;
    info.channelNames = m_channelNames;
    info.unit = QStringLiteral("V");
    return info;
}
