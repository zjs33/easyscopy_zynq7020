#include "processing/fftengine.h"

#include "core/compilercompat.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;

bool isPowerOfTwo(int value)
{
    return value > 0 && (value & (value - 1)) == 0;
}
}

SpectrumResult FftEngine::analyze(const DataBlock &block,
                                  int channel,
                                  const FftConfig &config)
{
    SpectrumResult result;
    const int size = clampValue(config.pointCount, 256, 16384);
    if (!block.isValid() || channel < 0 || channel >= block.channelCount
        || !isPowerOfTwo(size) || block.frameCount < size) {
        return result;
    }

    m_fftScratch.resize(size);
    const int firstFrame = block.frameCount - size;
    double mean = 0.0;
    if (config.removeDc) {
        for (int i = 0; i < size; ++i) {
            mean += block.interleaved[(firstFrame + i) * block.channelCount + channel];
        }
        mean /= size;
    }

    double coherentGainSum = 0.0;
    for (int i = 0; i < size; ++i) {
        const double window = windowValue(config.window, i, size);
        coherentGainSum += window;
        const double sample = block.interleaved[(firstFrame + i) * block.channelCount + channel]
            - mean;
        m_fftScratch[i] = std::complex<double>(sample * window, 0.0);
    }

    transformInPlace(size);
    const int bins = size / 2 + 1;
    result.frequenciesHz.resize(bins);
    result.magnitudes.resize(bins);
    result.valid = true;
    result.fftSize = size;
    result.sampleRateHz = block.sampleRateHz;
    result.binWidthHz = block.sampleRateHz / size;
    result.decibels = config.decibels;

    const double normalization = coherentGainSum > 0.0 ? coherentGainSum : size;
    double bestLinearMagnitude = -1.0;
    for (int bin = 0; bin < bins; ++bin) {
        double magnitude = std::abs(m_fftScratch[bin]) / normalization;
        if (bin != 0 && bin != size / 2) {
            magnitude *= 2.0;
        }
        result.frequenciesHz[bin] = bin * result.binWidthHz;
        if (magnitude > bestLinearMagnitude && bin > 0) {
            bestLinearMagnitude = magnitude;
            result.peakFrequencyHz = result.frequenciesHz[bin];
            result.peakMagnitude = config.decibels
                ? 20.0 * std::log10(std::max(magnitude, 1e-12)) : magnitude;
        }
        result.magnitudes[bin] = config.decibels
            ? 20.0 * std::log10(std::max(magnitude, 1e-12)) : magnitude;
    }
    return result;
}

double FftEngine::windowValue(WindowFunction function, int index, int size) const
{
    if (size <= 1 || function == WindowFunction::Rectangular) {
        return 1.0;
    }
    const double phase = 2.0 * kPi * index / (size - 1);
    switch (function) {
    case WindowFunction::Rectangular:
        return 1.0;
    case WindowFunction::Hann:
        return 0.5 - 0.5 * std::cos(phase);
    case WindowFunction::Hamming:
        return 0.54 - 0.46 * std::cos(phase);
    case WindowFunction::Blackman:
        return 0.42 - 0.5 * std::cos(phase) + 0.08 * std::cos(2.0 * phase);
    }
    return 1.0;
}

void FftEngine::transformInPlace(int size)
{
    for (int i = 1, j = 0; i < size; ++i) {
        int bit = size >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(m_fftScratch[i], m_fftScratch[j]);
        }
    }

    for (int length = 2; length <= size; length <<= 1) {
        const double angle = -2.0 * kPi / length;
        const std::complex<double> root(std::cos(angle), std::sin(angle));
        for (int start = 0; start < size; start += length) {
            std::complex<double> factor(1.0, 0.0);
            for (int offset = 0; offset < length / 2; ++offset) {
                const std::complex<double> even = m_fftScratch[start + offset];
                const std::complex<double> odd = m_fftScratch[start + offset + length / 2]
                    * factor;
                m_fftScratch[start + offset] = even + odd;
                m_fftScratch[start + offset + length / 2] = even - odd;
                factor *= root;
            }
        }
    }
}
