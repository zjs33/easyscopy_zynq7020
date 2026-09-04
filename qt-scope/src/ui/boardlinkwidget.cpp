#include "ui/boardlinkwidget.h"

#include "acquisition/serialdatasource.h"
#include "processing/dacfunctionexpression.h"
#include "ui/dacfunctionpreview.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

BoardLinkWidget::BoardLinkWidget(SerialDataSource *dataSource, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(9, 9, 9, 9);
    layout->setSpacing(8);

    auto *connectionGroup = new QGroupBox(QStringLiteral("通信与连接"), this);
    auto *connectionLayout = new QVBoxLayout(connectionGroup);
    connectionLayout->setContentsMargins(8, 10, 8, 8);
    connectionLayout->setSpacing(6);

    auto *transportRow = new QHBoxLayout;
    m_transportCombo = new QComboBox(connectionGroup);
    m_transportCombo->setObjectName(QStringLiteral("boardTransportCombo"));
    m_transportCombo->addItem(QStringLiteral("串口 COM"),
                              static_cast<int>(SerialDataSource::TransportMode::Serial));
    m_transportCombo->addItem(QStringLiteral("WiFi TCP 监听"),
                              static_cast<int>(SerialDataSource::TransportMode::TcpServer));
    m_transportCombo->addItem(QStringLiteral("CH347 USB-SPI"),
                              static_cast<int>(SerialDataSource::TransportMode::Ch347Spi));
    transportRow->addWidget(new QLabel(QStringLiteral("通信方式"), connectionGroup));
    transportRow->addWidget(m_transportCombo, 1);
    m_connectButton = new QPushButton(QStringLiteral("连接"), connectionGroup);
    m_connectButton->setMinimumWidth(88);
    transportRow->addWidget(m_connectButton);
    connectionLayout->addLayout(transportRow);

    auto *portRow = new QHBoxLayout;
    m_portCombo = new QComboBox(connectionGroup);
    m_portCombo->setObjectName(QStringLiteral("serialPortCombo"));
    m_refreshButton = new QPushButton(QStringLiteral("刷新"), connectionGroup);
    portRow->addWidget(m_portCombo, 1);
    portRow->addWidget(m_refreshButton);
    connectionLayout->addLayout(portRow);

    auto *tcpRow = new QHBoxLayout;
    m_tcpAddressEdit = new QLineEdit(dataSource->tcpListenAddress(), connectionGroup);
    m_tcpAddressEdit->setObjectName(QStringLiteral("tcpListenAddressEdit"));
    m_tcpAddressEdit->setPlaceholderText(QStringLiteral("0.0.0.0"));
    m_tcpPortSpin = new QSpinBox(connectionGroup);
    m_tcpPortSpin->setObjectName(QStringLiteral("tcpListenPortSpin"));
    m_tcpPortSpin->setRange(1, 65535);
    m_tcpPortSpin->setValue(dataSource->tcpListenPort());
    tcpRow->addWidget(new QLabel(QStringLiteral("监听地址"), connectionGroup));
    tcpRow->addWidget(m_tcpAddressEdit, 1);
    tcpRow->addWidget(m_tcpPortSpin);
    connectionLayout->addLayout(tcpRow);

    m_ch347Panel = new QWidget(connectionGroup);
    auto *ch347Form = new QFormLayout(m_ch347Panel);
    ch347Form->setContentsMargins(0, 0, 0, 0);
    auto *dllRow = new QWidget(m_ch347Panel);
    auto *dllLayout = new QHBoxLayout(dllRow);
    dllLayout->setContentsMargins(0, 0, 0, 0);
    m_ch347DllEdit = new QLineEdit(dataSource->ch347DllPath(), dllRow);
    m_ch347DllEdit->setObjectName(QStringLiteral("ch347DllPathEdit"));
    m_ch347DllEdit->setToolTip(m_ch347DllEdit->text());
    m_ch347BrowseButton = new QPushButton(QStringLiteral("浏览…"), dllRow);
    dllLayout->addWidget(m_ch347DllEdit, 1);
    dllLayout->addWidget(m_ch347BrowseButton);
    m_ch347DeviceSpin = new QSpinBox(m_ch347Panel);
    m_ch347DeviceSpin->setObjectName(QStringLiteral("ch347DeviceIndexSpin"));
    m_ch347DeviceSpin->setRange(0, 15);
    m_ch347DeviceSpin->setValue(dataSource->ch347DeviceIndex());
    m_ch347ClockCombo = new QComboBox(m_ch347Panel);
    m_ch347ClockCombo->setObjectName(QStringLiteral("ch347ClockCombo"));
    const QStringList clocks = {
        QStringLiteral("60 MHz（0）"), QStringLiteral("30 MHz（1）"),
        QStringLiteral("15 MHz（2）"), QStringLiteral("7.5 MHz（3，推荐）"),
        QStringLiteral("3.75 MHz（4）"), QStringLiteral("1.875 MHz（5）"),
        QStringLiteral("937.5 kHz（6）"), QStringLiteral("468.75 kHz（7，慢速保险）")
    };
    for (int index = 0; index < clocks.size(); ++index) {
        m_ch347ClockCombo->addItem(clocks.at(index), index);
    }
    m_ch347ClockCombo->setCurrentIndex(dataSource->ch347ClockIndex());
    ch347Form->addRow(QStringLiteral("64 位 DLL"), dllRow);
    ch347Form->addRow(QStringLiteral("设备序号"), m_ch347DeviceSpin);
    ch347Form->addRow(QStringLiteral("SPI 时钟"), m_ch347ClockCombo);
    connectionLayout->addWidget(m_ch347Panel);

    m_transportHint = new QLabel(connectionGroup);
    m_transportHint->setWordWrap(true);
    m_transportHint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    connectionLayout->addWidget(m_transportHint);

    layout->addWidget(connectionGroup);

    m_stateLabel = new QLabel(QStringLiteral("状态：等待连接"), this);
    m_stateLabel->setWordWrap(true);
    layout->addWidget(m_stateLabel);

    auto *actionRow = new QHBoxLayout;
    m_pingButton = new QPushButton(QStringLiteral("PING"), this);
    m_infoButton = new QPushButton(QStringLiteral("读取信息"), this);
    m_pingButton->setEnabled(false);
    m_infoButton->setEnabled(false);
    actionRow->addWidget(m_pingButton);
    actionRow->addWidget(m_infoButton);
    layout->addLayout(actionRow);

    m_streamCheck = new QCheckBox(QStringLiteral("接收板端 DAC/ADC 回环波形"), this);
    m_streamCheck->setChecked(true);
    m_highSpeedCheck = new QCheckBox(QStringLiteral("高速采集（AD9280 并行 800 kS/s）"), this);
    m_highSpeedCheck->setObjectName(QStringLiteral("highSpeedCheck"));
    m_highSpeedCheck->setChecked(dataSource->highSpeedMode());
    m_highSpeedCheck->setEnabled(false);
    m_autoPingCheck = new QCheckBox(QStringLiteral("每秒自动 PING"), this);
    m_autoPingCheck->setChecked(true);
    layout->addWidget(m_streamCheck);
    layout->addWidget(m_highSpeedCheck);
    layout->addWidget(m_autoPingCheck);

    m_pingLabel = new QLabel(QStringLiteral("往返延迟：--"), this);
    m_statisticsLabel = new QLabel(QStringLiteral("RX 0 B / TX 0 B / 样本 0"), this);
    layout->addWidget(m_pingLabel);
    layout->addWidget(m_statisticsLabel);

    /* Collapsible DAC generator section: folded by default so the streaming /
     * high-speed checkboxes stay visible on the first screen. */
    auto *generatorToggle = new QToolButton(this);
    generatorToggle->setObjectName(QStringLiteral("generatorToggle"));
    generatorToggle->setText(QStringLiteral("▸ DAC 回环信号发生器"));
    generatorToggle->setCheckable(true);
    generatorToggle->setChecked(false);
    generatorToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    generatorToggle->setAutoRaise(true);
    layout->addWidget(generatorToggle);

    auto *generatorBody = new QWidget(this);
    auto *generatorLayout = new QVBoxLayout(generatorBody);
    generatorLayout->setContentsMargins(0, 0, 0, 0);
    auto *generatorForm = new QFormLayout;
    generatorForm->setContentsMargins(0, 0, 0, 0);
    m_waveformCombo = new QComboBox(generatorBody);
    m_waveformCombo->setObjectName(QStringLiteral("dacWaveformCombo"));
    m_waveformCombo->addItem(QStringLiteral("直流"), QStringLiteral("DC"));
    m_waveformCombo->addItem(QStringLiteral("三角波"), QStringLiteral("TRI"));
    m_waveformCombo->addItem(QStringLiteral("正弦波"), QStringLiteral("SIN"));
    m_waveformCombo->addItem(QStringLiteral("方波"), QStringLiteral("SQUARE"));
    m_waveformCombo->addItem(QStringLiteral("锯齿波"), QStringLiteral("SAW"));
    m_waveformCombo->addItem(QStringLiteral("自定义函数"), QStringLiteral("LUT"));
    m_waveformCombo->setCurrentIndex(2); /* 正弦波默认 */

    m_frequencySpin = new QDoubleSpinBox(generatorBody);
    m_frequencySpin->setObjectName(QStringLiteral("dacFrequencySpin"));
    m_frequencySpin->setRange(0.1, 2000.0);
    m_frequencySpin->setDecimals(2);
    m_frequencySpin->setSingleStep(0.1);
    m_frequencySpin->setValue(1000.0);
    m_frequencySpin->setSuffix(QStringLiteral(" Hz"));

    m_amplitudeSpin = new QDoubleSpinBox(generatorBody);
    m_amplitudeSpin->setObjectName(QStringLiteral("dacAmplitudeSpin"));
    m_amplitudeSpin->setRange(0.0, 5.0);
    m_amplitudeSpin->setDecimals(3);
    m_amplitudeSpin->setSingleStep(0.1);
    m_amplitudeSpin->setValue(3.9);
    m_amplitudeSpin->setSuffix(QStringLiteral(" Vpk"));

    m_offsetSpin = new QDoubleSpinBox(generatorBody);
    m_offsetSpin->setObjectName(QStringLiteral("dacOffsetSpin"));
    m_offsetSpin->setRange(-5.0, 5.0);
    m_offsetSpin->setDecimals(3);
    m_offsetSpin->setSingleStep(0.1);
    m_offsetSpin->setValue(0.0);
    m_offsetSpin->setSuffix(QStringLiteral(" V"));

    m_applyGeneratorButton = new QPushButton(QStringLiteral("应用到 DAC"), generatorBody);
    m_applyGeneratorButton->setObjectName(QStringLiteral("applyDacGeneratorButton"));
    m_applyGeneratorButton->setEnabled(false);
    m_generatorLabel = new QLabel(
        QStringLiteral("默认：1000 Hz 正弦波，幅度 3.9 Vpk，偏置 0 V"),
        generatorBody);
    m_generatorLabel->setWordWrap(true);
    m_formulaEdit = new QLineEdit(
        QStringLiteral("1.0*sin(x) + 0.5*tri(2*x) + 0.2"), generatorBody);
    m_formulaEdit->setObjectName(QStringLiteral("dacFormulaEdit"));
    m_formulaEdit->setToolTip(
        QStringLiteral("x∈[0,1)，支持 sin/cos/tri/square/saw 与 + - * / 括号"));
    m_functionPreview = new DacFunctionPreview(generatorBody);
    m_functionPreview->setObjectName(QStringLiteral("dacFunctionPreview"));

    generatorForm->addRow(QStringLiteral("波形"), m_waveformCombo);
    generatorForm->addRow(QStringLiteral("频率"), m_frequencySpin);
    generatorForm->addRow(QStringLiteral("幅度"), m_amplitudeSpin);
    generatorForm->addRow(QStringLiteral("偏置/直流电压"), m_offsetSpin);
    generatorForm->addRow(QStringLiteral("函数 f(x) / V"), m_formulaEdit);
    generatorForm->addRow(QStringLiteral("256 点预览"), m_functionPreview);
    generatorForm->addRow(m_applyGeneratorButton);
    generatorForm->addRow(m_generatorLabel);
    generatorLayout->addLayout(generatorForm);
    generatorBody->setVisible(false);
    layout->addWidget(generatorBody);
    connect(generatorToggle, &QToolButton::toggled, generatorBody, &QWidget::setVisible);
    connect(generatorToggle, &QToolButton::toggled, this, [generatorToggle](bool on) {
        generatorToggle->setText(on ? QStringLiteral("▾ DAC 回环信号发生器")
                                    : QStringLiteral("▸ DAC 回环信号发生器"));
    });

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(300);
    m_log->setMinimumHeight(90);
    layout->addWidget(m_log);

    m_autoPingTimer = new QTimer(this);
    m_autoPingTimer->setInterval(1000);
    connect(m_autoPingTimer, &QTimer::timeout, this, [this] {
        /* High-speed stream mode must not be interrupted by 256-byte
         * control frames: the slave treats any frame starting with the
         * C347 magic as HS_STOP and tears down the stream DMA. */
        if (m_connected && m_autoPingCheck->isChecked()
            && !m_highSpeedCheck->isChecked()) {
            emit pingRequested();
        }
    });
    m_autoPingTimer->start();

    connect(m_refreshButton, &QPushButton::clicked,
            this, &BoardLinkWidget::refreshPorts);
    connect(m_ch347BrowseButton, &QPushButton::clicked, this, [this] {
        const QString selected = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择 64 位 CH347DLLA64.DLL"),
            m_ch347DllEdit->text(), QStringLiteral("DLL (*.dll);;所有文件 (*)"));
        if (!selected.isEmpty()) {
            m_ch347DllEdit->setText(QDir::toNativeSeparators(selected));
            m_ch347DllEdit->setToolTip(m_ch347DllEdit->text());
        }
    });
    connect(m_transportCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BoardLinkWidget::updateTransportControls);
    connect(m_connectButton, &QPushButton::clicked,
            this, &BoardLinkWidget::toggleConnection);
    connect(m_pingButton, &QPushButton::clicked,
            this, &BoardLinkWidget::pingRequested);
    connect(m_infoButton, &QPushButton::clicked,
            this, &BoardLinkWidget::infoRequested);
    connect(m_streamCheck, &QCheckBox::toggled,
            this, &BoardLinkWidget::streamingRequested);
    connect(m_highSpeedCheck, &QCheckBox::toggled,
            this, [this](bool enabled) {
        if (enabled) {
            QSignalBlocker blocker(m_streamCheck);
            m_streamCheck->setChecked(false);
        }
        /* Control frames (PING/INFO) must not interrupt the stream. */
        m_pingButton->setEnabled(m_connected && !enabled);
        m_infoButton->setEnabled(m_connected && !enabled);
        m_autoPingCheck->setEnabled(!enabled);
        emit highSpeedModeRequested(enabled);
    });
    connect(m_waveformCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BoardLinkWidget::updateGeneratorControls);
    connect(m_offsetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &BoardLinkWidget::updateGeneratorControls);
    connect(m_formulaEdit, &QLineEdit::textChanged,
            this, &BoardLinkWidget::updateGeneratorControls);
    connect(m_applyGeneratorButton, &QPushButton::clicked,
            this, &BoardLinkWidget::applyGeneratorSettings);

    connect(this, &BoardLinkWidget::portSelected,
            dataSource, &SerialDataSource::setPortName, Qt::QueuedConnection);
    connect(this, &BoardLinkWidget::transportModeSelected,
            dataSource, &SerialDataSource::setTransportMode, Qt::QueuedConnection);
    connect(this, &BoardLinkWidget::tcpListenAddressSelected,
            dataSource, &SerialDataSource::setTcpListenAddress, Qt::QueuedConnection);
    connect(this, &BoardLinkWidget::tcpListenPortSelected,
            dataSource, &SerialDataSource::setTcpListenPort, Qt::QueuedConnection);
    connect(this, &BoardLinkWidget::ch347DllPathSelected,
            dataSource, &SerialDataSource::setCh347DllPath, Qt::QueuedConnection);
    connect(this, &BoardLinkWidget::ch347DeviceIndexSelected,
            dataSource, &SerialDataSource::setCh347DeviceIndex, Qt::QueuedConnection);
    connect(this, &BoardLinkWidget::ch347ClockIndexSelected,
            dataSource, &SerialDataSource::setCh347ClockIndex, Qt::QueuedConnection);
    connect(this, &BoardLinkWidget::startRequested,
            dataSource, &SerialDataSource::start, Qt::QueuedConnection);
    connect(this, &BoardLinkWidget::stopRequested,
            dataSource, &SerialDataSource::stop, Qt::QueuedConnection);
    connect(this, &BoardLinkWidget::pingRequested,
            dataSource, &SerialDataSource::requestPing, Qt::QueuedConnection);
    connect(this, &BoardLinkWidget::infoRequested,
            dataSource, &SerialDataSource::requestInfo, Qt::QueuedConnection);
    connect(this, &BoardLinkWidget::streamingRequested,
            dataSource, &SerialDataSource::setStreaming, Qt::QueuedConnection);
    connect(this, &BoardLinkWidget::highSpeedModeRequested,
            dataSource, &SerialDataSource::setHighSpeedMode, Qt::QueuedConnection);
    connect(this, &BoardLinkWidget::generatorRequested,
            dataSource, &SerialDataSource::configureGenerator, Qt::QueuedConnection);
    connect(this, &BoardLinkWidget::customWaveformRequested,
            dataSource, &SerialDataSource::uploadWaveform, Qt::QueuedConnection);

    connect(dataSource, &SerialDataSource::linkStateChanged,
            this, &BoardLinkWidget::handleLinkState);
    connect(dataSource, &SerialDataSource::protocolLineReceived,
            this, &BoardLinkWidget::appendProtocolLine);
    connect(dataSource, &SerialDataSource::pingResult,
            this, &BoardLinkWidget::handlePingResult);
    connect(dataSource, &SerialDataSource::statisticsChanged,
            this, &BoardLinkWidget::handleStatistics);
    connect(dataSource, &SerialDataSource::waveformUploadProgress,
            this, [this](int sent, int total) {
        m_generatorLabel->setText(QStringLiteral("正在上传 LUT：%1/%2 字节").arg(sent).arg(total));
    });
    connect(dataSource, &SerialDataSource::waveformUploadFinished,
            this, [this](bool success, const QString &message) {
        m_generatorLabel->setText((success ? QStringLiteral("成功：")
                                           : QStringLiteral("失败：")) + message);
        m_applyGeneratorButton->setEnabled(m_connected);
    });

    refreshPorts();
    const int requestedIndex = m_portCombo->findText(dataSource->portName(),
                                                      Qt::MatchFixedString);
    if (requestedIndex >= 0) {
        m_portCombo->setCurrentIndex(requestedIndex);
    }
    const int transportIndex = m_transportCombo->findData(
        static_cast<int>(dataSource->transportMode()));
    if (transportIndex >= 0) {
        m_transportCombo->setCurrentIndex(transportIndex);
    }
    updateTransportControls();
    updateGeneratorControls();
}

void BoardLinkWidget::refreshPorts()
{
    const QString current = m_portCombo->currentText();
    const QStringList ports = SerialDataSource::availablePortNames();
    m_portCombo->clear();
    m_portCombo->addItems(ports);
    int index = m_portCombo->findText(current, Qt::MatchFixedString);
    if (index < 0) {
        index = m_portCombo->findText(QStringLiteral("COM5"), Qt::MatchFixedString);
    }
    if (index >= 0) {
        m_portCombo->setCurrentIndex(index);
    }
}

void BoardLinkWidget::updateTransportControls()
{
    const bool tcp = m_transportCombo->currentData().toInt()
        == static_cast<int>(SerialDataSource::TransportMode::TcpServer);
    const bool ch347 = m_transportCombo->currentData().toInt()
        == static_cast<int>(SerialDataSource::TransportMode::Ch347Spi);
    m_portCombo->setVisible(!tcp && !ch347);
    m_refreshButton->setVisible(!tcp && !ch347);
    m_tcpAddressEdit->setVisible(tcp);
    m_tcpPortSpin->setVisible(tcp);
    m_ch347Panel->setVisible(ch347);
    m_highSpeedCheck->setEnabled(ch347 && m_connected);
    m_transportHint->setText(
        ch347
            ? QStringLiteral("CS→PB12 · SCK→PB13 · MISO→PB14 · MOSI→PB15 · 3.3V Mode0")
            : (tcp
                   ? QStringLiteral("ESP8266 透传：串口侧 115200 8-N-1，"
                                    "本机监听 TCP 端口")
                   : QStringLiteral("USART1 PA9/PA10 · 115200 8-N-1")));
}

void BoardLinkWidget::toggleConnection()
{
    if (m_connected) {
        emit stopRequested();
        return;
    }
    const bool tcp = m_transportCombo->currentData().toInt()
        == static_cast<int>(SerialDataSource::TransportMode::TcpServer);
    const bool ch347 = m_transportCombo->currentData().toInt()
        == static_cast<int>(SerialDataSource::TransportMode::Ch347Spi);
    if (!tcp && !ch347 && m_portCombo->currentText().isEmpty()) {
        m_stateLabel->setText(QStringLiteral("状态：没有可用串口"));
        return;
    }
    emit transportModeSelected(m_transportCombo->currentData().toInt());
    if (tcp) {
        emit tcpListenAddressSelected(m_tcpAddressEdit->text());
        emit tcpListenPortSelected(m_tcpPortSpin->value());
    } else if (ch347) {
        emit ch347DllPathSelected(m_ch347DllEdit->text());
        emit ch347DeviceIndexSelected(m_ch347DeviceSpin->value());
        emit ch347ClockIndexSelected(m_ch347ClockCombo->currentData().toInt());
    } else {
        emit portSelected(m_portCombo->currentText());
    }
    emit startRequested();
}

void BoardLinkWidget::handleLinkState(bool connected,
                                      const QString &portName,
                                      const QString &message)
{
    m_connected = connected;
    m_connectButton->setText(connected ? QStringLiteral("断开")
                                       : QStringLiteral("连接"));
    m_portCombo->setEnabled(!connected);
    m_transportCombo->setEnabled(!connected);
    m_refreshButton->setEnabled(!connected);
    m_tcpAddressEdit->setEnabled(!connected);
    m_tcpPortSpin->setEnabled(!connected);
    m_ch347DllEdit->setEnabled(!connected);
    m_ch347BrowseButton->setEnabled(!connected);
    m_ch347DeviceSpin->setEnabled(!connected);
    m_ch347ClockCombo->setEnabled(!connected);
    m_pingButton->setEnabled(connected);
    m_infoButton->setEnabled(connected);
    m_applyGeneratorButton->setEnabled(connected);
    const bool ch347 = m_transportCombo->currentData().toInt()
        == static_cast<int>(SerialDataSource::TransportMode::Ch347Spi);
    m_highSpeedCheck->setEnabled(connected && ch347);
    m_streamCheck->setEnabled(connected);
    m_stateLabel->setText(QStringLiteral("状态：%1").arg(message));
    if (!portName.isEmpty()) {
        const int index = m_portCombo->findText(portName, Qt::MatchFixedString);
        if (index >= 0) {
            m_portCombo->setCurrentIndex(index);
        }
    }
}

void BoardLinkWidget::applyGeneratorSettings()
{
    const QString waveform = m_waveformCombo->currentData().toString();
    const int frequencyMilliHz = qRound(m_frequencySpin->value() * 1000.0);
    if (waveform == QStringLiteral("LUT")) {
        const DacFunctionResult function = DacFunctionExpression::generate(
            m_formulaEdit->text(), 256);
        if (!function.valid) {
            m_generatorLabel->setText(QStringLiteral("公式错误：%1").arg(function.error));
            m_functionPreview->setSamples({});
            return;
        }
        m_functionPreview->setSamples(function.volts);
        m_generatorLabel->setText(
            QStringLiteral("LUT：256 点，CRC=0x%1，限幅 %2 点（%3%）")
                .arg(function.crc16, 4, 16, QLatin1Char('0'))
                .arg(function.clippedCount)
                .arg(function.clippedCount * 100.0 / 256.0, 0, 'f', 1));
        m_applyGeneratorButton->setEnabled(false);
        emit customWaveformRequested(function.codes, frequencyMilliHz, function.crc16);
        return;
    }
    const double amplitudeVolts = waveform == QStringLiteral("DC")
        ? 0.0 : m_amplitudeSpin->value();
    const double offsetVolts = m_offsetSpin->value();
    const int amplitudeCode = qBound(0, qRound(amplitudeVolts * 25.5), 127);
    const int offsetCode = qBound(0, qRound((5.0 - offsetVolts) * 25.5), 255);
    emit generatorRequested(waveform, frequencyMilliHz,
                            amplitudeCode, offsetCode);

    m_generatorLabel->setText(
        waveform == QStringLiteral("DC")
            ? QStringLiteral("已请求：直流输出 %1 V（DAC码 %2）")
                  .arg(offsetVolts, 0, 'f', 3)
                  .arg(offsetCode)
            : QStringLiteral("已请求：%1，%2 Hz，幅度 %3 Vpk，偏置 %4 V"
                             "（码幅 %5，中心码 %6）")
                  .arg(m_waveformCombo->currentText())
                  .arg(m_frequencySpin->value(), 0, 'f', 2)
                  .arg(amplitudeVolts, 0, 'f', 3)
                  .arg(offsetVolts, 0, 'f', 3)
                  .arg(amplitudeCode)
                  .arg(offsetCode));
}

void BoardLinkWidget::updateGeneratorControls()
{
    const QString waveform = m_waveformCombo->currentData().toString();
    const bool custom = waveform == QStringLiteral("LUT");
    const bool varying = waveform != QStringLiteral("DC");
    const double maximumAmplitude = qMax(0.0, 5.0 - qAbs(m_offsetSpin->value()));
    m_amplitudeSpin->setMaximum(maximumAmplitude);
    m_frequencySpin->setEnabled(varying);
    m_amplitudeSpin->setEnabled(varying && !custom);
    m_offsetSpin->setEnabled(!custom);
    m_formulaEdit->setVisible(custom);
    m_functionPreview->setVisible(custom);
    if (custom) {
        const DacFunctionResult function = DacFunctionExpression::generate(
            m_formulaEdit->text(), 256);
        m_functionPreview->setSamples(function.volts);
    }
}

void BoardLinkWidget::appendProtocolLine(const QString &line, bool outgoing)
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    m_log->appendPlainText(QStringLiteral("%1 %2 %3")
                               .arg(timestamp, outgoing ? QStringLiteral("TX>")
                                                       : QStringLiteral("RX<"), line));
}

void BoardLinkWidget::handlePingResult(quint32 sequence,
                                       double roundTripMilliseconds,
                                       quint32 deviceMilliseconds)
{
    m_pingLabel->setText(
        QStringLiteral("往返延迟：%1 ms（序号 %2，板端运行 %3 ms）")
            .arg(roundTripMilliseconds, 0, 'f', 2)
            .arg(sequence)
            .arg(deviceMilliseconds));
}

void BoardLinkWidget::handleStatistics(quint64 receivedBytes,
                                       quint64 transmittedBytes,
                                       quint64 receivedSamples)
{
    m_statisticsLabel->setText(QStringLiteral("RX %1 B / TX %2 B / 样本 %3")
                                   .arg(receivedBytes)
                                   .arg(transmittedBytes)
                                   .arg(receivedSamples));
}
