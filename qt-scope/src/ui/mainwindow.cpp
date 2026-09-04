#include "ui/mainwindow.h"

#include "acquisition/idatasource.h"
#include "acquisition/filereplaydatasource.h"
#include "acquisition/serialdatasource.h"
#include "plotting/spectrumwidget.h"
#include "plotting/waveformwidget.h"
#include "processing/fftworker.h"
#include "storage/fileworker.h"
#include "ui/boardlinkwidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QScrollerProperties>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace {
int boundedEnvironmentInteger(const char *name, int fallback,
                              int minimum, int maximum)
{
    bool ok = false;
    const int requested = qEnvironmentVariableIntValue(name, &ok);
    return ok ? std::max(minimum, std::min(requested, maximum)) : fallback;
}
}

MainWindow::MainWindow(IDataSource *liveDataSource,
                       FileReplayDataSource *replayDataSource,
                       SerialDataSource *serialDataSource,
                       QWidget *parent)
    : QMainWindow(parent)
    , m_dataSource(liveDataSource)
    , m_liveDataSource(liveDataSource)
    , m_replayDataSource(replayDataSource)
    , m_serialDataSource(serialDataSource)
{
    buildUi();
    if (m_serialDataSource) {
        connect(m_serialDataSource, &SerialDataSource::boardFftResult,
                this, [this](quint32, float frequencyHz,
                             float amplitudeVolts, quint32 computeUs) {
            m_boardFftValid = true;
            m_boardPeakFrequencyHz = frequencyHz;
            m_boardPeakAmplitudeVolts = amplitudeVolts;
            m_boardFftComputeMicroseconds = computeUs;
        });
        m_timePerDivisionCombo->setCurrentIndex(6);
        m_voltsPerDivisionCombo->setCurrentIndex(7);
        m_waveform->setTimePerDivision(0.1);
        m_waveform->setVoltsPerDivision(2.0);
    }

    m_fftWorker = new FftWorker;
    m_fftWorker->moveToThread(&m_fftThread);
    connect(&m_fftThread, &QThread::finished,
            m_fftWorker, &QObject::deleteLater);
    connect(this, &MainWindow::fftAnalysisRequested,
            m_fftWorker, &FftWorker::analyze, Qt::QueuedConnection);
    connect(m_fftWorker, &FftWorker::resultReady,
            this, &MainWindow::handleFftResult, Qt::QueuedConnection);
    m_fftThread.setObjectName(QStringLiteral("FftWorkerThread"));
    m_fftThread.start();

    m_fileWorker = new FileWorker;
    m_fileWorker->moveToThread(&m_fileThread);
    connect(&m_fileThread, &QThread::finished,
            m_fileWorker, &QObject::deleteLater);
    connect(this, &MainWindow::exportTextRequested,
            m_fileWorker, &FileWorker::exportText, Qt::QueuedConnection);
    connect(this, &MainWindow::importTextRequested,
            m_fileWorker, &FileWorker::importText, Qt::QueuedConnection);
    connect(m_fileWorker, &FileWorker::exportFinished,
            this, &MainWindow::handleExportFinished, Qt::QueuedConnection);
    connect(m_fileWorker, &FileWorker::importFinished,
            this, &MainWindow::handleImportFinished, Qt::QueuedConnection);
    m_fileThread.setObjectName(QStringLiteral("FileWorkerThread"));
    m_fileThread.start();

    connectDataSource(m_liveDataSource);
    connectDataSource(m_replayDataSource);

    connect(m_waveform, &WaveformWidget::renderRateChanged,
            this, [this](double fps) { m_renderFps = fps; });
    connect(m_waveform, &WaveformWidget::inputRejected,
            this, &MainWindow::handleError);

    m_statusClock.start();
    m_statusTimer = new QTimer(this);
    m_measurementMaximumFrames = boundedEnvironmentInteger(
        "ZYNQ_SCOPE_MEASUREMENT_FRAMES", 200000, 2048, 200000);
    m_statusTimer->setInterval(boundedEnvironmentInteger(
        "ZYNQ_SCOPE_STATUS_MS", 500, 250, 5000));
    if (qEnvironmentVariableIntValue("ZYNQ_SCOPE_LOW_POWER") != 0) {
        m_statusTimer->setTimerType(Qt::CoarseTimer);
    }
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    m_statusTimer->start();

    QTimer::singleShot(0, this, [this] { m_runButton->setChecked(true); });
}

MainWindow::~MainWindow()
{
    m_fileThread.quit();
    m_fileThread.wait();
    m_fftThread.quit();
    m_fftThread.wait();
}

void MainWindow::requestRunning(bool running)
{
    const char *method = running ? "start" : "stop";
    if (!QMetaObject::invokeMethod(m_dataSource, method, Qt::QueuedConnection)) {
        handleError(QStringLiteral("无法调用数据源的 %1 方法").arg(QString::fromLatin1(method)));
    }
}

void MainWindow::initializeInstrument()
{
    // Keep background work bounded and discard stale results instead of waiting
    // in the GUI thread or adding another task to either worker queue.
    m_discardPendingFftResult = m_fftBusy;
    m_discardPendingFileResult = m_fileBusy;

    m_dataSource = m_liveDataSource;
    QMetaObject::invokeMethod(m_liveDataSource, "stop", Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_replayDataSource, "clearData", Qt::QueuedConnection);

    const int defaultTimeIndex = m_serialDataSource ? 6 : 4;
    m_timePerDivisionCombo->setCurrentIndex(defaultTimeIndex);
    m_voltsPerDivisionCombo->setCurrentIndex(m_serialDataSource ? 7 : 4);
    m_persistenceCombo->setCurrentIndex(0);
    m_waveform->setTimePerDivision(m_serialDataSource ? 0.1 : 0.01);
    m_waveform->setVoltsPerDivision(m_serialDataSource ? 2.0 : 0.25);
    m_waveform->setPersistenceMode(WaveformWidget::PersistenceMode::Off);

    for (int channel = 0; channel < m_channelChecks.size(); ++channel) {
        m_channelChecks[channel]->setChecked(true);
        m_waveform->setChannelVisible(channel, true);
        if (channel < m_channelScaleSpins.size()) {
            m_channelScaleSpins[channel]->setValue(1.0);
            m_waveform->setChannelScale(channel, 1.0);
        }
        if (channel < m_channelOffsetSpins.size()) {
            m_channelOffsetSpins[channel]->setValue(0.0);
            m_waveform->setChannelOffset(channel, 0.0);
        }
        if (channel < m_channelMarkerChecks.size()) {
            m_channelMarkerChecks[channel]->setChecked(true);
            m_waveform->setMeasurementMarkerEnabled(channel, true);
        }
        if (channel < m_channelMarkerPositionSpins.size()) {
            const double defaultPercent = m_channelChecks.size() == 1
                ? 50.0
                : 30.0 + 40.0 * channel
                    / std::max(1, static_cast<int>(m_channelChecks.size()) - 1);
            m_channelMarkerPositionSpins[channel]->setValue(defaultPercent);
            m_waveform->setMeasurementMarkerFraction(channel, defaultPercent / 100.0);
        }
    }

    m_triggerEnabledCheck->setChecked(false);
    m_triggerTypeCombo->setCurrentIndex(0);
    m_triggerModeCombo->setCurrentIndex(0);
    m_triggerSourceCombo->setCurrentIndex(0);
    m_triggerSlopeCombo->setCurrentIndex(0);
    m_triggerLevelSpin->setValue(0.0);
    m_triggerHysteresisSpin->setValue(0.02);
    m_triggerHoldoffSpin->setValue(1.0);
    m_triggerPretriggerSpin->setValue(50.0);
    m_pulsePolarityCombo->setCurrentIndex(0);
    m_pulseConditionCombo->setCurrentIndex(1);
    m_pulseMinimumSpin->setValue(5.0);
    m_pulseMaximumSpin->setValue(100.0);
    m_videoStandardCombo->setCurrentIndex(0);
    m_videoSyncCombo->setCurrentIndex(0);
    applyTriggerConfiguration();

    m_cursorCheck->setChecked(false);
    m_waveform->setCursorsVisible(false);
    m_mathEnabledCheck->setChecked(false);
    m_mathOperationCombo->setCurrentIndex(0);
    m_fftSourceCombo->setCurrentIndex(m_serialDataSource ? 1 : 0);
    m_fftPointsCombo->setCurrentIndex(
        m_serialDataSource ? m_fftPointsCombo->findData(256)
                           : m_fftPointsCombo->findData(4096));
    m_fftWindowCombo->setCurrentIndex(1);
    m_fftDecibelsCheck->setChecked(true);

    m_replaySpeedCombo->setCurrentIndex(2);
    m_replayLoopCheck->setChecked(false);
    m_controlTabs->setCurrentIndex(0);
    m_viewTabs->setCurrentIndex(0);

    m_pendingTriggerSample = -1;
    m_triggerEngine.reset();
    m_totalFrames = 0;
    m_lastStatusFrames = 0;
    m_statusClock.restart();
    m_waveform->clear();
    m_waveform->setMathVisible(false);
    m_waveform->returnToLive();
    m_spectrum->clear();
    for (int row = 0; row < m_measurementTable->rowCount(); ++row) {
        for (int column = 0; column < m_measurementTable->columnCount(); ++column) {
            m_measurementTable->item(row, column)->setText(QStringLiteral("--"));
        }
    }
    m_cursorReadoutLabel->setText(
        QStringLiteral("T1 --   T2 --\nΔt --   1/Δt --\nV1 --   V2 --   ΔV --"));
    m_fftStatusLabel->setText(QStringLiteral("FFT：等待足够样本"));
    m_fileStatusLabel->setText(
        QStringLiteral("已初始化；导入上限：200 万组样本；导出上限：100 万组"));

    {
        const QSignalBlocker blocker(m_runButton);
        m_runButton->setChecked(true);
        m_runButton->setText(QStringLiteral("暂停"));
    }
    QMetaObject::invokeMethod(m_liveDataSource, "start", Qt::QueuedConnection);
    statusBar()->showMessage(QStringLiteral("示波器已恢复默认设置并重新开始采集"), 4000);
}

void MainWindow::handleStreamInfo(const DataStreamInfo &info)
{
    if (sender() && sender() != m_dataSource) {
        return;
    }
    if (!info.isValid()) {
        handleError(QStringLiteral("数据源提供了无效的流参数"));
        return;
    }
    bool channelControlsChanged = m_channelChecks.size() != info.channelCount;
    if (!channelControlsChanged) {
        for (int channel = 0; channel < info.channelCount; ++channel) {
            const QString name = channel < info.channelNames.size()
                ? info.channelNames[channel]
                : QStringLiteral("CH%1").arg(channel + 1);
            if (m_channelChecks[channel]->text() != name) {
                channelControlsChanged = true;
                break;
            }
        }
    }

    m_streamInfo = info;
    m_waveform->setStreamInfo(info);
    if (info.sampleRateHz >= 100000.0) {
        /* High-speed mode: a 100 ms/div window would hold ~800k samples and
         * take seconds per repaint, freezing the UI thread. Default to a
         * 50 ms window (40k samples @ 800 kS/s) that repaints at full rate;
         * the user can still zoom out manually. */
        const QSignalBlocker blocker(m_timePerDivisionCombo);
        m_timePerDivisionCombo->setCurrentIndex(3); // 5 ms/div
        m_waveform->setTimePerDivision(0.005);
    } else if (m_serialDataSource && info.sampleRateHz < 1000.0) {
        const QSignalBlocker blocker(m_timePerDivisionCombo);
        m_timePerDivisionCombo->setCurrentIndex(6); // 100 ms/div
        m_waveform->setTimePerDivision(0.1);
    }
    if (channelControlsChanged) {
        rebuildChannelControls(info);
    }
    if (m_triggerSourceCombo) {
        m_triggerSourceCombo->clear();
        for (int channel = 0; channel < info.channelCount; ++channel) {
            const QString name = channel < info.channelNames.size()
                ? info.channelNames[channel]
                : QStringLiteral("CH%1").arg(channel + 1);
            m_triggerSourceCombo->addItem(name, channel);
        }
    }
    if (m_measurementTable) {
        QStringList labels;
        for (int channel = 0; channel < info.channelCount; ++channel) {
            labels.append(channel < info.channelNames.size()
                              ? info.channelNames[channel]
                              : QStringLiteral("CH%1").arg(channel + 1));
        }
        m_measurementTable->setHorizontalHeaderLabels(labels);
    }
    applyTriggerConfiguration();
}

void MainWindow::handleBlock(const DataBlockPtr &block)
{
    if (sender() && sender() != m_dataSource) {
        return;
    }
    if (!block || !block->isValid()) {
        handleError(QStringLiteral("数据源提供了无效的数据块"));
        return;
    }
    m_totalFrames += block->frameCount;
    m_waveform->appendBlock(block);
    if (m_mathEnabledCheck && m_mathEnabledCheck->isChecked()) {
        const DataBlock mathBlock = m_mathEngine.compute(*block, currentMathOperation());
        if (mathBlock.isValid()) {
            m_waveform->appendMathBlock(mathBlock);
        }
    }
    processTriggerBlock(*block);
}

void MainWindow::handleRunningChanged(bool running)
{
    if (sender() && sender() != m_dataSource) {
        return;
    }
    m_sourceRunning = running;
    m_waveform->setRunning(running);

    const QSignalBlocker blocker(m_runButton);
    m_runButton->setChecked(running);
    m_runButton->setText(running ? QStringLiteral("暂停") : QStringLiteral("开始"));
}

void MainWindow::handleError(const QString &message)
{
    statusBar()->showMessage(message, 5000);
}

void MainWindow::connectDataSource(IDataSource *source)
{
    if (!source) {
        return;
    }
    connect(source, &IDataSource::streamInfoChanged,
            this, &MainWindow::handleStreamInfo, Qt::QueuedConnection);
    connect(source, &IDataSource::blockReady,
            this, &MainWindow::handleBlock, Qt::QueuedConnection);
    connect(source, &IDataSource::runningChanged,
            this, &MainWindow::handleRunningChanged, Qt::QueuedConnection);
    connect(source, &IDataSource::errorOccurred,
            this, &MainWindow::handleError, Qt::QueuedConnection);
}

void MainWindow::updateStatus()
{
    const qint64 elapsedMs = std::max<qint64>(1, m_statusClock.restart());
    const qint64 deltaFrames = m_totalFrames - m_lastStatusFrames;
    m_lastStatusFrames = m_totalFrames;
    const double receivedRate = 1000.0 * deltaFrames / static_cast<double>(elapsedMs);

    const QString state = m_sourceRunning ? QStringLiteral("运行") : QStringLiteral("暂停");
    m_statusLabel->setText(
        QStringLiteral("%1  |  输入 %2  |  实收 %3  |  %4 通道  |  绘制 %5 FPS  |  累计 %6 帧")
            .arg(state)
            .arg(formatSampleRate(m_streamInfo.sampleRateHz))
            .arg(formatSampleRate(receivedRate))
            .arg(m_streamInfo.channelCount)
            .arg(m_renderFps, 0, 'f', 1)
            .arg(m_totalFrames));
    updateMeasurements();
    requestFftAnalysis();
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("AC880 Zynq-7020 · Linux 数据采集示波器"));
    resize(1280, 760);

    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(12, 10, 12, 10);
    rootLayout->setSpacing(10);

    auto *toolbar = new QFrame(central);
    toolbar->setObjectName(QStringLiteral("toolbarFrame"));
    auto *toolbarLayout = new QGridLayout(toolbar);
    toolbarLayout->setContentsMargins(10, 7, 10, 7);
    toolbarLayout->setHorizontalSpacing(8);
    toolbarLayout->setVerticalSpacing(5);

    m_runButton = new QPushButton(QStringLiteral("开始"), toolbar);
    m_runButton->setCheckable(true);
    m_runButton->setMinimumWidth(86);
    connect(m_runButton, &QPushButton::toggled,
            this, &MainWindow::requestRunning);

    auto *initializeButton = new QPushButton(QStringLiteral("初始化"), toolbar);
    initializeButton->setObjectName(QStringLiteral("initializeButton"));
    auto *clearButton = new QPushButton(QStringLiteral("清屏"), toolbar);
    m_liveButton = new QPushButton(QStringLiteral("回到实时"), toolbar);

    m_timePerDivisionCombo = new QComboBox(toolbar);
    m_timePerDivisionCombo->setObjectName(QStringLiteral("timePerDivisionCombo"));
    for (const auto &setting : {
             qMakePair(QStringLiteral("100 μs/div"), 0.0001),
             qMakePair(QStringLiteral("500 μs/div"), 0.0005),
             qMakePair(QStringLiteral("1 ms/div"), 0.001),
             qMakePair(QStringLiteral("5 ms/div"), 0.005),
             qMakePair(QStringLiteral("10 ms/div"), 0.01),
             qMakePair(QStringLiteral("50 ms/div"), 0.05),
             qMakePair(QStringLiteral("100 ms/div"), 0.1),
             qMakePair(QStringLiteral("200 ms/div"), 0.2),
             qMakePair(QStringLiteral("500 ms/div"), 0.5)}) {
        m_timePerDivisionCombo->addItem(setting.first, setting.second);
    }
    m_timePerDivisionCombo->setCurrentIndex(4);

    m_voltsPerDivisionCombo = new QComboBox(toolbar);
    m_voltsPerDivisionCombo->setObjectName(QStringLiteral("voltsPerDivisionCombo"));
    for (const auto &setting : {
             qMakePair(QStringLiteral("10 mV/div"), 0.01),
             qMakePair(QStringLiteral("20 mV/div"), 0.02),
             qMakePair(QStringLiteral("50 mV/div"), 0.05),
             qMakePair(QStringLiteral("100 mV/div"), 0.1),
             qMakePair(QStringLiteral("250 mV/div"), 0.25),
             qMakePair(QStringLiteral("500 mV/div"), 0.5),
             qMakePair(QStringLiteral("1 V/div"), 1.0),
             qMakePair(QStringLiteral("2 V/div"), 2.0),
             qMakePair(QStringLiteral("5 V/div"), 5.0)}) {
        m_voltsPerDivisionCombo->addItem(setting.first, setting.second);
    }
    m_voltsPerDivisionCombo->setCurrentIndex(4);

    m_persistenceCombo = new QComboBox(toolbar);
    m_persistenceCombo->setObjectName(QStringLiteral("persistenceCombo"));
    m_persistenceCombo->addItem(QStringLiteral("关闭"),
                                static_cast<int>(WaveformWidget::PersistenceMode::Off));
    m_persistenceCombo->addItem(QStringLiteral("短余辉"),
                                static_cast<int>(WaveformWidget::PersistenceMode::Short));
    m_persistenceCombo->addItem(QStringLiteral("长余辉"),
                                static_cast<int>(WaveformWidget::PersistenceMode::Long));
    m_persistenceCombo->addItem(QStringLiteral("无限余辉"),
                                static_cast<int>(WaveformWidget::PersistenceMode::Infinite));

    toolbarLayout->addWidget(m_runButton, 0, 0);
    toolbarLayout->addWidget(initializeButton, 0, 1);
    toolbarLayout->addWidget(clearButton, 0, 2);
    toolbarLayout->addWidget(m_liveButton, 0, 3);
    toolbarLayout->addWidget(new QLabel(
                                 QStringLiteral("显示刷新上限：%1 FPS")
                                     .arg(m_waveform ? m_waveform->renderRateLimitFps()
                                                     : boundedEnvironmentInteger(
                                                           "ZYNQ_SCOPE_RENDER_FPS",
                                                           30, 5, 60)),
                                 toolbar),
                             0, 6, 1, 2, Qt::AlignRight | Qt::AlignVCenter);
    toolbarLayout->addWidget(new QLabel(QStringLiteral("X 时基"), toolbar), 1, 0);
    toolbarLayout->addWidget(m_timePerDivisionCombo, 1, 1);
    toolbarLayout->addWidget(new QLabel(QStringLiteral("Y 灵敏度"), toolbar), 1, 2);
    toolbarLayout->addWidget(m_voltsPerDivisionCombo, 1, 3);
    toolbarLayout->addWidget(new QLabel(QStringLiteral("余辉"), toolbar), 1, 4);
    toolbarLayout->addWidget(m_persistenceCombo, 1, 5);
    toolbarLayout->setColumnStretch(6, 1);

    auto *contentLayout = new QHBoxLayout;
    contentLayout->setSpacing(10);

    auto *channelGroup = new QGroupBox(QStringLiteral("通道"), central);
    channelGroup->setMinimumWidth(208);
    channelGroup->setMaximumWidth(248);
    m_channelContainer = channelGroup;
    m_channelLayout = new QVBoxLayout(channelGroup);
    m_channelLayout->setContentsMargins(12, 12, 12, 12);
    m_channelLayout->setSpacing(7);
    m_channelLayout->addStretch();

    m_waveform = new WaveformWidget(central);
    m_spectrum = new SpectrumWidget(central);
    m_viewTabs = new QTabWidget(central);
    m_viewTabs->setObjectName(QStringLiteral("viewTabs"));
    m_viewTabs->addTab(m_waveform, QStringLiteral("时域波形"));
    m_viewTabs->addTab(m_spectrum, QStringLiteral("FFT 频谱"));
    connect(initializeButton, &QPushButton::clicked,
            this, &MainWindow::initializeInstrument);
    connect(clearButton, &QPushButton::clicked,
            m_waveform, &WaveformWidget::clear);
    connect(m_liveButton, &QPushButton::clicked,
            m_waveform, &WaveformWidget::returnToLive);
    connect(m_timePerDivisionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                m_waveform->setTimePerDivision(
                    m_timePerDivisionCombo->itemData(index).toDouble());
            });
    connect(m_voltsPerDivisionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                m_waveform->setVoltsPerDivision(
                    m_voltsPerDivisionCombo->itemData(index).toDouble());
            });
    connect(m_waveform, &WaveformWidget::timeWindowChanged,
            this, [this](double seconds) {
                const double perDivision = seconds
                    / ScopeAxisTransform::HorizontalDivisions;
                int nearest = 0;
                double distance = std::numeric_limits<double>::max();
                for (int i = 0; i < m_timePerDivisionCombo->count(); ++i) {
                    const double candidate = m_timePerDivisionCombo->itemData(i).toDouble();
                    if (std::abs(candidate - perDivision) < distance) {
                        nearest = i;
                        distance = std::abs(candidate - perDivision);
                    }
                }
                const QSignalBlocker blocker(m_timePerDivisionCombo);
                m_timePerDivisionCombo->setCurrentIndex(nearest);
            });
    connect(m_waveform, &WaveformWidget::amplitudeRangeChanged,
            this, [this](double range) {
                const double perDivision = 2.0 * range
                    / ScopeAxisTransform::VerticalDivisions;
                int nearest = 0;
                double distance = std::numeric_limits<double>::max();
                for (int i = 0; i < m_voltsPerDivisionCombo->count(); ++i) {
                    const double candidate = m_voltsPerDivisionCombo->itemData(i).toDouble();
                    if (std::abs(candidate - perDivision) < distance) {
                        nearest = i;
                        distance = std::abs(candidate - perDivision);
                    }
                }
                const QSignalBlocker blocker(m_voltsPerDivisionCombo);
                m_voltsPerDivisionCombo->setCurrentIndex(nearest);
            });
    connect(m_persistenceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                m_waveform->setPersistenceMode(
                    static_cast<WaveformWidget::PersistenceMode>(
                        m_persistenceCombo->itemData(index).toInt()));
            });
    connect(m_waveform, &WaveformWidget::liveViewChanged,
            this, [this](bool live) { m_liveButton->setEnabled(!live); });
    m_liveButton->setEnabled(false);

    m_controlTabs = new QTabWidget(central);
    m_controlTabs->setObjectName(QStringLiteral("controlTabs"));
    m_controlTabs->setMinimumWidth(280);
    m_controlTabs->setMaximumWidth(340);
    const auto addScrollableControlPage = [this](QWidget *page, const QString &title) {
        auto *scrollArea = new QScrollArea(m_controlTabs);
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollArea->setWidgetResizable(true);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scrollArea->setWidget(page);
        if (qEnvironmentVariableIntValue("ZYNQ_SCOPE_TOUCH") != 0) {
            QScroller::grabGesture(scrollArea->viewport(), QScroller::TouchGesture);
            QScroller *scroller = QScroller::scroller(scrollArea->viewport());
            QScrollerProperties properties = scroller->scrollerProperties();
            // Avoid the default delayed control press and expensive overshoot
            // animations on a software-rendered framebuffer.
            properties.setScrollMetric(QScrollerProperties::MousePressEventDelay,
                                       0.0);
            properties.setScrollMetric(QScrollerProperties::FrameRate,
                                       QScrollerProperties::Fps20);
            properties.setScrollMetric(
                QScrollerProperties::HorizontalOvershootPolicy,
                QScrollerProperties::OvershootAlwaysOff);
            properties.setScrollMetric(
                QScrollerProperties::VerticalOvershootPolicy,
                QScrollerProperties::OvershootAlwaysOff);
            scroller->setScrollerProperties(properties);
        }
        m_controlTabs->addTab(scrollArea, title);
    };
    addScrollableControlPage(channelGroup, QStringLiteral("通道"));
    addScrollableControlPage(buildTriggerPage(), QStringLiteral("触发"));
    addScrollableControlPage(buildMeasurementPage(), QStringLiteral("测量"));
    addScrollableControlPage(buildAnalysisPage(), QStringLiteral("数学/FFT"));
    addScrollableControlPage(buildFilePage(), QStringLiteral("文件"));
    if (m_serialDataSource) {
        addScrollableControlPage(new BoardLinkWidget(m_serialDataSource, this),
                                 QStringLiteral("开发板"));
    }

    contentLayout->addWidget(m_controlTabs);
    contentLayout->addWidget(m_viewTabs, 1);

    rootLayout->addWidget(toolbar);
    rootLayout->addLayout(contentLayout, 1);
    setCentralWidget(central);

    m_statusLabel = new QLabel(QStringLiteral("等待数据源"), this);
    statusBar()->addPermanentWidget(m_statusLabel, 1);

    QString styleSheet = QStringLiteral(R"(
        QMainWindow, QWidget { background: #f4f7fb; color: #17212b; }
        #toolbarFrame, QGroupBox { background: #ffffff; border: 1px solid #cbd5e1; border-radius: 5px; }
        QGroupBox { margin-top: 10px; padding-top: 12px; }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
        QPushButton, QComboBox {
            background: #ffffff; border: 1px solid #b8c5d3; border-radius: 4px;
            padding: 6px 10px; min-height: 20px;
        }
        QPushButton:hover, QComboBox:hover { border-color: #2f80c0; }
        QPushButton:checked { background: #dceefe; border-color: #2f80c0; color: #0b4f80; }
        QCheckBox { spacing: 8px; }
        #channelControlFrame { background: #f8fafc; border: 1px solid #d5dee8; border-radius: 4px; }
        QDoubleSpinBox {
            background: #ffffff; border: 1px solid #b8c5d3; border-radius: 3px;
            padding: 3px 5px; min-height: 19px;
        }
        QStatusBar { background: #eaf0f6; border-top: 1px solid #cbd5e1; }
    )");
    if (qEnvironmentVariableIntValue("ZYNQ_SCOPE_TOUCH") != 0) {
        styleSheet += QStringLiteral(R"(
            QPushButton, QComboBox, QDoubleSpinBox { min-height: 30px; }
            QTabBar::tab { min-width: 58px; min-height: 30px; padding: 3px 5px; }
            QCheckBox { min-height: 30px; }
            QScrollBar:vertical { width: 22px; }
            QScrollBar:horizontal { height: 22px; }
        )");
    }
    setStyleSheet(styleSheet);
}

void MainWindow::rebuildChannelControls(const DataStreamInfo &info)
{
    while (m_channelLayout->count() > 0) {
        QLayoutItem *item = m_channelLayout->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_channelChecks.clear();
    m_channelScaleSpins.clear();
    m_channelOffsetSpins.clear();
    m_channelMarkerChecks.clear();
    m_channelMarkerPositionSpins.clear();
    m_channelMarkerReadoutLabels.clear();

    static const QString colors[] = {
        QStringLiteral("#1565c0"), QStringLiteral("#00897b"),
        QStringLiteral("#c62828"), QStringLiteral("#6a1b9a"),
        QStringLiteral("#ef6c00"), QStringLiteral("#2e7d32"),
        QStringLiteral("#5d4037"), QStringLiteral("#455a64")
    };

    for (int channel = 0; channel < info.channelCount; ++channel) {
        const QString name = channel < info.channelNames.size()
            ? info.channelNames[channel]
            : QStringLiteral("CH%1").arg(channel + 1);
        auto *channelFrame = new QFrame(m_channelContainer);
        channelFrame->setObjectName(QStringLiteral("channelControlFrame"));
        auto *channelLayout = new QGridLayout(channelFrame);
        channelLayout->setContentsMargins(7, 7, 7, 7);
        channelLayout->setHorizontalSpacing(5);
        channelLayout->setVerticalSpacing(4);

        auto *check = new QCheckBox(name, channelFrame);
        check->setObjectName(QStringLiteral("channelVisibleCheck%1").arg(channel));
        check->setChecked(true);
        check->setStyleSheet(QStringLiteral("QCheckBox { color: %1; }")
                                 .arg(colors[channel % 8]));
        connect(check, &QCheckBox::toggled,
                this, [this, channel](bool visible) {
                    m_waveform->setChannelVisible(channel, visible);
                });
        m_channelChecks.append(check);

        auto *scaleSpin = new QDoubleSpinBox(channelFrame);
        scaleSpin->setObjectName(QStringLiteral("channelScaleSpin%1").arg(channel));
        scaleSpin->setRange(0.1, 10.0);
        scaleSpin->setDecimals(2);
        scaleSpin->setSingleStep(0.1);
        scaleSpin->setValue(1.0);
        scaleSpin->setSuffix(QStringLiteral(" ×"));

        auto *offsetSpin = new QDoubleSpinBox(channelFrame);
        offsetSpin->setObjectName(QStringLiteral("channelOffsetSpin%1").arg(channel));
        offsetSpin->setRange(-10.0, 10.0);
        offsetSpin->setDecimals(2);
        offsetSpin->setSingleStep(0.1);
        offsetSpin->setValue(0.0);
        offsetSpin->setSuffix(QStringLiteral(" V"));

        connect(scaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, channel](double value) {
                    m_waveform->setChannelScale(channel, value);
                });
        connect(offsetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, channel](double value) {
                    m_waveform->setChannelOffset(channel, value);
                });
        auto *markerCheck = new QCheckBox(
            QStringLiteral("启用 M%1 波形标记").arg(channel + 1), channelFrame);
        markerCheck->setObjectName(QStringLiteral("channelMarkerCheck%1").arg(channel));
        markerCheck->setChecked(true);
        markerCheck->setStyleSheet(QStringLiteral("QCheckBox { color: %1; font-weight: 600; }")
                                       .arg(colors[channel % 8]));

        auto *markerPositionSpin = new QDoubleSpinBox(channelFrame);
        markerPositionSpin->setObjectName(
            QStringLiteral("channelMarkerPositionSpin%1").arg(channel));
        markerPositionSpin->setRange(0.0, 100.0);
        markerPositionSpin->setDecimals(1);
        markerPositionSpin->setSingleStep(1.0);
        markerPositionSpin->setSuffix(QStringLiteral(" %"));
        markerPositionSpin->setValue(100.0
            * m_waveform->measurementMarkerFraction(channel));

        auto *markerReadout = new QLabel(QStringLiteral("等待该位置的波形数据"), channelFrame);
        markerReadout->setObjectName(QStringLiteral("channelMarkerReadout%1").arg(channel));
        markerReadout->setWordWrap(true);
        markerReadout->setStyleSheet(QStringLiteral("QLabel { color: %1; }")
                                         .arg(colors[channel % 8]));

        connect(markerCheck, &QCheckBox::toggled,
                this, [this, channel, markerPositionSpin](bool enabled) {
                    markerPositionSpin->setEnabled(enabled);
                    m_waveform->setMeasurementMarkerEnabled(channel, enabled);
                });
        connect(markerPositionSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, channel](double percentage) {
                    m_waveform->setMeasurementMarkerFraction(channel,
                                                             percentage / 100.0);
                });
        connect(m_waveform, &WaveformWidget::measurementMarkerChanged,
                markerReadout,
                [this, channel, markerCheck, markerPositionSpin, markerReadout](
                    int changedChannel, bool valid, double positionPercent,
                    double timeSeconds, double voltage) {
                    if (changedChannel != channel) {
                        return;
                    }
                    {
                        const QSignalBlocker blocker(markerPositionSpin);
                        markerPositionSpin->setValue(positionPercent);
                    }
                    if (!markerCheck->isChecked()) {
                        markerReadout->setText(QStringLiteral("标记已关闭"));
                    } else if (!valid) {
                        markerReadout->setText(QStringLiteral("等待该位置的波形数据"));
                    } else {
                        markerReadout->setText(
                            QStringLiteral("t = %1\nU = %2")
                                .arg(formatTimeValue(timeSeconds),
                                     formatVoltageValue(voltage)));
                    }
                });
        m_channelMarkerChecks.append(markerCheck);
        m_channelMarkerPositionSpins.append(markerPositionSpin);
        m_channelMarkerReadoutLabels.append(markerReadout);
        m_channelScaleSpins.append(scaleSpin);
        m_channelOffsetSpins.append(offsetSpin);

        channelLayout->addWidget(check, 0, 0, 1, 2);
        channelLayout->addWidget(new QLabel(QStringLiteral("垂直增益"), channelFrame), 1, 0);
        channelLayout->addWidget(scaleSpin, 1, 1);
        channelLayout->addWidget(new QLabel(QStringLiteral("垂直偏置"), channelFrame), 2, 0);
        channelLayout->addWidget(offsetSpin, 2, 1);
        channelLayout->addWidget(markerCheck, 3, 0, 1, 2);
        channelLayout->addWidget(new QLabel(QStringLiteral("X 位置"), channelFrame), 4, 0);
        channelLayout->addWidget(markerPositionSpin, 4, 1);
        channelLayout->addWidget(markerReadout, 5, 0, 1, 2);
        m_channelLayout->addWidget(channelFrame);
    }
    auto *markerHint = new QLabel(
        QStringLiteral("M1/M2 吸附到各通道波形采样点。\n"
                       "设置 X 位置或在波形上左右拖动。"),
        m_channelContainer);
    markerHint->setWordWrap(true);
    markerHint->setStyleSheet(QStringLiteral("QLabel { color: #8fa7ba; padding: 6px 2px; }"));
    m_channelLayout->addWidget(markerHint);
    m_channelLayout->addStretch();
}

QWidget *MainWindow::buildTriggerPage()
{
    auto *page = new QWidget(this);
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(9, 9, 9, 9);

    m_triggerEnabledCheck = new QCheckBox(QStringLiteral("启用触发扫描"), page);
    m_triggerEnabledCheck->setObjectName(QStringLiteral("triggerEnabledCheck"));
    pageLayout->addWidget(m_triggerEnabledCheck);

    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_triggerTypeCombo = new QComboBox(page);
    m_triggerTypeCombo->addItem(QStringLiteral("边沿"), static_cast<int>(TriggerType::Edge));
    m_triggerTypeCombo->addItem(QStringLiteral("脉宽"), static_cast<int>(TriggerType::PulseWidth));
    m_triggerTypeCombo->addItem(QStringLiteral("视频同步"), static_cast<int>(TriggerType::Video));

    m_triggerModeCombo = new QComboBox(page);
    m_triggerModeCombo->addItem(QStringLiteral("自动"), static_cast<int>(TriggerMode::Auto));
    m_triggerModeCombo->addItem(QStringLiteral("正常"), static_cast<int>(TriggerMode::Normal));
    m_triggerModeCombo->addItem(QStringLiteral("单次"), static_cast<int>(TriggerMode::Single));

    m_triggerSourceCombo = new QComboBox(page);
    m_triggerSourceCombo->addItem(QStringLiteral("CH1"), 0);
    m_triggerSourceCombo->addItem(QStringLiteral("CH2"), 1);

    m_triggerSlopeCombo = new QComboBox(page);
    m_triggerSlopeCombo->addItem(QStringLiteral("上升沿"), static_cast<int>(TriggerSlope::Rising));
    m_triggerSlopeCombo->addItem(QStringLiteral("下降沿"), static_cast<int>(TriggerSlope::Falling));
    m_triggerSlopeCombo->addItem(QStringLiteral("双边沿"), static_cast<int>(TriggerSlope::Either));

    m_triggerLevelSpin = new QDoubleSpinBox(page);
    m_triggerLevelSpin->setRange(-10.0, 10.0);
    m_triggerLevelSpin->setDecimals(3);
    m_triggerLevelSpin->setSingleStep(0.05);
    m_triggerLevelSpin->setSuffix(QStringLiteral(" V"));

    m_triggerHysteresisSpin = new QDoubleSpinBox(page);
    m_triggerHysteresisSpin->setRange(0.0, 5.0);
    m_triggerHysteresisSpin->setDecimals(3);
    m_triggerHysteresisSpin->setSingleStep(0.01);
    m_triggerHysteresisSpin->setValue(0.02);
    m_triggerHysteresisSpin->setSuffix(QStringLiteral(" V"));

    m_triggerHoldoffSpin = new QDoubleSpinBox(page);
    m_triggerHoldoffSpin->setRange(0.0, 10000.0);
    m_triggerHoldoffSpin->setDecimals(2);
    m_triggerHoldoffSpin->setValue(1.0);
    m_triggerHoldoffSpin->setSuffix(QStringLiteral(" ms"));

    m_triggerPretriggerSpin = new QDoubleSpinBox(page);
    m_triggerPretriggerSpin->setRange(0.0, 90.0);
    m_triggerPretriggerSpin->setDecimals(0);
    m_triggerPretriggerSpin->setSingleStep(10.0);
    m_triggerPretriggerSpin->setValue(50.0);
    m_triggerPretriggerSpin->setSuffix(QStringLiteral(" %"));

    m_pulsePolarityCombo = new QComboBox(page);
    m_pulsePolarityCombo->addItem(QStringLiteral("正脉宽"), true);
    m_pulsePolarityCombo->addItem(QStringLiteral("负脉宽"), false);

    m_pulseConditionCombo = new QComboBox(page);
    m_pulseConditionCombo->addItem(QStringLiteral("小于"), static_cast<int>(PulseWidthCondition::LessThan));
    m_pulseConditionCombo->addItem(QStringLiteral("大于"), static_cast<int>(PulseWidthCondition::GreaterThan));
    m_pulseConditionCombo->addItem(QStringLiteral("区间内"), static_cast<int>(PulseWidthCondition::InsideRange));
    m_pulseConditionCombo->addItem(QStringLiteral("区间外"), static_cast<int>(PulseWidthCondition::OutsideRange));
    m_pulseConditionCombo->setCurrentIndex(1);

    m_pulseMinimumSpin = new QDoubleSpinBox(page);
    m_pulseMinimumSpin->setRange(0.1, 1000000.0);
    m_pulseMinimumSpin->setDecimals(1);
    m_pulseMinimumSpin->setValue(5.0);
    m_pulseMinimumSpin->setSuffix(QStringLiteral(" μs"));

    m_pulseMaximumSpin = new QDoubleSpinBox(page);
    m_pulseMaximumSpin->setRange(0.1, 1000000.0);
    m_pulseMaximumSpin->setDecimals(1);
    m_pulseMaximumSpin->setValue(100.0);
    m_pulseMaximumSpin->setSuffix(QStringLiteral(" μs"));

    m_videoStandardCombo = new QComboBox(page);
    m_videoStandardCombo->addItem(QStringLiteral("PAL"), static_cast<int>(VideoStandard::PAL));
    m_videoStandardCombo->addItem(QStringLiteral("NTSC"), static_cast<int>(VideoStandard::NTSC));
    m_videoSyncCombo = new QComboBox(page);
    m_videoSyncCombo->addItem(QStringLiteral("行同步"), static_cast<int>(VideoSync::Line));
    m_videoSyncCombo->addItem(QStringLiteral("场同步"), static_cast<int>(VideoSync::Field));

    form->addRow(QStringLiteral("类型"), m_triggerTypeCombo);
    form->addRow(QStringLiteral("模式"), m_triggerModeCombo);
    form->addRow(QStringLiteral("源"), m_triggerSourceCombo);
    form->addRow(QStringLiteral("边沿"), m_triggerSlopeCombo);
    form->addRow(QStringLiteral("电平"), m_triggerLevelSpin);
    form->addRow(QStringLiteral("迟滞"), m_triggerHysteresisSpin);
    form->addRow(QStringLiteral("保持"), m_triggerHoldoffSpin);
    form->addRow(QStringLiteral("预触发"), m_triggerPretriggerSpin);
    form->addRow(QStringLiteral("极性"), m_pulsePolarityCombo);
    form->addRow(QStringLiteral("脉宽条件"), m_pulseConditionCombo);
    form->addRow(QStringLiteral("最小脉宽"), m_pulseMinimumSpin);
    form->addRow(QStringLiteral("最大脉宽"), m_pulseMaximumSpin);
    form->addRow(QStringLiteral("视频制式"), m_videoStandardCombo);
    form->addRow(QStringLiteral("同步"), m_videoSyncCombo);
    pageLayout->addLayout(form);

    auto *armButton = new QPushButton(QStringLiteral("重新武装单次触发"), page);
    connect(armButton, &QPushButton::clicked, this, &MainWindow::armSingleTrigger);
    pageLayout->addWidget(armButton);

    m_triggerStatusLabel = new QLabel(QStringLiteral("状态：滚动显示"), page);
    m_triggerStatusLabel->setWordWrap(true);
    pageLayout->addWidget(m_triggerStatusLabel);
    pageLayout->addStretch();

    connect(m_triggerEnabledCheck, &QCheckBox::toggled,
            this, &MainWindow::applyTriggerConfiguration);
    const auto connectCombo = [this](QComboBox *combo) {
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::applyTriggerConfiguration);
    };
    connectCombo(m_triggerTypeCombo);
    connectCombo(m_triggerModeCombo);
    connectCombo(m_triggerSourceCombo);
    connectCombo(m_triggerSlopeCombo);
    connectCombo(m_pulsePolarityCombo);
    connectCombo(m_pulseConditionCombo);
    connectCombo(m_videoStandardCombo);
    connectCombo(m_videoSyncCombo);

    const auto connectSpin = [this](QDoubleSpinBox *spin) {
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &MainWindow::applyTriggerConfiguration);
    };
    connectSpin(m_triggerLevelSpin);
    connectSpin(m_triggerHysteresisSpin);
    connectSpin(m_triggerHoldoffSpin);
    connectSpin(m_triggerPretriggerSpin);
    connectSpin(m_pulseMinimumSpin);
    connectSpin(m_pulseMaximumSpin);
    return page;
}

QWidget *MainWindow::buildMeasurementPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);

    m_measurementTable = new QTableWidget(8, 2, page);
    m_measurementTable->setVerticalHeaderLabels({
        QStringLiteral("最大值"), QStringLiteral("最小值"),
        QStringLiteral("峰峰值"), QStringLiteral("平均值"),
        QStringLiteral("RMS"), QStringLiteral("频率"),
        QStringLiteral("周期"), QStringLiteral("占空比")
    });
    m_measurementTable->setHorizontalHeaderLabels({QStringLiteral("CH1"),
                                                    QStringLiteral("CH2")});
    m_measurementTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_measurementTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_measurementTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_measurementTable->setSelectionMode(QAbstractItemView::NoSelection);
    for (int row = 0; row < m_measurementTable->rowCount(); ++row) {
        for (int column = 0; column < m_measurementTable->columnCount(); ++column) {
            auto *item = new QTableWidgetItem(QStringLiteral("--"));
            item->setTextAlignment(Qt::AlignCenter);
            m_measurementTable->setItem(row, column, item);
        }
    }
    layout->addWidget(m_measurementTable, 1);

    m_cursorCheck = new QCheckBox(QStringLiteral("显示并启用 Δt / ΔV 光标"), page);
    m_cursorCheck->setObjectName(QStringLiteral("cursorCheck"));
    layout->addWidget(m_cursorCheck);
    m_cursorReadoutLabel = new QLabel(
        QStringLiteral("T1 --   T2 --\nΔt --   1/Δt --\nV1 --   V2 --   ΔV --"), page);
    m_cursorReadoutLabel->setWordWrap(true);
    m_cursorReadoutLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_cursorReadoutLabel);

    connect(m_cursorCheck, &QCheckBox::toggled,
            m_waveform, &WaveformWidget::setCursorsVisible);
    connect(m_waveform, &WaveformWidget::cursorReadoutChanged,
            this, [this](double t1, double t2, double dt,
                         double v1, double v2, double dv) {
                const QString reciprocal = std::abs(dt) > 1e-15
                    ? QStringLiteral("%1 Hz").arg(std::abs(1.0 / dt), 0, 'g', 7)
                    : QStringLiteral("--");
                m_cursorReadoutLabel->setText(
                    QStringLiteral("T1 %1   T2 %2\n"
                                   "Δt %3   1/Δt %4\n"
                                   "V1 %5   V2 %6   ΔV %7")
                        .arg(formatTimeValue(t1))
                        .arg(formatTimeValue(t2))
                        .arg(formatTimeValue(dt))
                        .arg(reciprocal)
                        .arg(formatVoltageValue(v1))
                        .arg(formatVoltageValue(v2))
                        .arg(formatVoltageValue(dv)));
            });
    return page;
}

QWidget *MainWindow::buildAnalysisPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(9, 9, 9, 9);
    auto *form = new QFormLayout;

    m_mathEnabledCheck = new QCheckBox(QStringLiteral("叠加显示数学通道 M"), page);
    m_mathEnabledCheck->setObjectName(QStringLiteral("mathEnabledCheck"));
    m_mathOperationCombo = new QComboBox(page);
    m_mathOperationCombo->addItem(QStringLiteral("CH1 + CH2"), static_cast<int>(MathOperation::Add));
    m_mathOperationCombo->addItem(QStringLiteral("CH1 - CH2"), static_cast<int>(MathOperation::Subtract12));
    m_mathOperationCombo->addItem(QStringLiteral("CH2 - CH1"), static_cast<int>(MathOperation::Subtract21));
    m_mathOperationCombo->addItem(QStringLiteral("CH1 × CH2"), static_cast<int>(MathOperation::Multiply));
    m_mathOperationCombo->addItem(QStringLiteral("CH1 ÷ CH2"), static_cast<int>(MathOperation::Divide12));
    m_mathOperationCombo->addItem(QStringLiteral("CH2 ÷ CH1"), static_cast<int>(MathOperation::Divide21));

    m_fftSourceCombo = new QComboBox(page);
    m_fftSourceCombo->setObjectName(QStringLiteral("fftSourceCombo"));
    m_fftSourceCombo->addItem(QStringLiteral("CH1"), 0);
    m_fftSourceCombo->addItem(QStringLiteral("CH2"), 1);
    m_fftSourceCombo->addItem(QStringLiteral("数学通道 M"), 2);

    m_fftPointsCombo = new QComboBox(page);
    m_fftPointsCombo->setObjectName(QStringLiteral("fftPointsCombo"));
    for (const int points : {256, 512, 1024, 2048, 4096, 8192, 16384}) {
        m_fftPointsCombo->addItem(QString::number(points), points);
    }
    m_fftPointsCombo->setCurrentIndex(
        m_serialDataSource ? m_fftPointsCombo->findData(256)
                           : m_fftPointsCombo->findData(4096));

    m_fftWindowCombo = new QComboBox(page);
    m_fftWindowCombo->setObjectName(QStringLiteral("fftWindowCombo"));
    m_fftWindowCombo->addItem(QStringLiteral("矩形"), static_cast<int>(WindowFunction::Rectangular));
    m_fftWindowCombo->addItem(QStringLiteral("Hann"), static_cast<int>(WindowFunction::Hann));
    m_fftWindowCombo->addItem(QStringLiteral("Hamming"), static_cast<int>(WindowFunction::Hamming));
    m_fftWindowCombo->addItem(QStringLiteral("Blackman"), static_cast<int>(WindowFunction::Blackman));
    m_fftWindowCombo->setCurrentIndex(1);

    m_fftDecibelsCheck = new QCheckBox(QStringLiteral("dB 幅值"), page);
    m_fftDecibelsCheck->setChecked(true);

    form->addRow(m_mathEnabledCheck);
    form->addRow(QStringLiteral("数学运算"), m_mathOperationCombo);
    form->addRow(QStringLiteral("FFT 源"), m_fftSourceCombo);
    form->addRow(QStringLiteral("FFT 点数"), m_fftPointsCombo);
    form->addRow(QStringLiteral("窗函数"), m_fftWindowCombo);
    form->addRow(m_fftDecibelsCheck);
    layout->addLayout(form);

    auto *showSpectrumButton = new QPushButton(QStringLiteral("切换到 FFT 频谱"), page);
    connect(showSpectrumButton, &QPushButton::clicked,
            this, [this] { m_viewTabs->setCurrentIndex(1); });
    layout->addWidget(showSpectrumButton);

    auto *showWaveformButton = new QPushButton(QStringLiteral("返回时域波形"), page);
    connect(showWaveformButton, &QPushButton::clicked,
            this, [this] { m_viewTabs->setCurrentIndex(0); });
    layout->addWidget(showWaveformButton);

    m_fftStatusLabel = new QLabel(QStringLiteral("FFT：等待足够样本"), page);
    m_fftStatusLabel->setWordWrap(true);
    layout->addWidget(m_fftStatusLabel);
    layout->addStretch();

    connect(m_mathEnabledCheck, &QCheckBox::toggled,
            this, [this](bool enabled) {
                m_waveform->clearMath();
                m_waveform->setMathVisible(enabled);
            });
    connect(m_mathOperationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { m_waveform->clearMath(); });
    return page;
}

QWidget *MainWindow::buildFilePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(9, 9, 9, 9);

    auto *screenshotButton = new QPushButton(QStringLiteral("保存界面截图 PNG"), page);
    auto *exportCsvButton = new QPushButton(QStringLiteral("导出可见波形 CSV"), page);
    auto *exportTxtButton = new QPushButton(QStringLiteral("导出可见波形 TXT"), page);
    auto *importButton = new QPushButton(QStringLiteral("导入 CSV/TXT 并回放"), page);
    auto *liveButton = new QPushButton(QStringLiteral("返回实时仿真数据"), page);
    layout->addWidget(screenshotButton);
    layout->addWidget(exportCsvButton);
    layout->addWidget(exportTxtButton);
    layout->addWidget(importButton);
    layout->addWidget(liveButton);

    auto *form = new QFormLayout;
    m_replaySpeedCombo = new QComboBox(page);
    for (const double speed : {0.25, 0.5, 1.0, 2.0, 4.0}) {
        m_replaySpeedCombo->addItem(QStringLiteral("%1×").arg(speed, 0, 'g', 3), speed);
    }
    m_replaySpeedCombo->setCurrentIndex(2);
    m_replayLoopCheck = new QCheckBox(QStringLiteral("循环回放"), page);
    form->addRow(QStringLiteral("回放速度"), m_replaySpeedCombo);
    form->addRow(m_replayLoopCheck);
    layout->addLayout(form);

    m_fileStatusLabel = new QLabel(
        QStringLiteral("导入上限：200 万组样本；导出上限：当前可见区间 100 万组"), page);
    m_fileStatusLabel->setWordWrap(true);
    layout->addWidget(m_fileStatusLabel);
    layout->addStretch();

    connect(screenshotButton, &QPushButton::clicked,
            this, &MainWindow::captureScreenshot);
    connect(exportCsvButton, &QPushButton::clicked,
            this, &MainWindow::exportCsv);
    connect(exportTxtButton, &QPushButton::clicked,
            this, &MainWindow::exportTxt);
    connect(importButton, &QPushButton::clicked,
            this, &MainWindow::importReplayFile);
    connect(liveButton, &QPushButton::clicked,
            this, &MainWindow::returnToLiveSource);
    connect(m_replaySpeedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                const double speed = m_replaySpeedCombo->currentData().toDouble();
                QMetaObject::invokeMethod(m_replayDataSource,
                    [source = m_replayDataSource, speed] { source->setPlaybackSpeed(speed); },
                    Qt::QueuedConnection);
            });
    connect(m_replayLoopCheck, &QCheckBox::toggled,
            this, [this](bool looping) {
                QMetaObject::invokeMethod(m_replayDataSource,
                    [source = m_replayDataSource, looping] { source->setLooping(looping); },
                    Qt::QueuedConnection);
            });
    return page;
}

void MainWindow::captureScreenshot()
{
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存示波器截图"), QStringLiteral("zynqscope.png"),
        QStringLiteral("PNG 图像 (*.png)"));
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".png");
    }
    const bool success = grab().save(path, "PNG");
    m_fileStatusLabel->setText(success
        ? QStringLiteral("截图已保存：%1").arg(path)
        : QStringLiteral("截图保存失败：%1").arg(path));
}

void MainWindow::exportCsv()
{
    exportTextFile(',', QStringLiteral("CSV 数据 (*.csv)"), QStringLiteral(".csv"));
}

void MainWindow::exportTxt()
{
    exportTextFile('\t', QStringLiteral("TXT 数据 (*.txt)"), QStringLiteral(".txt"));
}

void MainWindow::exportTextFile(QChar delimiter,
                                const QString &filter,
                                const QString &suffix)
{
    if (m_fileBusy) {
        m_fileStatusLabel->setText(QStringLiteral("文件任务正在运行，请稍候"));
        return;
    }
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出波形数据"), QStringLiteral("waveform%1").arg(suffix), filter);
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(suffix, Qt::CaseInsensitive)) {
        path += suffix;
    }
    const DataBlock block = m_waveform->visibleSnapshot(1000000);
    if (!block.isValid()) {
        m_fileStatusLabel->setText(QStringLiteral("当前没有可导出的波形"));
        return;
    }
    m_fileBusy = true;
    m_fileStatusLabel->setText(QStringLiteral("正在导出 %1 组样本…").arg(block.frameCount));
    emit exportTextRequested(path, block, m_streamInfo.channelNames, delimiter);
}

void MainWindow::importReplayFile()
{
    if (m_fileBusy) {
        m_fileStatusLabel->setText(QStringLiteral("文件任务正在运行，请稍候"));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入波形文件"), QString(),
        QStringLiteral("波形数据 (*.csv *.txt);;CSV 数据 (*.csv);;TXT 数据 (*.txt)"));
    if (path.isEmpty()) {
        return;
    }
    m_fileBusy = true;
    m_fileStatusLabel->setText(QStringLiteral("正在导入：%1").arg(QFileInfo(path).fileName()));
    emit importTextRequested(path, 2000000);
}

void MainWindow::handleExportFinished(const QString &path,
                                      bool success,
                                      const QString &message)
{
    m_fileBusy = false;
    if (m_discardPendingFileResult) {
        m_discardPendingFileResult = false;
        return;
    }
    m_fileStatusLabel->setText(success
        ? QStringLiteral("%1：%2").arg(message, path)
        : QStringLiteral("导出失败：%1").arg(message));
}

void MainWindow::handleImportFinished(const DataBlock &block,
                                      const QStringList &channelNames,
                                      bool success,
                                      const QString &message)
{
    m_fileBusy = false;
    if (m_discardPendingFileResult) {
        m_discardPendingFileResult = false;
        return;
    }
    if (!success || !block.isValid()) {
        m_fileStatusLabel->setText(QStringLiteral("导入失败：%1").arg(message));
        return;
    }

    QMetaObject::invokeMethod(m_dataSource, "stop", Qt::QueuedConnection);
    m_dataSource = m_replayDataSource;
    m_waveform->clear();
    m_spectrum->clear();
    m_pendingTriggerSample = -1;
    const double speed = m_replaySpeedCombo->currentData().toDouble();
    const bool looping = m_replayLoopCheck->isChecked();
    QMetaObject::invokeMethod(m_replayDataSource,
        [source = m_replayDataSource, block, channelNames, speed, looping] {
            source->loadData(block, channelNames);
            source->setPlaybackSpeed(speed);
            source->setLooping(looping);
            source->start();
        }, Qt::QueuedConnection);
    m_fileStatusLabel->setText(QStringLiteral("%1；正在按 %2× 回放")
                                   .arg(message)
                                   .arg(speed, 0, 'g', 3));
}

void MainWindow::returnToLiveSource()
{
    if (m_dataSource == m_liveDataSource) {
        return;
    }
    QMetaObject::invokeMethod(m_replayDataSource, "clearData", Qt::QueuedConnection);
    m_dataSource = m_liveDataSource;
    m_waveform->clear();
    m_spectrum->clear();
    QMetaObject::invokeMethod(m_liveDataSource, "start", Qt::QueuedConnection);
    m_fileStatusLabel->setText(QStringLiteral("已返回实时仿真数据"));
}

MathOperation MainWindow::currentMathOperation() const
{
    if (!m_mathOperationCombo) {
        return MathOperation::Add;
    }
    return static_cast<MathOperation>(m_mathOperationCombo->currentData().toInt());
}

void MainWindow::requestFftAnalysis()
{
    if (m_fftBusy || !m_fftPointsCombo || !m_streamInfo.isValid()) {
        return;
    }

    const int pointCount = m_fftPointsCombo->currentData().toInt();
    DataBlock input = m_waveform->newestSnapshot(pointCount);
    if (!input.isValid() || input.frameCount < pointCount) {
        if (m_fftStatusLabel) {
            m_fftStatusLabel->setText(QStringLiteral("FFT：等待 %1 个样本").arg(pointCount));
        }
        return;
    }

    int channel = m_fftSourceCombo->currentData().toInt();
    if (channel == 2) {
        input = m_mathEngine.compute(input, currentMathOperation());
        channel = 0;
    }

    FftConfig config;
    config.pointCount = pointCount;
    config.window = static_cast<WindowFunction>(m_fftWindowCombo->currentData().toInt());
    config.decibels = m_fftDecibelsCheck->isChecked();
    config.removeDc = true;
    m_fftBusy = true;
    emit fftAnalysisRequested(input, channel, config);
}

void MainWindow::handleFftResult(const SpectrumResult &result)
{
    m_fftBusy = false;
    if (m_discardPendingFftResult) {
        m_discardPendingFftResult = false;
        return;
    }
    if (!result.valid) {
        m_fftStatusLabel->setText(QStringLiteral("FFT：输入无效或样本不足"));
        return;
    }
    m_spectrum->setSpectrum(result);
    QString status = QStringLiteral("上位机 FFT：%1 点，Δf=%2 Hz\n主峰：%3 Hz")
        .arg(result.fftSize)
        .arg(result.binWidthHz, 0, 'g', 6)
        .arg(result.peakFrequencyHz, 0, 'g', 7);
    if (m_boardFftValid) {
        status += QStringLiteral("\n板端：%1 Hz / %2 Vpk / %3 µs\n频差：%4 Hz")
            .arg(m_boardPeakFrequencyHz, 0, 'f', 3)
            .arg(m_boardPeakAmplitudeVolts, 0, 'f', 3)
            .arg(m_boardFftComputeMicroseconds)
            .arg(qAbs(result.peakFrequencyHz - m_boardPeakFrequencyHz), 0, 'f', 3);
    }
    m_fftStatusLabel->setText(status);
}

void MainWindow::updateMeasurements()
{
    if (!m_measurementTable || !m_streamInfo.isValid()) {
        return;
    }
    const DataBlock snapshot = m_waveform->visibleSnapshot(m_measurementMaximumFrames);
    if (!snapshot.isValid()) {
        return;
    }

    const int displayedChannels = std::min(snapshot.channelCount,
                                            m_measurementTable->columnCount());
    for (int channel = 0; channel < displayedChannels; ++channel) {
        const MeasurementResult result = m_measurementEngine.analyze(snapshot, channel);
        if (!result.valid) {
            continue;
        }
        const QString values[] = {
            QStringLiteral("%1 V").arg(result.maximum, 0, 'g', 6),
            QStringLiteral("%1 V").arg(result.minimum, 0, 'g', 6),
            QStringLiteral("%1 V").arg(result.peakToPeak, 0, 'g', 6),
            QStringLiteral("%1 V").arg(result.mean, 0, 'g', 6),
            QStringLiteral("%1 V").arg(result.rms, 0, 'g', 6),
            result.frequencyValid
                ? QStringLiteral("%1 Hz").arg(result.frequencyHz, 0, 'g', 7)
                : QStringLiteral("--"),
            result.frequencyValid
                ? QStringLiteral("%1 ms").arg(result.periodSeconds * 1000.0, 0, 'g', 7)
                : QStringLiteral("--"),
            QStringLiteral("%1 %").arg(result.dutyCyclePercent, 0, 'f', 2)
        };
        for (int row = 0; row < 8; ++row) {
            m_measurementTable->item(row, channel)->setText(values[row]);
        }
    }
}

void MainWindow::applyTriggerConfiguration()
{
    if (!m_triggerEnabledCheck) {
        return;
    }

    TriggerConfig config;
    config.type = static_cast<TriggerType>(m_triggerTypeCombo->currentData().toInt());
    config.mode = static_cast<TriggerMode>(m_triggerModeCombo->currentData().toInt());
    config.sourceChannel = m_triggerSourceCombo->currentData().toInt();
    config.slope = static_cast<TriggerSlope>(m_triggerSlopeCombo->currentData().toInt());
    config.level = m_triggerLevelSpin->value();
    config.hysteresis = m_triggerHysteresisSpin->value();
    config.holdoffSeconds = m_triggerHoldoffSpin->value() / 1000.0;
    config.pretriggerRatio = m_triggerPretriggerSpin->value() / 100.0;
    config.positivePulse = m_pulsePolarityCombo->currentData().toBool();
    config.pulseCondition = static_cast<PulseWidthCondition>(
        m_pulseConditionCombo->currentData().toInt());
    config.minimumPulseWidthSeconds = m_pulseMinimumSpin->value() / 1000000.0;
    config.maximumPulseWidthSeconds = m_pulseMaximumSpin->value() / 1000000.0;
    config.videoStandard = static_cast<VideoStandard>(
        m_videoStandardCombo->currentData().toInt());
    config.videoSync = static_cast<VideoSync>(m_videoSyncCombo->currentData().toInt());
    m_triggerEngine.setConfig(config);
    m_pendingTriggerSample = -1;

    const bool enabled = m_triggerEnabledCheck->isChecked();
    if (!enabled) {
        m_triggerStatusLabel->setText(QStringLiteral("状态：滚动显示"));
        m_waveform->returnToLive();
    } else {
        m_triggerStatusLabel->setText(QStringLiteral("状态：等待触发"));
    }
}

void MainWindow::armSingleTrigger()
{
    m_triggerEngine.armSingle();
    m_pendingTriggerSample = -1;
    if (m_triggerEnabledCheck) {
        m_triggerEnabledCheck->setChecked(true);
    }
    m_triggerStatusLabel->setText(QStringLiteral("状态：单次触发已武装"));
}

void MainWindow::processTriggerBlock(const DataBlock &block)
{
    if (!m_triggerEnabledCheck || !m_triggerEnabledCheck->isChecked()) {
        return;
    }

    const auto event = m_triggerEngine.process(block);
    if (event && m_pendingTriggerSample < 0) {
        m_pendingTriggerSample = event->sampleIndex;
        m_triggerStatusLabel->setText(event->automatic
            ? QStringLiteral("状态：自动触发，等待后触发数据")
            : QStringLiteral("状态：已触发，等待后触发数据"));
    }

    if (m_pendingTriggerSample < 0 || !m_streamInfo.isValid()) {
        return;
    }

    const TriggerConfig &config = m_triggerEngine.config();
    const qint64 displayFrames = qRound64(
        m_waveform->timeWindowSeconds() * m_streamInfo.sampleRateHz);
    const qint64 posttriggerFrames = qRound64(
        (1.0 - config.pretriggerRatio) * displayFrames);
    const qint64 newestSample = block.firstSampleIndex + block.frameCount;
    if (newestSample < m_pendingTriggerSample + posttriggerFrames) {
        return;
    }

    m_waveform->freezeViewAt(m_pendingTriggerSample, config.pretriggerRatio);
    m_triggerStatusLabel->setText(QStringLiteral("状态：触发记录已显示，采样点 %1")
                                      .arg(m_pendingTriggerSample));
    m_pendingTriggerSample = -1;
}

QString MainWindow::formatSampleRate(double sampleRateHz) const
{
    if (sampleRateHz >= 1000000.0) {
        return QStringLiteral("%1 MS/s").arg(sampleRateHz / 1000000.0, 0, 'f', 2);
    }
    if (sampleRateHz >= 1000.0) {
        return QStringLiteral("%1 kS/s").arg(sampleRateHz / 1000.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 S/s").arg(sampleRateHz, 0, 'f', 0);
}

QString MainWindow::formatTimeValue(double seconds) const
{
    const double absolute = std::abs(seconds);
    if (absolute < 1e-15) {
        return QStringLiteral("0 s");
    }
    if (absolute < 1e-6) {
        return QStringLiteral("%1 ns").arg(seconds * 1e9, 0, 'g', 5);
    }
    if (absolute < 1e-3) {
        return QStringLiteral("%1 μs").arg(seconds * 1e6, 0, 'g', 5);
    }
    if (absolute < 1.0) {
        return QStringLiteral("%1 ms").arg(seconds * 1e3, 0, 'g', 5);
    }
    return QStringLiteral("%1 s").arg(seconds, 0, 'g', 5);
}

QString MainWindow::formatVoltageValue(double volts) const
{
    const double absolute = std::abs(volts);
    if (absolute < 1e-15) {
        return QStringLiteral("0 V");
    }
    if (absolute < 1e-3) {
        return QStringLiteral("%1 μV").arg(volts * 1e6, 0, 'g', 5);
    }
    if (absolute < 1.0) {
        return QStringLiteral("%1 mV").arg(volts * 1e3, 0, 'g', 5);
    }
    return QStringLiteral("%1 V").arg(volts, 0, 'g', 5);
}
