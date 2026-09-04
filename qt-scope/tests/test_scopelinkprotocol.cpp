#include "protocol/scopelinkprotocol.h"

#include <QtTest>

class ScopeLinkProtocolTest final : public QObject
{
    Q_OBJECT

private slots:
    void parsesPong()
    {
        const ScopeLinkMessage message = ScopeLinkProtocol::parseLine("PONG 42 123456\r\n");
        QCOMPARE(message.type, ScopeLinkMessage::Type::Pong);
        QVERIFY(message.valid);
        QCOMPARE(message.sequence, quint32(42));
        QCOMPARE(message.deviceMilliseconds, quint32(123456));
    }

    void parsesTwoChannelSample()
    {
        const ScopeLinkMessage message = ScopeLinkProtocol::parseLine(
            "SAMPLE 7 900 -5000 2500");
        QCOMPARE(message.type, ScopeLinkMessage::Type::Sample);
        QVERIFY(message.valid);
        QCOMPARE(message.channels.size(), 2);
        QCOMPARE(message.channels.at(0), -5.0F);
        QCOMPARE(message.channels.at(1), 2.5F);
    }

    void rejectsMalformedSample()
    {
        const ScopeLinkMessage message = ScopeLinkProtocol::parseLine(
            "SAMPLE bad data");
        QCOMPARE(message.type, ScopeLinkMessage::Type::Sample);
        QVERIFY(!message.valid);
    }

    void parsesBoardFftSummary()
    {
        const ScopeLinkMessage message = ScopeLinkProtocol::parseLine(
            "FFT 3 1280 6250 1200 4321");
        QCOMPARE(message.type, ScopeLinkMessage::Type::Fft);
        QVERIFY(message.valid);
        QCOMPARE(message.sequence, quint32(3));
        QCOMPARE(message.deviceMilliseconds, quint32(1280));
        QCOMPARE(message.peakFrequencyHz, 6.25F);
        QCOMPARE(message.peakAmplitudeVolts, 1.2F);
        QCOMPARE(message.computeMicroseconds, quint32(4321));
    }
};

QTEST_APPLESS_MAIN(ScopeLinkProtocolTest)

#include "test_scopelinkprotocol.moc"
