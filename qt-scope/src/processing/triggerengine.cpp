#include "processing/triggerengine.h"

#include "core/compilercompat.h"

#include <algorithm>
#include <cmath>

void TriggerEngine::setConfig(const TriggerConfig &config)
{
    m_config = config;
    m_config.sourceChannel = std::max(0, m_config.sourceChannel);
    m_config.hysteresis = std::max(0.0, m_config.hysteresis);
    m_config.holdoffSeconds = std::max(0.0, m_config.holdoffSeconds);
    m_config.autoTimeoutSeconds = std::max(0.001, m_config.autoTimeoutSeconds);
    m_config.pretriggerRatio = clampValue(m_config.pretriggerRatio, 0.0, 1.0);
    m_config.minimumPulseWidthSeconds = std::max(0.0, m_config.minimumPulseWidthSeconds);
    m_config.maximumPulseWidthSeconds = std::max(m_config.minimumPulseWidthSeconds,
                                                 m_config.maximumPulseWidthSeconds);
    reset();
}

void TriggerEngine::reset()
{
    m_hasPrevious = false;
    m_previousValue = 0.0F;
    m_risingArmed = false;
    m_fallingArmed = false;
    m_logicHigh = false;
    m_pulseActive = false;
    m_pulseStartSample = 0;
    m_lastTriggerSample = -1;
    m_lastAutomaticSample = -1;
    m_singleArmed = true;
}

void TriggerEngine::armSingle()
{
    m_singleArmed = true;
    m_lastTriggerSample = -1;
    m_lastAutomaticSample = -1;
}

TriggerResult TriggerEngine::process(const DataBlock &block)
{
    if (!block.isValid() || m_config.sourceChannel >= block.channelCount) {
        return TriggerResult();
    }
    if (m_config.mode == TriggerMode::Single && !m_singleArmed) {
        return TriggerResult();
    }

    if (m_lastTriggerSample < 0 && m_lastAutomaticSample < 0) {
        m_lastAutomaticSample = block.firstSampleIndex;
    }

    TriggerResult firstEvent;
    for (int frame = 0; frame < block.frameCount; ++frame) {
        const qint64 sampleIndex = block.firstSampleIndex + frame;
        const float value = block.interleaved[frame * block.channelCount
                                               + m_config.sourceChannel];
        TriggerResult event;
        switch (m_config.type) {
        case TriggerType::Edge:
            event = processEdge(value, sampleIndex, block.sampleRateHz);
            break;
        case TriggerType::PulseWidth:
            event = processPulse(value, sampleIndex, block.sampleRateHz);
            break;
        case TriggerType::Video:
            event = processVideo(value, sampleIndex, block.sampleRateHz);
            break;
        }

        m_previousValue = value;
        m_hasPrevious = true;
        if (event) {
            if (!firstEvent) {
                firstEvent = event;
            }
        }
    }

    if (!firstEvent && m_config.mode == TriggerMode::Auto) {
        const qint64 lastSample = block.firstSampleIndex + block.frameCount - 1;
        const qint64 timeoutSamples = std::max<qint64>(
            1, qRound64(m_config.autoTimeoutSeconds * block.sampleRateHz));
        const qint64 reference = std::max(m_lastTriggerSample, m_lastAutomaticSample);
        if (reference < 0 || lastSample - reference >= timeoutSamples) {
            firstEvent = acceptEvent(lastSample, true);
        }
    }
    return firstEvent;
}

bool TriggerEngine::triggerAllowed(qint64 sampleIndex, double sampleRateHz) const
{
    if (m_config.mode == TriggerMode::Single && !m_singleArmed) {
        return false;
    }
    if (m_lastTriggerSample < 0) {
        return true;
    }
    const qint64 holdoffSamples = qRound64(m_config.holdoffSeconds * sampleRateHz);
    return sampleIndex - m_lastTriggerSample >= holdoffSamples;
}

bool TriggerEngine::pulseWidthMatches(double widthSeconds) const
{
    switch (m_config.pulseCondition) {
    case PulseWidthCondition::LessThan:
        return widthSeconds < m_config.minimumPulseWidthSeconds;
    case PulseWidthCondition::GreaterThan:
        return widthSeconds > m_config.minimumPulseWidthSeconds;
    case PulseWidthCondition::InsideRange:
        return widthSeconds >= m_config.minimumPulseWidthSeconds
            && widthSeconds <= m_config.maximumPulseWidthSeconds;
    case PulseWidthCondition::OutsideRange:
        return widthSeconds < m_config.minimumPulseWidthSeconds
            || widthSeconds > m_config.maximumPulseWidthSeconds;
    }
    return false;
}

TriggerResult TriggerEngine::acceptEvent(qint64 sampleIndex, bool automatic)
{
    if (m_config.mode == TriggerMode::Single && !m_singleArmed) {
        return TriggerResult();
    }
    if (automatic) {
        m_lastAutomaticSample = sampleIndex;
    } else {
        m_lastTriggerSample = sampleIndex;
    }
    if (m_config.mode == TriggerMode::Single) {
        m_singleArmed = false;
    }
    TriggerEvent event;
    event.sampleIndex = sampleIndex;
    event.type = m_config.type;
    event.automatic = automatic;
    return TriggerResult(event);
}

TriggerResult TriggerEngine::processEdge(float value,
                                         qint64 sampleIndex,
                                         double sampleRateHz)
{
    const double low = m_config.level - m_config.hysteresis * 0.5;
    const double high = m_config.level + m_config.hysteresis * 0.5;
    if (value <= low) {
        m_risingArmed = true;
    }
    if (value >= high) {
        m_fallingArmed = true;
    }

    const bool rising = m_risingArmed && value >= high;
    const bool falling = m_fallingArmed && value <= low;
    if (rising) {
        m_risingArmed = false;
    }
    if (falling) {
        m_fallingArmed = false;
    }

    const bool slopeMatches = (m_config.slope == TriggerSlope::Rising && rising)
        || (m_config.slope == TriggerSlope::Falling && falling)
        || (m_config.slope == TriggerSlope::Either && (rising || falling));
    if (slopeMatches && triggerAllowed(sampleIndex, sampleRateHz)) {
        return acceptEvent(sampleIndex, false);
    }
    return TriggerResult();
}

TriggerResult TriggerEngine::processPulse(float value,
                                          qint64 sampleIndex,
                                          double sampleRateHz)
{
    const double low = m_config.level - m_config.hysteresis * 0.5;
    const double high = m_config.level + m_config.hysteresis * 0.5;
    if (!m_logicHigh && value >= high) {
        m_logicHigh = true;
    } else if (m_logicHigh && value <= low) {
        m_logicHigh = false;
    }

    const bool desiredState = m_config.positivePulse ? m_logicHigh : !m_logicHigh;
    if (desiredState && !m_pulseActive) {
        m_pulseActive = true;
        m_pulseStartSample = sampleIndex;
    } else if (!desiredState && m_pulseActive) {
        m_pulseActive = false;
        const double width = static_cast<double>(sampleIndex - m_pulseStartSample)
            / sampleRateHz;
        if (pulseWidthMatches(width)
            && triggerAllowed(sampleIndex, sampleRateHz)) {
            return acceptEvent(m_pulseStartSample, false);
        }
    }
    return TriggerResult();
}

TriggerResult TriggerEngine::processVideo(float value,
                                          qint64 sampleIndex,
                                          double sampleRateHz)
{
    const double low = m_config.level - m_config.hysteresis * 0.5;
    const double high = m_config.level + m_config.hysteresis * 0.5;
    const bool wasLow = m_logicHigh;
    if (!m_logicHigh && value <= low) {
        m_logicHigh = true;
        m_pulseStartSample = sampleIndex;
    } else if (m_logicHigh && value >= high) {
        m_logicHigh = false;
        const double lowWidth = static_cast<double>(sampleIndex - m_pulseStartSample)
            / sampleRateHz;
        const double minimumFieldSync = 20e-6;
        const bool syncMatches = m_config.videoSync == VideoSync::Line
            || lowWidth >= minimumFieldSync;
        const double linePeriod = m_config.videoStandard == VideoStandard::PAL
            ? 64.0e-6 : 63.556e-6;
        const bool standardHoldoffSatisfied = m_config.videoSync == VideoSync::Field
            || m_lastTriggerSample < 0
            || static_cast<double>(sampleIndex - m_lastTriggerSample) / sampleRateHz
                >= linePeriod * 0.8;
        if (syncMatches && standardHoldoffSatisfied
            && triggerAllowed(sampleIndex, sampleRateHz)) {
            return acceptEvent(m_pulseStartSample, false);
        }
    }
    Q_UNUSED(wasLow)
    return TriggerResult();
}
