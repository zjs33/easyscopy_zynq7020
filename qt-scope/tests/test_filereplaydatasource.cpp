#include "acquisition/filereplaydatasource.h"

#include <QSignalSpy>
#include <QtTest>

class FileReplayDataSourceTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void replaysBoundedBlocksAndStops();
    void clearsLoadedReplayData();
};

void FileReplayDataSourceTest::initTestCase()
{
    qRegisterMetaType<DataStreamInfo>();
    qRegisterMetaType<DataBlockPtr>();
}

void FileReplayDataSourceTest::replaysBoundedBlocksAndStops()
{
    DataBlock data;
    data.firstSampleIndex = 100;
    data.channelCount = 2;
    data.frameCount = 20;
    data.sampleRateHz = 1000.0;
    data.interleaved.resize(40);
    for (int i = 0; i < data.interleaved.size(); ++i) {
        data.interleaved[i] = static_cast<float>(i);
    }

    FileReplayDataSource source;
    QSignalSpy blocks(&source, &IDataSource::blockReady);
    QSignalSpy finished(&source, &FileReplayDataSource::playbackFinished);
    source.loadData(data, {QStringLiteral("A"), QStringLiteral("B")});
    source.start();

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1000);
    QCOMPARE(blocks.count(), 2);
    const DataBlockPtr first = qvariant_cast<DataBlockPtr>(blocks.at(0).at(0));
    const DataBlockPtr second = qvariant_cast<DataBlockPtr>(blocks.at(1).at(0));
    QVERIFY(first && second);
    QCOMPARE(first->firstSampleIndex, 100);
    QCOMPARE(first->frameCount, 10);
    QCOMPARE(second->firstSampleIndex, 110);
    QCOMPARE(second->frameCount, 10);
}

void FileReplayDataSourceTest::clearsLoadedReplayData()
{
    DataBlock data;
    data.channelCount = 2;
    data.frameCount = 4;
    data.sampleRateHz = 1000.0;
    data.interleaved.fill(0.25F, 8);

    FileReplayDataSource source;
    QSignalSpy errors(&source, &IDataSource::errorOccurred);
    QSignalSpy blocks(&source, &IDataSource::blockReady);
    source.loadData(data, {QStringLiteral("CH1"), QStringLiteral("CH2")});
    source.clearData();
    source.start();

    QCOMPARE(errors.count(), 1);
    QCOMPARE(blocks.count(), 0);
}

QTEST_GUILESS_MAIN(FileReplayDataSourceTest)

#include "test_filereplaydatasource.moc"
