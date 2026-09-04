#pragma once

#include "acquisition/idatasource.h"

#include <QElapsedTimer>
#include <QTimer>

class SimulationDataSource final : public IDataSource
{
    Q_OBJECT

public:
    explicit SimulationDataSource(double sampleRateHz = 200000.0,
                                  int channelCount = 8,
                                  int blockDurationMs = 10,
                                  QObject *parent = nullptr);

public slots:
    void start() override;
    void stop() override;

private slots:
    void generateBlock();

private:
    [[nodiscard]] float nextNoise();
    [[nodiscard]] float sampleForChannel(int channel, double timeSeconds);
    [[nodiscard]] DataStreamInfo streamInfo() const;

    double m_sampleRateHz;
    int m_channelCount;
    int m_blockDurationMs = 10;
    int m_framesPerBlock = 0;
    qint64 m_nextSampleIndex = 0;
    quint32 m_randomState = 0x12345678U;
    bool m_running = false;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;
};
