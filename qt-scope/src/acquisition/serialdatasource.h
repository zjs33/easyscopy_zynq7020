#pragma once

#include "acquisition/idatasource.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QStringList>
#include <QVector>

#include <memory>
#include <string>

class QTimer;
class QTcpServer;
class QTcpSocket;
#ifdef ZYNQ_SCOPE_HAS_CH347
class Ch347SpiHost;
class ScopeSpiClient;
struct ScopeSpiStatus;
#endif

class SerialDataSource final : public IDataSource
{
    Q_OBJECT

public:
    enum class TransportMode { Serial = 0, TcpServer = 1, Ch347Spi = 2 };

    explicit SerialDataSource(const QString &portName = QString(),
                              QObject *parent = nullptr);
    ~SerialDataSource() override;

    [[nodiscard]] QString portName() const;
    [[nodiscard]] TransportMode transportMode() const { return m_transportMode; }
    [[nodiscard]] QString tcpListenAddress() const { return m_tcpListenAddress; }
    [[nodiscard]] quint16 tcpListenPort() const { return m_tcpListenPort; }
    [[nodiscard]] QString ch347DllPath() const { return m_ch347DllPath; }
    [[nodiscard]] int ch347DeviceIndex() const { return m_ch347DeviceIndex; }
    [[nodiscard]] int ch347ClockIndex() const { return m_ch347ClockIndex; }
    [[nodiscard]] bool highSpeedMode() const { return m_highSpeedMode; }
    [[nodiscard]] static QStringList availablePortNames();

public slots:
    void setPortName(const QString &portName);
    void setTransportMode(int mode);
    void setTcpListenAddress(const QString &address);
    void setTcpListenPort(int port);
    void setCh347DllPath(const QString &path);
    void setCh347DeviceIndex(int index);
    void setCh347ClockIndex(int index);
    void setHighSpeedMode(bool enabled);
    void start() override;
    void stop() override;
    void requestPing();
    void requestInfo();
    void setStreaming(bool enabled);
    void configureGenerator(const QString &waveform,
                            int frequencyMilliHz,
                            int amplitudeCode,
                            int offsetCode);
    void setBoardFftEnabled(bool enabled);
    void uploadWaveform(const QByteArray &codes,
                        int frequencyMilliHz,
                        quint16 crc16);

signals:
    void linkStateChanged(bool connected,
                          const QString &portName,
                          const QString &message);
    void protocolLineReceived(const QString &line, bool outgoing);
    void pingResult(quint32 sequence,
                    double roundTripMilliseconds,
                    quint32 deviceMilliseconds);
    void statisticsChanged(quint64 receivedBytes,
                           quint64 transmittedBytes,
                           quint64 receivedSamples);
    void boardFftResult(quint32 sequence,
                        float peakFrequencyHz,
                        float peakAmplitudeVolts,
                        quint32 computeMicroseconds);
    void waveformUploadProgress(int sentBytes, int totalBytes);
    void waveformUploadFinished(bool success, const QString &message);

private slots:
    void pollSerialPort();
    void handleUploadTimeout();
    void handleTcpNewConnection();
    void handleTcpReadyRead();
    void handleTcpDisconnected();
    void handleTcpError();

private:
    [[nodiscard]] DataStreamInfo streamInfo() const;
    [[nodiscard]] bool openTransport(QString *errorMessage);
    [[nodiscard]] bool transportReady() const;
    [[nodiscard]] QString endpointName() const;
    void initializeSession();
#ifdef ZYNQ_SCOPE_HAS_CH347
    [[nodiscard]] bool openCh347(QString *errorMessage);
    void initializeCh347Session();
    void pollCh347();
    void pollCh347HighSpeed();
    void logCh347Error(const QString &operation, const std::string &error);
#endif
    void closeTransport();
    void writeCommand(const QByteArray &command);
    void processIncoming(const QByteArray &bytes);
    void processLine(const QByteArray &line);
    void emitSampleBlockIfReady();
    void sendNextUploadCommand();
    void finishUpload(bool success, const QString &message, bool sendAbort);

    enum class UploadStage { Idle, Begin, Data, Commit };

    QString m_portName;
    TransportMode m_transportMode = TransportMode::Serial;
    QString m_tcpListenAddress = QStringLiteral("0.0.0.0");
    quint16 m_tcpListenPort = 8086;
    QString m_ch347DllPath;
    int m_ch347DeviceIndex = 0;
    int m_ch347ClockIndex = 3; /* 7.5 MHz: fastest stable slave rate (default) */
    bool m_highSpeedMode = false;
    QByteArray m_receiveBuffer;
    QVector<float> m_pendingSamples;
    QHash<quint32, qint64> m_pendingPings;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_uploadTimer = nullptr;
    QElapsedTimer m_clock;
    void *m_handle = nullptr;
    QTcpServer *m_tcpServer = nullptr;
    QTcpSocket *m_tcpSocket = nullptr;
    qint64 m_nextSampleIndex = 0;
    quint32 m_nextPingSequence = 1;
    quint64 m_receivedBytes = 0;
    quint64 m_transmittedBytes = 0;
    quint64 m_receivedSamples = 0;
    QString m_generatorWaveform = QStringLiteral("SIN");
    int m_generatorFrequencyMilliHz = 1000000; /* 1 kHz sine by default */
    int m_generatorAmplitudeCode = 100;
    int m_generatorOffsetCode = 128;
    bool m_running = false;
    bool m_protocolReady = false;
    bool m_streamingRequested = true;
    bool m_boardFftEnabled = true;
    UploadStage m_uploadStage = UploadStage::Idle;
    QByteArray m_uploadCodes;
    int m_uploadOffset = 0;
    int m_uploadFrequencyMilliHz = 1000;
    quint16 m_uploadCrc16 = 0;
#ifdef ZYNQ_SCOPE_HAS_CH347
    std::unique_ptr<Ch347SpiHost> m_ch347Host;
    std::unique_ptr<ScopeSpiClient> m_ch347Client;
    quint32 m_ch347DeviceMilliseconds = 0;
    quint32 m_ch347DroppedSamples = 0;
    quint16 m_ch347SampleRateHz = 100;
    double m_ch347HsSampleRateHz = 800000.0;
    int m_ch347ConsecutiveErrors = 0;
    qint64 m_lastCh347StatusNs = 0;
#endif
};
