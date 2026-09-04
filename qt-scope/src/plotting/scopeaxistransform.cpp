#include "plotting/scopeaxistransform.h"

#include "core/compilercompat.h"

#include <QtMath>

#include <algorithm>

ScopeAxisTransform::ScopeAxisTransform(double sampleRateHz,
                                       double timePerDivisionSeconds,
                                       double voltsPerDivision,
                                       qint64 firstVisibleSample,
                                       qint64 timeReferenceSample)
    : m_sampleRateHz(std::max(1.0, sampleRateHz))
    , m_timePerDivisionSeconds(std::max(1e-9, timePerDivisionSeconds))
    , m_voltsPerDivision(std::max(1e-9, voltsPerDivision))
    , m_firstVisibleSample(firstVisibleSample)
    , m_timeReferenceSample(timeReferenceSample)
{
}

double ScopeAxisTransform::timeWindowSeconds() const
{
    return HorizontalDivisions * m_timePerDivisionSeconds;
}

double ScopeAxisTransform::positiveVoltageRange() const
{
    return VerticalDivisions * 0.5 * m_voltsPerDivision;
}

qint64 ScopeAxisTransform::visibleFrameCount() const
{
    return std::max<qint64>(1, qCeil(timeWindowSeconds() * m_sampleRateHz));
}

qint64 ScopeAxisTransform::sampleAtHorizontalFraction(double fraction) const
{
    return m_firstVisibleSample
        + qRound64(clampValue(fraction, 0.0, 1.0) * visibleFrameCount());
}

double ScopeAxisTransform::horizontalFractionForSample(qint64 sampleIndex) const
{
    return static_cast<double>(sampleIndex - m_firstVisibleSample)
        / visibleFrameCount();
}

double ScopeAxisTransform::relativeTimeAtHorizontalFraction(double fraction) const
{
    const double sample = static_cast<double>(m_firstVisibleSample)
        + clampValue(fraction, 0.0, 1.0) * visibleFrameCount();
    return (sample - static_cast<double>(m_timeReferenceSample)) / m_sampleRateHz;
}

double ScopeAxisTransform::relativeTimeForSample(qint64 sampleIndex) const
{
    return static_cast<double>(sampleIndex - m_timeReferenceSample) / m_sampleRateHz;
}

double ScopeAxisTransform::voltageAtVerticalFraction(double fraction) const
{
    return (0.5 - clampValue(fraction, 0.0, 1.0))
        * VerticalDivisions * m_voltsPerDivision;
}

double ScopeAxisTransform::verticalFractionForVoltage(double voltage) const
{
    return 0.5 - voltage / (VerticalDivisions * m_voltsPerDivision);
}
