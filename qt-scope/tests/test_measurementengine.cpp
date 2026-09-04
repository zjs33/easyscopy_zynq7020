#include "processing/measurementengine.h"

#include <QtTest>

#include <cmath>

class MeasurementEngineTest final : public QObject
{
    Q_OBJECT

private slots:
    void measuresSineWave();
    void handlesDcAndInvalidInput();
};

void MeasurementEngineTest::measuresSineWave()
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double sampleRate = 10000.0;
    constexpr double frequency = 100.0;
    DataBlock block;
    block.channelCount = 1;
    block.frameCount = 1000;
    block.sampleRateHz = sampleRate;
    block.interleaved.resize(block.frameCount);
    for (int i = 0; i < block.frameCount; ++i) {
        block.interleaved[i] = static_cast<float>(
            0.5 + 2.0 * std::sin(2.0 * pi * frequency * i / sampleRate));
    }

    const MeasurementResult result = MeasurementEngine().analyze(block, 0);
    QVERIFY(result.valid);
    QVERIFY(result.frequencyValid);
    QVERIFY(std::abs(result.peakToPeak - 4.0) < 0.01);
    QVERIFY(std::abs(result.mean - 0.5) < 0.01);
    QVERIFY(std::abs(result.rms - 1.5) < 0.02);
    QVERIFY(std::abs(result.frequencyHz - frequency) < 0.5);
    QVERIFY(std::abs(result.periodSeconds - 0.01) < 1e-5);
}

void MeasurementEngineTest::handlesDcAndInvalidInput()
{
    DataBlock block;
    block.channelCount = 1;
    block.frameCount = 10;
    block.sampleRateHz = 1000.0;
    block.interleaved.fill(1.25F, 10);

    const MeasurementResult dc = MeasurementEngine().analyze(block, 0);
    QVERIFY(dc.valid);
    QVERIFY(!dc.frequencyValid);
    QCOMPARE(dc.mean, 1.25);
    QCOMPARE(dc.peakToPeak, 0.0);

    QVERIFY(!MeasurementEngine().analyze(block, 2).valid);
}

QTEST_APPLESS_MAIN(MeasurementEngineTest)

#include "test_measurementengine.moc"
