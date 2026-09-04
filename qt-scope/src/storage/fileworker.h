#pragma once

#include "storage/waveformfilecodec.h"

#include <QObject>

class FileWorker final : public QObject
{
    Q_OBJECT

public slots:
    void exportText(const QString &path,
                    const DataBlock &block,
                    const QStringList &channelNames,
                    QChar delimiter);
    void importText(const QString &path, int maximumFrames);

signals:
    void exportFinished(const QString &path, bool success, const QString &message);
    void importFinished(const DataBlock &block,
                        const QStringList &channelNames,
                        bool success,
                        const QString &message);
};
