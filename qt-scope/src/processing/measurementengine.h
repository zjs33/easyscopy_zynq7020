#pragma once

#include "acquisition/datablock.h"

struct MeasurementResult
{
    bool valid = false;
    int sampleCount = 0;
    double minimum = 0.0;
    double maximum = 0.0;
    double peakToPeak = 0.0;
    double mean = 0.0;
    double rms = 0.0;
    double frequencyHz = 0.0;
    double periodSeconds = 0.0;
    double dutyCyclePercent = 0.0;
    bool frequencyValid = false;
};

class MeasurementEngine
{
public:
    [[nodiscard]] MeasurementResult analyze(const DataBlock &block,
                                             int channel) const;
};
