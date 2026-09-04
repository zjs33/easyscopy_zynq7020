#pragma once

#include <QMetaType>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QVector>

struct DataStreamInfo
{
    QString sourceName;
    int channelCount = 0;
    double sampleRateHz = 0.0;
    QStringList channelNames;
    QString unit = QStringLiteral("V");

    [[nodiscard]] bool isValid() const
    {
        return channelCount > 0 && sampleRateHz > 0.0;
    }
};

struct DataBlock
{
    qint64 firstSampleIndex = 0;
    qint64 monotonicTimestampNs = 0;
    int channelCount = 0;
    int frameCount = 0;
    double sampleRateHz = 0.0;
    QVector<float> interleaved;

    [[nodiscard]] bool isValid() const
    {
        return channelCount > 0
            && frameCount > 0
            && sampleRateHz > 0.0
            && interleaved.size() == channelCount * frameCount;
    }
};

using DataBlockPtr = QSharedPointer<const DataBlock>;

Q_DECLARE_METATYPE(DataStreamInfo)
Q_DECLARE_METATYPE(DataBlock)
Q_DECLARE_METATYPE(DataBlockPtr)
