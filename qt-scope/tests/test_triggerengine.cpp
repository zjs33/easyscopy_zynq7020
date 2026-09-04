#include "processing/triggerengine.h"

#include <QtTest>

class TriggerEngineTest final : public QObject
{
    Q_OBJECT

private slots:
    void detectsRisingAndFallingEdges();
    void detectsPulseWidth();
    void singleModeRequiresRearm();
    void autoModeProducesTimeoutTrigger();
};

static DataBlock monoBlock(qint64 first, double rate,
                           std::initializer_list<float> samples)
{
    DataBlock block;
    block.firstSampleIndex = first;
    block.channelCount = 1;
    block.frameCount = static_cast<int>(samples.size());
    block.sampleRateHz = rate;
    block.interleaved = QVector<float>(samples);
    return block;
}

void TriggerEngineTest::detectsRisingAndFallingEdges()
{
    TriggerEngine engine;
    TriggerConfig config;
    config.mode = TriggerMode::Normal;
    config.level = 0.0;
    config.hysteresis = 0.1;
    config.slope = TriggerSlope::Rising;
    engine.setConfig(config);

    auto event = engine.process(monoBlock(10, 1000.0, {-1.0F, -0.2F, 0.2F, 1.0F}));
    QVERIFY(event.hasValue());
    QCOMPARE(event->sampleIndex, 12);

    config.slope = TriggerSlope::Falling;
    engine.setConfig(config);
    event = engine.process(monoBlock(20, 1000.0, {1.0F, 0.2F, -0.2F, -1.0F}));
    QVERIFY(event.hasValue());
    QCOMPARE(event->sampleIndex, 22);
}

void TriggerEngineTest::detectsPulseWidth()
{
    TriggerEngine engine;
    TriggerConfig config;
    config.type = TriggerType::PulseWidth;
    config.mode = TriggerMode::Normal;
    config.level = 0.0;
    config.hysteresis = 0.1;
    config.positivePulse = true;
    config.pulseCondition = PulseWidthCondition::GreaterThan;
    config.minimumPulseWidthSeconds = 0.002;
    engine.setConfig(config);

    const auto event = engine.process(
        monoBlock(0, 1000.0, {-1.0F, 1.0F, 1.0F, 1.0F, -1.0F}));
    QVERIFY(event.hasValue());
    QCOMPARE(event->sampleIndex, 1);
}

void TriggerEngineTest::singleModeRequiresRearm()
{
    TriggerEngine engine;
    TriggerConfig config;
    config.mode = TriggerMode::Single;
    config.slope = TriggerSlope::Rising;
    engine.setConfig(config);

    QVERIFY(engine.process(monoBlock(0, 1000.0, {-1.0F, 1.0F})).hasValue());
    QVERIFY(!engine.isSingleArmed());
    QVERIFY(!engine.process(monoBlock(2, 1000.0, {-1.0F, 1.0F})).hasValue());

    engine.armSingle();
    QVERIFY(engine.process(monoBlock(4, 1000.0, {-1.0F, 1.0F})).hasValue());
}

void TriggerEngineTest::autoModeProducesTimeoutTrigger()
{
    TriggerEngine engine;
    TriggerConfig config;
    config.mode = TriggerMode::Auto;
    config.autoTimeoutSeconds = 0.005;
    config.level = 2.0;
    engine.setConfig(config);

    const auto event = engine.process(
        monoBlock(0, 1000.0, {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F}));
    QVERIFY(event.hasValue());
    QVERIFY(event->automatic);
}

QTEST_APPLESS_MAIN(TriggerEngineTest)

#include "test_triggerengine.moc"
