#pragma once

#include "acquisition/idatasource.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>

class DmaCaptureDataSource final : public IDataSource
{
    Q_OBJECT

public:
    explicit DmaCaptureDataSource(QObject *parent = nullptr);
    ~DmaCaptureDataSource() override;

    void setPath(const QString &path);
    void setSampleRate(double sampleRateHz);
    void setSegmentBytes(int bytes);
    void setZeroCode(float zeroCode);
    void setVoltsPerCode(float voltsPerCode);

    [[nodiscard]] QString path() const { return m_path; }
    [[nodiscard]] int segmentBytes() const { return m_segmentBytes; }

public slots:
    void start() override;
    void stop() override;

private slots:
    void readNextSegment();

private:
    [[nodiscard]] DataStreamInfo streamInfo() const;
    bool openInput();
    void closeInput();
    bool readFileSegment();
    bool readDeviceSegment();
    void emitSegment();

    QString m_path;
    double m_sampleRateHz = 50000000.0;
    int m_segmentBytes = 1024 * 1024;
    float m_zeroCode = 128.0F;
    float m_voltsPerCode = 1.0F / 128.0F;
    QFile m_file;
    QByteArray m_rawSegment;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;
    qint64 m_nextSampleIndex = 0;
    quint64 m_segmentSequence = 0;
    bool m_running = false;
    bool m_deviceInput = false;
    int m_deviceFd = -1;
};
