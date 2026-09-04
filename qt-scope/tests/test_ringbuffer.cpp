#include "plotting/sampleringbuffer.h"

#include <QtTest>

class RingBufferTest final : public QObject
{
    Q_OBJECT

private slots:
    void rejectsInvalidConfiguration();
    void preservesChannelOrder();
    void keepsNewestFramesAfterWrap();
    void tracksAbsoluteSampleIndicesAndSnapshots();
    void acceleratesRangeQueriesWithEnvelope();
};

void RingBufferTest::rejectsInvalidConfiguration()
{
    SampleRingBuffer buffer;
    buffer.configure(0, 0);

    DataBlock block;
    block.channelCount = 1;
    block.frameCount = 1;
    block.sampleRateHz = 1000.0;
    block.interleaved = {1.0F};

    QVERIFY(!buffer.append(block));
    QCOMPARE(buffer.sizeFrames(), 0);
}

void RingBufferTest::preservesChannelOrder()
{
    SampleRingBuffer buffer;
    buffer.configure(2, 4);

    DataBlock block;
    block.channelCount = 2;
    block.frameCount = 2;
    block.sampleRateHz = 1000.0;
    block.interleaved = {1.0F, 10.0F, 2.0F, 20.0F};

    QVERIFY(buffer.append(block));
    QCOMPARE(buffer.sizeFrames(), 2);
    QCOMPARE(buffer.valueFromOldest(0, 0), 1.0F);
    QCOMPARE(buffer.valueFromOldest(1, 0), 10.0F);
    QCOMPARE(buffer.valueFromOldest(0, 1), 2.0F);
    QCOMPARE(buffer.valueFromOldest(1, 1), 20.0F);
}

void RingBufferTest::keepsNewestFramesAfterWrap()
{
    SampleRingBuffer buffer;
    buffer.configure(2, 3);

    DataBlock first;
    first.channelCount = 2;
    first.frameCount = 2;
    first.sampleRateHz = 1000.0;
    first.interleaved = {1.0F, 10.0F, 2.0F, 20.0F};
    QVERIFY(buffer.append(first));

    DataBlock second;
    second.firstSampleIndex = 2;
    second.channelCount = 2;
    second.frameCount = 2;
    second.sampleRateHz = 1000.0;
    second.interleaved = {3.0F, 30.0F, 4.0F, 40.0F};
    QVERIFY(buffer.append(second));

    QCOMPARE(buffer.sizeFrames(), 3);
    QCOMPARE(buffer.valueFromOldest(0, 0), 2.0F);
    QCOMPARE(buffer.valueFromOldest(0, 1), 3.0F);
    QCOMPARE(buffer.valueFromOldest(0, 2), 4.0F);
    QCOMPARE(buffer.valueFromOldest(1, 2), 40.0F);
}

void RingBufferTest::tracksAbsoluteSampleIndicesAndSnapshots()
{
    SampleRingBuffer buffer;
    buffer.configure(2, 3);

    DataBlock block;
    block.firstSampleIndex = 100;
    block.channelCount = 2;
    block.frameCount = 4;
    block.sampleRateHz = 200000.0;
    block.interleaved = {
        1.0F, 10.0F,
        2.0F, 20.0F,
        3.0F, 30.0F,
        4.0F, 40.0F
    };

    QVERIFY(buffer.append(block));
    QCOMPARE(buffer.oldestSampleIndex(), 101);
    QCOMPARE(buffer.newestSampleIndexExclusive(), 104);
    QCOMPARE(buffer.valueAtSample(0, 102), 3.0F);

    const DataBlock snapshot = buffer.snapshot(102, 8, 200000.0);
    QVERIFY(snapshot.isValid());
    QCOMPARE(snapshot.firstSampleIndex, 102);
    QCOMPARE(snapshot.frameCount, 2);
    QCOMPARE(snapshot.interleaved,
             QVector<float>({3.0F, 30.0F, 4.0F, 40.0F}));
}

void RingBufferTest::acceleratesRangeQueriesWithEnvelope()
{
    SampleRingBuffer buffer;
    buffer.configure(1, 1024);

    DataBlock block;
    block.channelCount = 1;
    block.frameCount = 768;
    block.sampleRateHz = 1000.0;
    block.interleaved.resize(block.frameCount);
    for (int index = 0; index < block.frameCount; ++index) {
        block.interleaved[index] = static_cast<float>(index % 37) - 18.0F;
    }
    QVERIFY(buffer.append(block));

    const SampleRingBuffer::Range range = buffer.rangeForSamples(0, 17, 500);
    QVERIFY(range.valid);
    QCOMPARE(range.minimum, -18.0F);
    QCOMPARE(range.maximum, 18.0F);

    const SampleRingBuffer::Range tail = buffer.rangeForSamples(0, 700, 100);
    QVERIFY(tail.valid);
    QCOMPARE(tail.minimum, -18.0F);
    QCOMPARE(tail.maximum, 18.0F);
}

QTEST_APPLESS_MAIN(RingBufferTest)

#include "test_ringbuffer.moc"
