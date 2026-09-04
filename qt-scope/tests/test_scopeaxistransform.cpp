#include "plotting/scopeaxistransform.h"

#include <QtTest>

#include <cmath>

class ScopeAxisTransformTest final : public QObject
{
    Q_OBJECT

private slots:
    void convertsTenByEightDivisionGrid();
    void usesTriggerSampleAsTimeZero();
    void roundTripsScreenFractions();
};

void ScopeAxisTransformTest::convertsTenByEightDivisionGrid()
{
    const ScopeAxisTransform axis(200000.0, 0.01, 0.2, 10000, 30000);
    QCOMPARE(axis.timeWindowSeconds(), 0.1);
    QCOMPARE(axis.positiveVoltageRange(), 0.8);
    QCOMPARE(axis.visibleFrameCount(), 20000);
    QCOMPARE(axis.voltageAtVerticalFraction(0.0), 0.8);
    QCOMPARE(axis.voltageAtVerticalFraction(0.5), 0.0);
    QCOMPARE(axis.voltageAtVerticalFraction(1.0), -0.8);
}

void ScopeAxisTransformTest::usesTriggerSampleAsTimeZero()
{
    const ScopeAxisTransform axis(1000.0, 0.01, 0.5, 100, 150);
    QVERIFY(std::abs(axis.relativeTimeAtHorizontalFraction(0.0) + 0.05) < 1e-12);
    QVERIFY(std::abs(axis.relativeTimeAtHorizontalFraction(0.5)) < 1e-12);
    QVERIFY(std::abs(axis.relativeTimeAtHorizontalFraction(1.0) - 0.05) < 1e-12);
}

void ScopeAxisTransformTest::roundTripsScreenFractions()
{
    const ScopeAxisTransform axis(200000.0, 0.002, 1.0, 500000, 504000);
    for (const double fraction : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        const qint64 sample = axis.sampleAtHorizontalFraction(fraction);
        QVERIFY(std::abs(axis.horizontalFractionForSample(sample) - fraction) < 1e-12);
        const double voltage = axis.voltageAtVerticalFraction(fraction);
        QVERIFY(std::abs(axis.verticalFractionForVoltage(voltage) - fraction) < 1e-12);
    }
}

QTEST_APPLESS_MAIN(ScopeAxisTransformTest)

#include "test_scopeaxistransform.moc"
