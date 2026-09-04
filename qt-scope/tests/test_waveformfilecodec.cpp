#include "storage/waveformfilecodec.h"

#include <QTemporaryDir>
#include <QtTest>

class WaveformFileCodecTest final : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsCsvAndTxt();
    void rejectsFilesOverMemoryLimit();
};

void WaveformFileCodecTest::roundTripsCsvAndTxt()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    DataBlock original;
    original.firstSampleIndex = 42;
    original.channelCount = 2;
    original.frameCount = 3;
    original.sampleRateHz = 200000.0;
    original.interleaved = {1.0F, -1.0F, 2.5F, -2.5F, 3.25F, -3.25F};
    const QStringList names = {QStringLiteral("CH1"), QStringLiteral("CH2")};

    for (const auto &format : {qMakePair(QStringLiteral("wave.csv"), QChar(',')),
                               qMakePair(QStringLiteral("wave.txt"), QChar('\t'))}) {
        const QString path = directory.filePath(format.first);
        QString error;
        QVERIFY2(WaveformFileCodec::exportText(path, original, names,
                                                format.second, &error),
                 qPrintable(error));
        const WaveformFileResult imported = WaveformFileCodec::importText(path, 100);
        QVERIFY2(imported.success, qPrintable(imported.message));
        QCOMPARE(imported.block.firstSampleIndex, original.firstSampleIndex);
        QCOMPARE(imported.block.channelCount, original.channelCount);
        QCOMPARE(imported.block.frameCount, original.frameCount);
        QCOMPARE(imported.block.sampleRateHz, original.sampleRateHz);
        QCOMPARE(imported.block.interleaved, original.interleaved);
        QCOMPARE(imported.channelNames, names);
    }
}

void WaveformFileCodecTest::rejectsFilesOverMemoryLimit()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DataBlock original;
    original.channelCount = 1;
    original.frameCount = 3;
    original.sampleRateHz = 1000.0;
    original.interleaved = {1.0F, 2.0F, 3.0F};
    const QString path = directory.filePath(QStringLiteral("limited.csv"));
    QString error;
    QVERIFY(WaveformFileCodec::exportText(path, original,
                                          {QStringLiteral("CH1")}, ',', &error));
    const WaveformFileResult result = WaveformFileCodec::importText(path, 2);
    QVERIFY(!result.success);
    QVERIFY(!result.block.isValid());
}

QTEST_APPLESS_MAIN(WaveformFileCodecTest)

#include "test_waveformfilecodec.moc"
