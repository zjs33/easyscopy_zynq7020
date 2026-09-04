#include "plotting/sampleringbuffer.h"

#include <algorithm>
#include <cmath>
#include <limits>

void SampleRingBuffer::configure(int channelCount, int capacityFrames)
{
    m_channelCount = std::max(0, channelCount);
    m_capacityFrames = std::max(0, capacityFrames);
    m_channels.clear();
    m_channels.resize(m_channelCount);
    for (auto &channel : m_channels) {
        channel.resize(m_capacityFrames);
    }

    m_envelopeBucketCapacity = std::max(
        1, (m_capacityFrames + EnvelopeBucketFrames - 1)
            / EnvelopeBucketFrames + 2);
    m_envelopeBucketStarts.fill(-1, m_envelopeBucketCapacity);
    m_envelopeMinimums.resize(m_channelCount);
    m_envelopeMaximums.resize(m_channelCount);
    for (int channel = 0; channel < m_channelCount; ++channel) {
        m_envelopeMinimums[channel].fill(0.0F, m_envelopeBucketCapacity);
        m_envelopeMaximums[channel].fill(0.0F, m_envelopeBucketCapacity);
    }
    clear();
}

void SampleRingBuffer::clear()
{
    m_sizeFrames = 0;
    m_writeIndex = 0;
    m_totalFramesReceived = 0;
    m_oldestSampleIndex = 0;
    m_newestSampleIndexExclusive = 0;
    m_envelopeBucketStarts.fill(-1);
}

bool SampleRingBuffer::append(const DataBlock &block)
{
    if (!block.isValid()
        || block.channelCount != m_channelCount
        || m_capacityFrames <= 0) {
        return false;
    }

    if (m_sizeFrames > 0
        && block.firstSampleIndex != m_newestSampleIndexExclusive) {
        clear();
    }

    const int firstFrame = std::max(0, block.frameCount - m_capacityFrames);
    const int framesToAppend = block.frameCount - firstFrame;
    if (m_channelCount == 1) {
        const float *source = block.interleaved.constData() + firstFrame;
        float *destination = m_channels[0].data();
        const int firstPart = std::min(framesToAppend, m_capacityFrames - m_writeIndex);
        std::copy_n(source, firstPart, destination + m_writeIndex);
        if (firstPart < framesToAppend) {
            std::copy_n(source + firstPart, framesToAppend - firstPart, destination);
        }
        m_writeIndex = (m_writeIndex + framesToAppend) % m_capacityFrames;
        m_sizeFrames = std::min(m_sizeFrames + framesToAppend, m_capacityFrames);
    } else {
        for (int frame = firstFrame; frame < block.frameCount; ++frame) {
            const int sourceBase = frame * m_channelCount;
            for (int channel = 0; channel < m_channelCount; ++channel) {
                m_channels[channel][m_writeIndex] = block.interleaved[sourceBase + channel];
            }
            m_writeIndex = (m_writeIndex + 1) % m_capacityFrames;
            m_sizeFrames = std::min(m_sizeFrames + 1, m_capacityFrames);
        }
    }

    updateEnvelope(block, firstFrame);
    m_totalFramesReceived += block.frameCount;
    m_newestSampleIndexExclusive = block.firstSampleIndex + block.frameCount;
    m_oldestSampleIndex = m_newestSampleIndexExclusive - m_sizeFrames;
    return true;
}

void SampleRingBuffer::updateEnvelope(const DataBlock &block, int firstFrame)
{
    if (m_envelopeBucketCapacity <= 0 || firstFrame >= block.frameCount) {
        return;
    }

    int frame = firstFrame;
    while (frame < block.frameCount) {
        const qint64 sampleIndex = block.firstSampleIndex + frame;
        const qint64 bucketStart = sampleIndex
            - (sampleIndex % EnvelopeBucketFrames);
        const int offsetInBucket = static_cast<int>(sampleIndex - bucketStart);
        const int framesInBucket = std::min(
            block.frameCount - frame, EnvelopeBucketFrames - offsetInBucket);
        const int bucket = static_cast<int>(
            (bucketStart / EnvelopeBucketFrames) % m_envelopeBucketCapacity);

        if (m_envelopeBucketStarts[bucket] != bucketStart) {
            m_envelopeBucketStarts[bucket] = bucketStart;
            for (int channel = 0; channel < m_channelCount; ++channel) {
                m_envelopeMinimums[channel][bucket] =
                    std::numeric_limits<float>::max();
                m_envelopeMaximums[channel][bucket] =
                    std::numeric_limits<float>::lowest();
            }
        }

        for (int localFrame = 0; localFrame < framesInBucket; ++localFrame) {
            const int sourceBase = (frame + localFrame) * m_channelCount;
            for (int channel = 0; channel < m_channelCount; ++channel) {
                const float value = block.interleaved[sourceBase + channel];
                if (!std::isfinite(value)) {
                    continue;
                }
                m_envelopeMinimums[channel][bucket] = std::min(
                    m_envelopeMinimums[channel][bucket], value);
                m_envelopeMaximums[channel][bucket] = std::max(
                    m_envelopeMaximums[channel][bucket], value);
            }
        }
        frame += framesInBucket;
    }
}

float SampleRingBuffer::valueFromOldest(int channel, int frameOffset) const
{
    if (channel < 0 || channel >= m_channelCount
        || frameOffset < 0 || frameOffset >= m_sizeFrames
        || m_capacityFrames <= 0) {
        return 0.0F;
    }

    const int oldestIndex = (m_writeIndex - m_sizeFrames + m_capacityFrames)
        % m_capacityFrames;
    const int index = (oldestIndex + frameOffset) % m_capacityFrames;
    return m_channels[channel][index];
}

float SampleRingBuffer::valueAtSample(int channel, qint64 sampleIndex) const
{
    if (sampleIndex < m_oldestSampleIndex
        || sampleIndex >= m_newestSampleIndexExclusive) {
        return 0.0F;
    }
    return valueFromOldest(channel,
                           static_cast<int>(sampleIndex - m_oldestSampleIndex));
}

SampleRingBuffer::Range SampleRingBuffer::rangeForSamples(
    int channel, qint64 firstSampleIndex, int frameCount) const
{
    Range result;
    if (channel < 0 || channel >= m_channelCount || frameCount <= 0
        || firstSampleIndex < m_oldestSampleIndex
        || firstSampleIndex >= m_newestSampleIndexExclusive) {
        return result;
    }

    const qint64 endSampleIndex = std::min(
        firstSampleIndex + static_cast<qint64>(frameCount),
        m_newestSampleIndexExclusive);
    const qint64 bucketSize = EnvelopeBucketFrames;
    auto include = [&result](float value) {
        if (!std::isfinite(value)) {
            return;
        }
        result.minimum = std::min(result.minimum, value);
        result.maximum = std::max(result.maximum, value);
        result.valid = true;
    };
    result.minimum = std::numeric_limits<float>::max();
    result.maximum = std::numeric_limits<float>::lowest();

    qint64 sampleIndex = firstSampleIndex;
    while (sampleIndex < endSampleIndex
           && sampleIndex % bucketSize != 0) {
        include(valueAtSample(channel, sampleIndex));
        ++sampleIndex;
    }

    while (sampleIndex + bucketSize <= endSampleIndex) {
        const int bucket = static_cast<int>(
            (sampleIndex / bucketSize) % m_envelopeBucketCapacity);
        if (m_envelopeBucketStarts[bucket] == sampleIndex) {
            include(m_envelopeMinimums[channel][bucket]);
            include(m_envelopeMaximums[channel][bucket]);
            sampleIndex += bucketSize;
        } else {
            for (qint64 i = 0; i < bucketSize; ++i) {
                include(valueAtSample(channel, sampleIndex + i));
            }
            sampleIndex += bucketSize;
        }
    }

    while (sampleIndex < endSampleIndex) {
        include(valueAtSample(channel, sampleIndex));
        ++sampleIndex;
    }
    return result;
}

DataBlock SampleRingBuffer::snapshot(qint64 firstSampleIndex,
                                     int frameCount,
                                     double sampleRateHz) const
{
    DataBlock result;
    if (m_sizeFrames <= 0 || frameCount <= 0 || sampleRateHz <= 0.0) {
        return result;
    }

    const qint64 first = std::max(firstSampleIndex, m_oldestSampleIndex);
    const qint64 requestedEnd = firstSampleIndex + frameCount;
    const qint64 end = std::min(requestedEnd, m_newestSampleIndexExclusive);
    if (end <= first) {
        return result;
    }

    result.firstSampleIndex = first;
    result.channelCount = m_channelCount;
    result.frameCount = static_cast<int>(end - first);
    result.sampleRateHz = sampleRateHz;
    result.interleaved.resize(result.channelCount * result.frameCount);
    for (int frame = 0; frame < result.frameCount; ++frame) {
        for (int channel = 0; channel < result.channelCount; ++channel) {
            result.interleaved[frame * result.channelCount + channel]
                = valueAtSample(channel, first + frame);
        }
    }
    return result;
}
