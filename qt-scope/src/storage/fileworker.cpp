#include "storage/fileworker.h"

void FileWorker::exportText(const QString &path,
                            const DataBlock &block,
                            const QStringList &channelNames,
                            QChar delimiter)
{
    QString error;
    const bool success = WaveformFileCodec::exportText(
        path, block, channelNames, delimiter, &error);
    emit exportFinished(path, success,
                        success ? QStringLiteral("波形导出完成") : error);
}

void FileWorker::importText(const QString &path, int maximumFrames)
{
    const WaveformFileResult result = WaveformFileCodec::importText(path, maximumFrames);
    emit importFinished(result.block, result.channelNames,
                        result.success, result.message);
}
