#include "processing/fftworker.h"

FftWorker::FftWorker(QObject *parent)
    : QObject(parent)
{
}

void FftWorker::analyze(const DataBlock &block, int channel, const FftConfig &config)
{
    emit resultReady(m_engine.analyze(block, channel, config));
}
