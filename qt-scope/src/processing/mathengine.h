#pragma once

#include "acquisition/datablock.h"

enum class MathOperation {
    Add,
    Subtract12,
    Subtract21,
    Multiply,
    Divide12,
    Divide21
};

class MathEngine
{
public:
    [[nodiscard]] DataBlock compute(const DataBlock &block,
                                    MathOperation operation,
                                    double divisionEpsilon = 1e-9) const;
};
