#include "processing/dacfunctionexpression.h"

#include <QtTest>

class DacFunctionExpressionTest final : public QObject
{
    Q_OBJECT

private slots:
    void precedenceAndParentheses()
    {
        const auto a = DacFunctionExpression::generate(QStringLiteral("1+2*2"), 4);
        const auto b = DacFunctionExpression::generate(QStringLiteral("(1+2)*2"), 4);
        QVERIFY(a.valid);
        QVERIFY(b.valid);
        QCOMPARE(a.clippedCount, 0);
        QCOMPARE(b.clippedCount, 4);
        QCOMPARE(a.volts.first(), 5.0);
        QCOMPARE(b.volts.first(), 5.0);
    }

    void unaryHarmonicsAndWaveforms()
    {
        const auto result = DacFunctionExpression::generate(
            QStringLiteral("-sin(x)+0.5*tri(2*x)+square(x)*0+saw(x)*0+cos(x)*0"));
        QVERIFY2(result.valid, qPrintable(result.error));
        QCOMPARE(result.codes.size(), 256);
        QVERIFY(qAbs(result.volts.at(64) + 0.5) < 0.02);
    }

    void rejectsBadInputAndDivisionByZero()
    {
        QVERIFY(!DacFunctionExpression::generate(QStringLiteral("sin[x]"), 256).valid);
        QVERIFY(!DacFunctionExpression::generate(QStringLiteral("1/(x-x)"), 256).valid);
        QVERIFY(!DacFunctionExpression::generate(QStringLiteral("unknown(x)"), 256).valid);
    }

    void clipsAndUsesInvertingDacEncoding()
    {
        const auto positive = DacFunctionExpression::generate(QStringLiteral("5"), 256);
        const auto zero = DacFunctionExpression::generate(QStringLiteral("0"), 256);
        const auto negative = DacFunctionExpression::generate(QStringLiteral("-5"), 256);
        const auto clipped = DacFunctionExpression::generate(QStringLiteral("10*sin(x)"), 256);
        QVERIFY(positive.valid && zero.valid && negative.valid && clipped.valid);
        QCOMPARE(static_cast<quint8>(positive.codes.at(0)), quint8(0));
        QCOMPARE(static_cast<quint8>(zero.codes.at(0)), quint8(128));
        QCOMPARE(static_cast<quint8>(negative.codes.at(0)), quint8(255));
        QVERIFY(clipped.clippedCount > 0);
        QCOMPARE(positive.crc16, DacFunctionExpression::crc16Ccitt(positive.codes));
    }
};

QTEST_APPLESS_MAIN(DacFunctionExpressionTest)

#include "test_dacfunctionexpression.moc"
