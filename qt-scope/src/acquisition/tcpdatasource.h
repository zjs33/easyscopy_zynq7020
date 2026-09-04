#pragma once

#include "acquisition/idatasource.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>

class QTcpSocket;

class TcpDataSource final : public IDataSource
{
    Q_OBJECT

public:
    explicit TcpDataSource(const QString &host = QStringLiteral("192.168.137.1"),
                           quint16 port = 8086,
                           QObject *parent = nullptr);
    ~TcpDataSource() override;

    QString host() const { return m_host; }
    quint16 port() const { return m_port; }

public slots:
    void start() override;
    void stop() override;
    void requestPing();
    void requestInfo();
    void setStreaming(bool enabled);
    void setBoardFftEnabled(bool enabled);

signals:
    void linkStateChanged(bool connected, const QString &endpoint, const QString &message);
    void protocolLineReceived(const QString &line, bool outgoing);
    void pingResult(quint32 sequence, double roundTripMilliseconds, quint32 deviceMilliseconds);
    void statisticsChanged(quint64 receivedBytes, quint64 transmittedBytes,
                           quint64 receivedSamples);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError();

private:
    DataStreamInfo streamInfo() const;
    void writeCommand(const QByteArray &command);
    void processIncoming(const QByteArray &bytes);
    void processLine(const QByteArray &line);
    void emitSampleBlockIfReady();

    QString m_host;
    quint16 m_port;
    QTcpSocket *m_socket = nullptr;
    QByteArray m_receiveBuffer;
    QVector<float> m_pendingSamples;
    QHash<quint32, qint64> m_pendingPings;
    QElapsedTimer m_clock;
    qint64 m_nextSampleIndex = 0;
    quint32 m_nextPingSequence = 1;
    quint64 m_receivedBytes = 0;
    quint64 m_transmittedBytes = 0;
    quint64 m_receivedSamples = 0;
    bool m_running = false;
    bool m_streamingRequested = true;
    bool m_boardFftEnabled = true;
};
