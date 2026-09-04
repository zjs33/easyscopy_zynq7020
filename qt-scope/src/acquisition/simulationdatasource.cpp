#include "acquisition/simulationdatasource.h"

#include "core/compilercompat.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

SimulationDataSource::SimulationDataSource(double sampleRateHz,
                                           int channelCount,
                                           int blockDurationMs,
                                           QObject *parent)
    : IDataSource(parent)
    , m_sampleRateHz(std::max(1.0, sampleRateHz))
    , m_channelCount(clampValue(channelCount, 1, 8))
    , m_blockDurationMs(clampValue(blockDurationMs, 5, 1000))
{
    m_framesPerBlock = std::max(1, qRound(m_sampleRateHz * m_blockDurationMs / 1000.0));
}

void SimulationDataSource::start()
{
    if (m_running) {
        return;
    }

    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setTimerType(m_blockDurationMs >= 20
                                  ? Qt::CoarseTimer : Qt::PreciseTimer);
        connect(m_timer, &QTimer::timeout,
                this, &SimulationDataSource::generateBlock);
    }

    m_running = true;
    m_clock.restart();
    emit streamInfoChanged(streamInfo());
    emit runningChanged(true);
    m_timer->start(m_blockDurationMs);
}

void SimulationDataSource::stop()
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

void SimulationDataSource::generateBlock()
{
    if (!m_running) {
        return;
    }

    auto block = QSharedPointer<DataBlock>::create();
    block->firstSampleIndex = m_nextSampleIndex;
    block->monotonicTimestampNs = m_clock.nsecsElapsed();
    block->channelCount = m_channelCount;
    block->frameCount = m_framesPerBlock;
    block->sampleRateHz = m_sampleRateHz;
    block->interleaved.resize(m_framesPerBlock * m_channelCount);

    for (int frame = 0; frame < m_framesPerBlock; ++frame) {
        const double timeSeconds = static_cast<double>(m_nextSampleIndex + frame)
            / m_sampleRateHz;
        for (int channel = 0; channel < m_channelCount; ++channel) {
            block->interleaved[frame * m_channelCount + channel]
                = sampleForChannel(channel, timeSeconds);
        }
    }

    m_nextSampleIndex += m_framesPerBlock;
    emit blockReady(block);
}

float SimulationDataSource::nextNoise()
{
    m_randomState = 1664525U * m_randomState + 1013904223U;
    const float normalized = static_cast<float>((m_randomState >> 8U) & 0x00ffffffU)
        / static_cast<float>(0x01000000U);
    return normalized - 0.5F;
}

float SimulationDataSource::sampleForChannel(int channel, double t)
{
    const float noise = 0.04F * nextNoise();
    switch (channel) {
    case 0:
        return static_cast<float>(0.78 * std::sin(2.0 * kPi * 50.0 * t)) + noise;
    case 1:
        return static_cast<float>(0.62 * std::sin(2.0 * kPi * 120.0 * t + 0.5)) + noise;
    case 2:
        return static_cast<float>(0.42 * std::sin(2.0 * kPi * 1000.0 * t)
                                  + 0.23 * std::sin(2.0 * kPi * 2400.0 * t)) + noise;
    case 3:
        return std::sin(2.0 * kPi * 25.0 * t) >= 0.0 ? 0.68F : -0.68F;
    case 4: {
        const double phase = std::fmod(t * 10.0, 1.0);
        return static_cast<float>(1.4 * phase - 0.7) + noise;
    }
    case 5:
        return 0.85F * nextNoise();
    case 6: {
        const double envelope = 0.42 + 0.25 * std::sin(2.0 * kPi * 2.0 * t);
        return static_cast<float>(envelope * std::sin(2.0 * kPi * 400.0 * t)) + noise;
    }
    case 7: {
        const double pulsePhase = std::fmod(t, 0.2);
        return (pulsePhase < 0.008 ? 0.92F : -0.12F) + noise;
    }
    default:
        return noise;
    }
}

DataStreamInfo SimulationDataSource::streamInfo() const
{
    DataStreamInfo info;
    info.sourceName = QStringLiteral("AC880 AD0 · Windows 仿真数据源");
    info.channelCount = m_channelCount;
    info.sampleRateHz = m_sampleRateHz;
    info.unit = QStringLiteral("V");
    for (int channel = 0; channel < m_channelCount; ++channel) {
        info.channelNames.append(channel == 0
                                     ? QStringLiteral("AD0")
                                     : QStringLiteral("CH%1").arg(channel + 1));
    }
    return info;
}
