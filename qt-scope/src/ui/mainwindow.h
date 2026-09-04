#pragma once

#include "acquisition/datablock.h"
#include "processing/triggerengine.h"
#include "processing/measurementengine.h"
#include "processing/mathengine.h"
#include "processing/fftengine.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QThread>
#include <QVector>

class IDataSource;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QTimer;
class QVBoxLayout;
class FftWorker;
class FileWorker;
class FileReplayDataSource;
class SerialDataSource;
class SpectrumWidget;
class WaveformWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(IDataSource *liveDataSource,
                        FileReplayDataSource *replayDataSource,
                        SerialDataSource *serialDataSource = nullptr,
                        QWidget *parent = nullptr);
    ~MainWindow() override;

signals:
    void fftAnalysisRequested(const DataBlock &block,
                              int channel,
                              const FftConfig &config);
    void exportTextRequested(const QString &path,
                             const DataBlock &block,
                             const QStringList &channelNames,
                             QChar delimiter);
    void importTextRequested(const QString &path, int maximumFrames);

private slots:
    void requestRunning(bool running);
    void initializeInstrument();
    void handleStreamInfo(const DataStreamInfo &info);
    void handleBlock(const DataBlockPtr &block);
    void handleRunningChanged(bool running);
    void handleError(const QString &message);
    void updateStatus();
    void applyTriggerConfiguration();
    void armSingleTrigger();
    void updateMeasurements();
    void requestFftAnalysis();
    void handleFftResult(const SpectrumResult &result);
    void captureScreenshot();
    void exportCsv();
    void exportTxt();
    void importReplayFile();
    void returnToLiveSource();
    void handleExportFinished(const QString &path,
                              bool success,
                              const QString &message);
    void handleImportFinished(const DataBlock &block,
                              const QStringList &channelNames,
                              bool success,
                              const QString &message);

private:
    void buildUi();
    void rebuildChannelControls(const DataStreamInfo &info);
    [[nodiscard]] QWidget *buildTriggerPage();
    [[nodiscard]] QWidget *buildMeasurementPage();
    [[nodiscard]] QWidget *buildAnalysisPage();
    [[nodiscard]] QWidget *buildFilePage();
    void processTriggerBlock(const DataBlock &block);
    void connectDataSource(IDataSource *source);
    void exportTextFile(QChar delimiter, const QString &filter, const QString &suffix);
    [[nodiscard]] MathOperation currentMathOperation() const;
    [[nodiscard]] QString formatSampleRate(double sampleRateHz) const;
    [[nodiscard]] QString formatTimeValue(double seconds) const;
    [[nodiscard]] QString formatVoltageValue(double volts) const;

    IDataSource *m_dataSource;
    IDataSource *m_liveDataSource;
    FileReplayDataSource *m_replayDataSource;
    SerialDataSource *m_serialDataSource;
    WaveformWidget *m_waveform = nullptr;
    SpectrumWidget *m_spectrum = nullptr;
    QTabWidget *m_viewTabs = nullptr;
    QPushButton *m_runButton = nullptr;
    QPushButton *m_liveButton = nullptr;
    QComboBox *m_timePerDivisionCombo = nullptr;
    QComboBox *m_voltsPerDivisionCombo = nullptr;
    QComboBox *m_persistenceCombo = nullptr;
    QTabWidget *m_controlTabs = nullptr;
    QCheckBox *m_triggerEnabledCheck = nullptr;
    QComboBox *m_triggerTypeCombo = nullptr;
    QComboBox *m_triggerModeCombo = nullptr;
    QComboBox *m_triggerSourceCombo = nullptr;
    QComboBox *m_triggerSlopeCombo = nullptr;
    QComboBox *m_pulsePolarityCombo = nullptr;
    QComboBox *m_pulseConditionCombo = nullptr;
    QComboBox *m_videoStandardCombo = nullptr;
    QComboBox *m_videoSyncCombo = nullptr;
    QDoubleSpinBox *m_triggerLevelSpin = nullptr;
    QDoubleSpinBox *m_triggerHysteresisSpin = nullptr;
    QDoubleSpinBox *m_triggerHoldoffSpin = nullptr;
    QDoubleSpinBox *m_triggerPretriggerSpin = nullptr;
    QDoubleSpinBox *m_pulseMinimumSpin = nullptr;
    QDoubleSpinBox *m_pulseMaximumSpin = nullptr;
    QLabel *m_triggerStatusLabel = nullptr;
    QTableWidget *m_measurementTable = nullptr;
    QLabel *m_cursorReadoutLabel = nullptr;
    QCheckBox *m_mathEnabledCheck = nullptr;
    QCheckBox *m_cursorCheck = nullptr;
    QComboBox *m_mathOperationCombo = nullptr;
    QComboBox *m_fftSourceCombo = nullptr;
    QComboBox *m_fftPointsCombo = nullptr;
    QComboBox *m_fftWindowCombo = nullptr;
    QCheckBox *m_fftDecibelsCheck = nullptr;
    QLabel *m_fftStatusLabel = nullptr;
    QComboBox *m_replaySpeedCombo = nullptr;
    QCheckBox *m_replayLoopCheck = nullptr;
    QLabel *m_fileStatusLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QWidget *m_channelContainer = nullptr;
    QVBoxLayout *m_channelLayout = nullptr;
    QVector<QCheckBox *> m_channelChecks;
    QVector<QDoubleSpinBox *> m_channelScaleSpins;
    QVector<QDoubleSpinBox *> m_channelOffsetSpins;
    QVector<QCheckBox *> m_channelMarkerChecks;
    QVector<QDoubleSpinBox *> m_channelMarkerPositionSpins;
    QVector<QLabel *> m_channelMarkerReadoutLabels;
    QTimer *m_statusTimer = nullptr;
    QElapsedTimer m_statusClock;
    DataStreamInfo m_streamInfo;
    qint64 m_totalFrames = 0;
    qint64 m_lastStatusFrames = 0;
    double m_renderFps = 0.0;
    int m_measurementMaximumFrames = 200000;
    bool m_sourceRunning = false;
    TriggerEngine m_triggerEngine;
    MeasurementEngine m_measurementEngine;
    MathEngine m_mathEngine;
    qint64 m_pendingTriggerSample = -1;
    QThread m_fftThread;
    FftWorker *m_fftWorker = nullptr;
    bool m_fftBusy = false;
    bool m_discardPendingFftResult = false;
    bool m_boardFftValid = false;
    double m_boardPeakFrequencyHz = 0.0;
    double m_boardPeakAmplitudeVolts = 0.0;
    quint32 m_boardFftComputeMicroseconds = 0;
    QThread m_fileThread;
    FileWorker *m_fileWorker = nullptr;
    bool m_fileBusy = false;
    bool m_discardPendingFileResult = false;
};
