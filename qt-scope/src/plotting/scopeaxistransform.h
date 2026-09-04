#pragma once

#include <QtGlobal>

class ScopeAxisTransform
{
public:
    static constexpr int HorizontalDivisions = 10;
    static constexpr int VerticalDivisions = 8;

    ScopeAxisTransform(double sampleRateHz,
                       double timePerDivisionSeconds,
                       double voltsPerDivision,
                       qint64 firstVisibleSample,
                       qint64 timeReferenceSample);

    [[nodiscard]] double timeWindowSeconds() const;
    [[nodiscard]] double positiveVoltageRange() const;
    [[nodiscard]] qint64 visibleFrameCount() const;
    [[nodiscard]] qint64 sampleAtHorizontalFraction(double fraction) const;
    [[nodiscard]] double horizontalFractionForSample(qint64 sampleIndex) const;
    [[nodiscard]] double relativeTimeAtHorizontalFraction(double fraction) const;
    [[nodiscard]] double relativeTimeForSample(qint64 sampleIndex) const;
    [[nodiscard]] double voltageAtVerticalFraction(double fraction) const;
    [[nodiscard]] double verticalFractionForVoltage(double voltage) const;

private:
    double m_sampleRateHz;
    double m_timePerDivisionSeconds;
    double m_voltsPerDivision;
    qint64 m_firstVisibleSample;
    qint64 m_timeReferenceSample;
};
