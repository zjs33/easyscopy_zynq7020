#pragma once

#include <QByteArray>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class SerialDataSource;
class DacFunctionPreview;

class BoardLinkWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit BoardLinkWidget(SerialDataSource *dataSource,
                             QWidget *parent = nullptr);

signals:
    void portSelected(const QString &portName);
    void transportModeSelected(int mode);
    void tcpListenAddressSelected(const QString &address);
    void tcpListenPortSelected(int port);
    void ch347DllPathSelected(const QString &path);
    void ch347DeviceIndexSelected(int index);
    void ch347ClockIndexSelected(int index);
    void startRequested();
    void stopRequested();
    void pingRequested();
    void infoRequested();
    void streamingRequested(bool enabled);
    void highSpeedModeRequested(bool enabled);
    void generatorRequested(const QString &waveform,
                            int frequencyMilliHz,
                            int amplitudeCode,
                            int offsetCode);
    void customWaveformRequested(const QByteArray &codes,
                                 int frequencyMilliHz,
                                 quint16 crc16);

private slots:
    void refreshPorts();
    void updateTransportControls();
    void toggleConnection();
    void handleLinkState(bool connected,
                         const QString &portName,
                         const QString &message);
    void appendProtocolLine(const QString &line, bool outgoing);
    void handlePingResult(quint32 sequence,
                          double roundTripMilliseconds,
                          quint32 deviceMilliseconds);
    void handleStatistics(quint64 receivedBytes,
                          quint64 transmittedBytes,
                          quint64 receivedSamples);
    void applyGeneratorSettings();
    void updateGeneratorControls();

private:
    QComboBox *m_portCombo = nullptr;
    QComboBox *m_transportCombo = nullptr;
    QLineEdit *m_tcpAddressEdit = nullptr;
    QSpinBox *m_tcpPortSpin = nullptr;
    QWidget *m_ch347Panel = nullptr;
    QLineEdit *m_ch347DllEdit = nullptr;
    QSpinBox *m_ch347DeviceSpin = nullptr;
    QComboBox *m_ch347ClockCombo = nullptr;
    QPushButton *m_ch347BrowseButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_connectButton = nullptr;
    QPushButton *m_pingButton = nullptr;
    QPushButton *m_infoButton = nullptr;
    QCheckBox *m_streamCheck = nullptr;
    QCheckBox *m_highSpeedCheck = nullptr;
    QCheckBox *m_autoPingCheck = nullptr;
    QLabel *m_transportHint = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_pingLabel = nullptr;
    QLabel *m_statisticsLabel = nullptr;
    QComboBox *m_waveformCombo = nullptr;
    QDoubleSpinBox *m_frequencySpin = nullptr;
    QDoubleSpinBox *m_amplitudeSpin = nullptr;
    QDoubleSpinBox *m_offsetSpin = nullptr;
    QPushButton *m_applyGeneratorButton = nullptr;
    QLabel *m_generatorLabel = nullptr;
    QLineEdit *m_formulaEdit = nullptr;
    DacFunctionPreview *m_functionPreview = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QTimer *m_autoPingTimer = nullptr;
    bool m_connected = false;
};
