#include "acquisition/dmacapturedatasource.h"

#include <QSignalSpy>
#include <QTemporaryFile>
#include <QtTest>

#include <cmath>

class DmaCaptureDataSourceTest final : public QObject
{
    Q_OBJECT

private slots:
    void readsOneMegabyteSegment();
};

void DmaCaptureDataSourceTest::readsOneMegabyteSegment()
{
    QTemporaryFile capture;
    QVERIFY(capture.open());

    QByteArray payload(1024 * 1024, '\0');
    for (int index = 0; index < payload.size(); ++index) {
        payload[index] = static_cast<char>(index & 0xff);
    }
    QCOMPARE(capture.write(payload), static_cast<qint64>(payload.size()));
    QVERIFY(capture.flush());

    DmaCaptureDataSource source;
    source.setPath(capture.fileName());
    source.setSampleRate(50000000.0);
    QSignalSpy blocks(&source, &IDataSource::blockReady);
    QSignalSpy errors(&source, &IDataSource::errorOccurred);

    source.start();
    QTRY_VERIFY_WITH_TIMEOUT(blocks.count() >= 1, 2000);
    if (!errors.isEmpty()) {
        QFAIL(qPrintable(errors.first().at(0).toString()));
    }

    const auto block = qvariant_cast<DataBlockPtr>(blocks.takeFirst().at(0));
    QVERIFY(block);
    QVERIFY(block->isValid());
    QCOMPARE(block->channelCount, 1);
    QCOMPARE(block->frameCount, 1024 * 1024);
    QCOMPARE(block->sampleRateHz, 50000000.0);
    QCOMPARE(block->firstSampleIndex, qint64(0));
    QVERIFY(std::abs(block->interleaved.at(0) + 1.0F) < 1e-6F);
    QVERIFY(std::abs(block->interleaved.at(128)) < 1e-6F);
    QVERIFY(std::abs(block->interleaved.at(255) - 127.0F / 128.0F) < 1e-6F);

    source.stop();
}

QTEST_MAIN(DmaCaptureDataSourceTest)

#include "test_dmacapturedatasource.moc"
