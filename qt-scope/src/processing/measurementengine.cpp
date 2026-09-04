#include "processing/measurementengine.h"

#include <algorithm>
#include <cmath>
#include <limits>

MeasurementResult MeasurementEngine::analyze(const DataBlock &block,
                                              int channel) const
{
    MeasurementResult result;
    if (!block.isValid() || channel < 0 || channel >= block.channelCount) {
        return result;
    }

    double sum = 0.0;
    double sumSquares = 0.0;
    result.minimum = std::numeric_limits<double>::max();
    result.maximum = std::numeric_limits<double>::lowest();
    for (int frame = 0; frame < block.frameCount; ++frame) {
        const double value = block.interleaved[frame * block.channelCount + channel];
        result.minimum = std::min(result.minimum, value);
        result.maximum = std::max(result.maximum, value);
        sum += value;
        sumSquares += value * value;
    }

    result.valid = true;
    result.sampleCount = block.frameCount;
    result.peakToPeak = result.maximum - result.minimum;
    result.mean = sum / block.frameCount;
    result.rms = std::sqrt(sumSquares / block.frameCount);

    if (block.frameCount < 3 || result.peakToPeak < 1e-9) {
        return result;
    }

    const double threshold = 0.5 * (result.maximum + result.minimum);
    // A 10% Schmitt band suppresses false zero crossings caused by ADC noise.
    const double hysteresis = std::max(1e-9, result.peakToPeak * 0.10);
    const double low = threshold - hysteresis * 0.5;
    const double high = threshold + hysteresis * 0.5;
    bool risingArmed = false;
    qint64 firstRising = -1;
    qint64 lastRising = -1;
    int risingCount = 0;
    int highSamples = 0;

    for (int frame = 0; frame < block.frameCount; ++frame) {
        const double value = block.interleaved[frame * block.channelCount + channel];
        if (value > threshold) {
            ++highSamples;
        }
        if (value <= low) {
            risingArmed = true;
        }
        if (risingArmed && value >= high) {
            risingArmed = false;
            const qint64 absoluteIndex = block.firstSampleIndex + frame;
            if (firstRising < 0) {
                firstRising = absoluteIndex;
            }
            lastRising = absoluteIndex;
            ++risingCount;
        }
    }

    result.dutyCyclePercent = 100.0 * highSamples / block.frameCount;
    if (risingCount >= 2 && lastRising > firstRising) {
        result.periodSeconds = static_cast<double>(lastRising - firstRising)
            / ((risingCount - 1) * block.sampleRateHz);
        if (result.periodSeconds > 0.0) {
            result.frequencyHz = 1.0 / result.periodSeconds;
            result.frequencyValid = true;
        }
    }
    return result;
}
