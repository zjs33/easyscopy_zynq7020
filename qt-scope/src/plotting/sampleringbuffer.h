#pragma once

#include "acquisition/datablock.h"

#include <QVector>

class SampleRingBuffer
{
public:
    struct Range
    {
        float minimum = 0.0F;
        float maximum = 0.0F;
        bool valid = false;
    };

    void configure(int channelCount, int capacityFrames);
    void clear();
    [[nodiscard]] bool append(const DataBlock &block);

    [[nodiscard]] int channelCount() const { return m_channelCount; }
    [[nodiscard]] int capacityFrames() const { return m_capacityFrames; }
    [[nodiscard]] int sizeFrames() const { return m_sizeFrames; }
    [[nodiscard]] qint64 totalFramesReceived() const { return m_totalFramesReceived; }
    [[nodiscard]] qint64 oldestSampleIndex() const { return m_oldestSampleIndex; }
    [[nodiscard]] qint64 newestSampleIndexExclusive() const { return m_newestSampleIndexExclusive; }
    [[nodiscard]] float valueFromOldest(int channel, int frameOffset) const;
    [[nodiscard]] float valueAtSample(int channel, qint64 sampleIndex) const;
    [[nodiscard]] Range rangeForSamples(int channel,
                                         qint64 firstSampleIndex,
                                         int frameCount) const;
    [[nodiscard]] DataBlock snapshot(qint64 firstSampleIndex,
                                     int frameCount,
                                     double sampleRateHz) const;

private:
    enum { EnvelopeBucketFrames = 256 };

    void updateEnvelope(const DataBlock &block, int firstFrame);

    int m_channelCount = 0;
    int m_capacityFrames = 0;
    int m_sizeFrames = 0;
    int m_writeIndex = 0;
    qint64 m_totalFramesReceived = 0;
    qint64 m_oldestSampleIndex = 0;
    qint64 m_newestSampleIndexExclusive = 0;
    QVector<QVector<float>> m_channels;
    int m_envelopeBucketCapacity = 0;
    QVector<qint64> m_envelopeBucketStarts;
    QVector<QVector<float>> m_envelopeMinimums;
    QVector<QVector<float>> m_envelopeMaximums;
};
