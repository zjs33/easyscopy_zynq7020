#pragma once

#include "acquisition/datablock.h"

#include <QObject>

class IDataSource : public QObject
{
    Q_OBJECT

public:
    explicit IDataSource(QObject *parent = nullptr);
    ~IDataSource() override;

public slots:
    virtual void start() = 0;
    virtual void stop() = 0;

signals:
    void streamInfoChanged(const DataStreamInfo &info);
    void blockReady(const DataBlockPtr &block);
    void runningChanged(bool running);
    void errorOccurred(const QString &message);
};
