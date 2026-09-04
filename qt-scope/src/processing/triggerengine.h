#pragma once

#include "acquisition/datablock.h"

enum class TriggerType {
    Edge,
    PulseWidth,
    Video
};

enum class TriggerMode {
    Auto,
    Normal,
    Single
};

enum class TriggerSlope {
    Rising,
    Falling,
    Either
};

enum class PulseWidthCondition {
    LessThan,
    GreaterThan,
    InsideRange,
    OutsideRange
};

enum class VideoStandard {
    PAL,
    NTSC
};

enum class VideoSync {
    Line,
    Field
};

struct TriggerConfig
{
    TriggerType type = TriggerType::Edge;
    TriggerMode mode = TriggerMode::Auto;
    TriggerSlope slope = TriggerSlope::Rising;
    int sourceChannel = 0;
    double level = 0.0;
    double hysteresis = 0.02;
    double holdoffSeconds = 0.001;
    double autoTimeoutSeconds = 0.2;
    double pretriggerRatio = 0.5;

    bool positivePulse = true;
    PulseWidthCondition pulseCondition = PulseWidthCondition::GreaterThan;
    double minimumPulseWidthSeconds = 5e-6;
    double maximumPulseWidthSeconds = 100e-6;

    VideoStandard videoStandard = VideoStandard::PAL;
    VideoSync videoSync = VideoSync::Line;
};

struct TriggerEvent
{
    qint64 sampleIndex = -1;
    TriggerType type = TriggerType::Edge;
    bool automatic = false;
};

class TriggerResult
{
public:
    TriggerResult() = default;
    explicit TriggerResult(const TriggerEvent &event)
        : m_event(event)
        , m_valid(true)
    {
    }

    [[nodiscard]] bool hasValue() const { return m_valid; }
    explicit operator bool() const { return m_valid; }
    [[nodiscard]] const TriggerEvent *operator->() const { return &m_event; }

private:
    TriggerEvent m_event;
    bool m_valid = false;
};

class TriggerEngine
{
public:
    void setConfig(const TriggerConfig &config);
    [[nodiscard]] const TriggerConfig &config() const { return m_config; }
    void reset();
    void armSingle();
    [[nodiscard]] bool isSingleArmed() const { return m_singleArmed; }
    [[nodiscard]] TriggerResult process(const DataBlock &block);

private:
    [[nodiscard]] bool triggerAllowed(qint64 sampleIndex, double sampleRateHz) const;
    [[nodiscard]] bool pulseWidthMatches(double widthSeconds) const;
    [[nodiscard]] TriggerResult acceptEvent(qint64 sampleIndex, bool automatic);
    [[nodiscard]] TriggerResult processEdge(float value,
                                             qint64 sampleIndex,
                                             double sampleRateHz);
    [[nodiscard]] TriggerResult processPulse(float value,
                                              qint64 sampleIndex,
                                              double sampleRateHz);
    [[nodiscard]] TriggerResult processVideo(float value,
                                              qint64 sampleIndex,
                                              double sampleRateHz);

    TriggerConfig m_config;
    bool m_hasPrevious = false;
    float m_previousValue = 0.0F;
    bool m_risingArmed = false;
    bool m_fallingArmed = false;
    bool m_logicHigh = false;
    bool m_pulseActive = false;
    qint64 m_pulseStartSample = 0;
    qint64 m_lastTriggerSample = -1;
    qint64 m_lastAutomaticSample = -1;
    bool m_singleArmed = true;
};
