#pragma once

#include "acquisition/datablock.h"

#include <QMetaType>
#include <QVector>

#include <complex>

enum class WindowFunction {
    Rectangular,
    Hann,
    Hamming,
    Blackman
};

struct FftConfig
{
    int pointCount = 4096;
    WindowFunction window = WindowFunction::Hann;
    bool removeDc = true;
    bool decibels = true;
};

struct SpectrumResult
{
    bool valid = false;
    int fftSize = 0;
    double sampleRateHz = 0.0;
    double binWidthHz = 0.0;
    bool decibels = true;
    double peakFrequencyHz = 0.0;
    double peakMagnitude = 0.0;
    QVector<double> frequenciesHz;
    QVector<double> magnitudes;
};

Q_DECLARE_METATYPE(FftConfig)
Q_DECLARE_METATYPE(SpectrumResult)

class FftEngine
{
public:
    [[nodiscard]] SpectrumResult analyze(const DataBlock &block,
                                         int channel,
                                         const FftConfig &config);

private:
    [[nodiscard]] double windowValue(WindowFunction function,
                                     int index,
                                     int size) const;
    void transformInPlace(int size);

    QVector<std::complex<double>> m_fftScratch;
};
