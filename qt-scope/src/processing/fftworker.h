#pragma once

#include "processing/fftengine.h"

#include <QObject>

class FftWorker final : public QObject
{
    Q_OBJECT

public:
    explicit FftWorker(QObject *parent = nullptr);

public slots:
    void analyze(const DataBlock &block, int channel, const FftConfig &config);

signals:
    void resultReady(const SpectrumResult &result);

private:
    FftEngine m_engine;
};
