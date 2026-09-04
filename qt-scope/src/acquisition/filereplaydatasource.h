#pragma once

#include "acquisition/idatasource.h"

#include <QTimer>

class FileReplayDataSource final : public IDataSource
{
    Q_OBJECT

public:
    explicit FileReplayDataSource(QObject *parent = nullptr);

public slots:
    void loadData(const DataBlock &block, const QStringList &channelNames);
    void clearData();
    void setPlaybackSpeed(double speed);
    void setLooping(bool looping);
    void rewind();
    void start() override;
    void stop() override;

signals:
    void playbackFinished();

private slots:
    void emitNextBlock();

private:
    [[nodiscard]] DataStreamInfo streamInfo() const;

    DataBlock m_data;
    QStringList m_channelNames;
    int m_position = 0;
    int m_blockDurationMs = 10;
    double m_speed = 1.0;
    bool m_looping = false;
    bool m_running = false;
    QTimer *m_timer = nullptr;
};
