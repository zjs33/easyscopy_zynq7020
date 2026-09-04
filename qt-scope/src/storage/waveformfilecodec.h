#pragma once

#include "acquisition/datablock.h"

#include <QStringList>

struct WaveformFileResult
{
    bool success = false;
    QString message;
    DataBlock block;
    QStringList channelNames;
};

class WaveformFileCodec
{
public:
    [[nodiscard]] static bool exportText(const QString &path,
                                         const DataBlock &block,
                                         const QStringList &channelNames,
                                         QChar delimiter,
                                         QString *errorMessage);
    [[nodiscard]] static WaveformFileResult importText(const QString &path,
                                                       int maximumFrames);
};
