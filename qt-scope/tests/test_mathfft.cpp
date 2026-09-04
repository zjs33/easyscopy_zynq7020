#include "processing/fftengine.h"
#include "processing/mathengine.h"

#include <QtTest>

#include <cmath>

class MathFftTest final : public QObject
{
    Q_OBJECT

private slots:
    void computesMathOperationsAndHandlesDivisionByZero();
    void findsFftPeakAndAmplitude();
};

void MathFftTest::computesMathOperationsAndHandlesDivisionByZero()
{
    DataBlock block;
    block.channelCount = 2;
    block.frameCount = 3;
    block.sampleRateHz = 1000.0;
    block.interleaved = {2.0F, 1.0F, 4.0F, 2.0F, 3.0F, 0.0F};

    const DataBlock added = MathEngine().compute(block, MathOperation::Add);
    QVERIFY(added.isValid());
    QCOMPARE(added.interleaved, QVector<float>({3.0F, 6.0F, 3.0F}));

    const DataBlock divided = MathEngine().compute(block, MathOperation::Divide12);
    QCOMPARE(divided.interleaved[0], 2.0F);
    QCOMPARE(divided.interleaved[1], 2.0F);
    QVERIFY(std::isnan(divided.interleaved[2]));
}

void MathFftTest::findsFftPeakAndAmplitude()
{
    constexpr double pi = 3.14159265358979323846;
    constexpr int size = 1024;
    constexpr double sampleRate = 16000.0;
    constexpr double frequency = 1000.0;
    DataBlock block;
    block.channelCount = 1;
    block.frameCount = size;
    block.sampleRateHz = sampleRate;
    block.interleaved.resize(size);
    for (int i = 0; i < size; ++i) {
        block.interleaved[i] = static_cast<float>(
            std::sin(2.0 * pi * frequency * i / sampleRate));
    }

    FftConfig config;
    config.pointCount = size;
    config.window = WindowFunction::Hann;
    config.decibels = false;
    SpectrumResult result = FftEngine().analyze(block, 0, config);
    QVERIFY(result.valid);
    QCOMPARE(result.fftSize, size);
    QVERIFY(std::abs(result.peakFrequencyHz - frequency) <= result.binWidthHz);
    QVERIFY(std::abs(result.peakMagnitude - 1.0) < 0.02);
}

QTEST_APPLESS_MAIN(MathFftTest)

#include "test_mathfft.moc"
