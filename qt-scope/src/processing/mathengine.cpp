#include "processing/mathengine.h"

#include <cmath>
#include <limits>

DataBlock MathEngine::compute(const DataBlock &block,
                              MathOperation operation,
                              double divisionEpsilon) const
{
    DataBlock result;
    if (!block.isValid() || block.channelCount < 2) {
        return result;
    }

    result.firstSampleIndex = block.firstSampleIndex;
    result.monotonicTimestampNs = block.monotonicTimestampNs;
    result.channelCount = 1;
    result.frameCount = block.frameCount;
    result.sampleRateHz = block.sampleRateHz;
    result.interleaved.resize(block.frameCount);
    const float invalid = std::numeric_limits<float>::quiet_NaN();
    const double epsilon = std::max(0.0, divisionEpsilon);

    for (int frame = 0; frame < block.frameCount; ++frame) {
        const float first = block.interleaved[frame * block.channelCount];
        const float second = block.interleaved[frame * block.channelCount + 1];
        float value = 0.0F;
        switch (operation) {
        case MathOperation::Add:
            value = first + second;
            break;
        case MathOperation::Subtract12:
            value = first - second;
            break;
        case MathOperation::Subtract21:
            value = second - first;
            break;
        case MathOperation::Multiply:
            value = first * second;
            break;
        case MathOperation::Divide12:
            value = std::abs(second) <= epsilon ? invalid : first / second;
            break;
        case MathOperation::Divide21:
            value = std::abs(first) <= epsilon ? invalid : second / first;
            break;
        }
        result.interleaved[frame] = value;
    }
    return result;
}
