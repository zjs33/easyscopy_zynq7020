#include "storage/waveformfilecodec.h"

#include "core/compilercompat.h"

#include <QFile>
#include <QLocale>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

bool WaveformFileCodec::exportText(const QString &path,
                                   const DataBlock &block,
                                   const QStringList &channelNames,
                                   QChar delimiter,
                                   QString *errorMessage)
{
    if (!block.isValid()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("没有可导出的有效数据");
        }
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    QTextStream stream(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::Utf8);
#endif
    stream.setLocale(QLocale::c());
    stream << "# ZynqScope waveform\n";
    stream << "# sample_rate_hz=" << QString::number(block.sampleRateHz, 'g', 16) << '\n';
    stream << "# channel_count=" << block.channelCount << '\n';
    stream << "# first_sample_index=" << block.firstSampleIndex << '\n';
    stream << "sample_index" << delimiter << "time_s";
    for (int channel = 0; channel < block.channelCount; ++channel) {
        const QString name = channel < channelNames.size()
            ? channelNames[channel] : QStringLiteral("CH%1").arg(channel + 1);
        stream << delimiter << name;
    }
    stream << '\n';

    for (int frame = 0; frame < block.frameCount; ++frame) {
        const qint64 sampleIndex = block.firstSampleIndex + frame;
        stream << sampleIndex << delimiter
               << QString::number(static_cast<double>(frame) / block.sampleRateHz,
                                  'g', 16);
        for (int channel = 0; channel < block.channelCount; ++channel) {
            stream << delimiter
                   << QString::number(block.interleaved[frame * block.channelCount + channel],
                                      'g', 9);
        }
        stream << '\n';
        if (stream.status() != QTextStream::Ok) {
            file.cancelWriting();
            if (errorMessage) {
                *errorMessage = QStringLiteral("写入波形文件失败");
            }
            return false;
        }
    }

    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    return true;
}

WaveformFileResult WaveformFileCodec::importText(const QString &path,
                                                 int maximumFrames)
{
    WaveformFileResult result;
    maximumFrames = clampValue(maximumFrames, 1, 2000000);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.message = file.errorString();
        return result;
    }

    QTextStream stream(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::Utf8);
#endif
    stream.setLocale(QLocale::c());

    double sampleRate = 0.0;
    int declaredChannels = 0;
    qint64 declaredFirstSample = 0;
    QChar delimiter = path.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)
        ? QChar('\t') : QChar(',');
    bool headerRead = false;
    qint64 firstSample = 0;
    int frameCount = 0;

    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QLatin1Char('#'))) {
            const QString metadata = line.mid(1).trimmed();
            const int equals = metadata.indexOf(QLatin1Char('='));
            if (equals > 0) {
                const QString key = metadata.left(equals).trimmed();
                const QString value = metadata.mid(equals + 1).trimmed();
                if (key == QStringLiteral("sample_rate_hz")) {
                    sampleRate = value.toDouble();
                } else if (key == QStringLiteral("channel_count")) {
                    declaredChannels = value.toInt();
                } else if (key == QStringLiteral("first_sample_index")) {
                    declaredFirstSample = value.toLongLong();
                }
            }
            continue;
        }

        if (!headerRead) {
            if (line.contains(QLatin1Char('\t'))) {
                delimiter = QChar('\t');
            } else if (line.contains(QLatin1Char(','))) {
                delimiter = QChar(',');
            }
            const QStringList header = line.split(delimiter);
            if (header.size() < 3) {
                result.message = QStringLiteral("文件表头至少需要 sample_index、time_s 和一个通道");
                return result;
            }
            for (int i = 2; i < header.size(); ++i) {
                result.channelNames.append(header[i].trimmed());
            }
            if (declaredChannels <= 0) {
                declaredChannels = result.channelNames.size();
            }
            if (declaredChannels != result.channelNames.size()) {
                result.message = QStringLiteral("表头通道数与元数据不一致");
                return result;
            }
            result.block.interleaved.reserve(
                std::min(maximumFrames, 200000) * declaredChannels);
            headerRead = true;
            continue;
        }

        if (frameCount >= maximumFrames) {
            result.message = QStringLiteral("文件超过 %1 组样本的导入上限").arg(maximumFrames);
            result.block = {};
            return result;
        }
        const QStringList fields = line.split(delimiter);
        if (fields.size() != declaredChannels + 2) {
            result.message = QStringLiteral("第 %1 行列数错误").arg(frameCount + 2);
            result.block = {};
            return result;
        }
        bool indexOk = false;
        const qint64 sampleIndex = fields[0].trimmed().toLongLong(&indexOk);
        if (!indexOk) {
            result.message = QStringLiteral("第 %1 行采样序号无效").arg(frameCount + 2);
            result.block = {};
            return result;
        }
        if (frameCount == 0) {
            firstSample = sampleIndex;
        } else if (sampleIndex != firstSample + frameCount) {
            result.message = QStringLiteral("采样序号不连续");
            result.block = {};
            return result;
        }
        for (int channel = 0; channel < declaredChannels; ++channel) {
            bool valueOk = false;
            const float value = fields[channel + 2].trimmed().toFloat(&valueOk);
            if (!valueOk) {
                result.message = QStringLiteral("第 %1 行包含无效数值").arg(frameCount + 2);
                result.block = {};
                return result;
            }
            result.block.interleaved.append(value);
        }
        ++frameCount;
    }

    if (!headerRead || frameCount == 0 || sampleRate <= 0.0 || declaredChannels <= 0) {
        result.message = QStringLiteral("文件缺少有效数据或采样率元数据");
        result.block = {};
        return result;
    }
    result.block.firstSampleIndex = firstSample;
    result.block.channelCount = declaredChannels;
    result.block.frameCount = frameCount;
    result.block.sampleRateHz = sampleRate;
    if (declaredFirstSample != 0 && declaredFirstSample != firstSample) {
        result.message = QStringLiteral("首采样序号与元数据不一致");
        result.block = {};
        return result;
    }
    result.success = result.block.isValid();
    result.message = result.success
        ? QStringLiteral("已导入 %1 组样本").arg(frameCount)
        : QStringLiteral("导入的数据结构无效");
    return result;
}
