#include "acquisition/filereplaydatasource.h"
#include "acquisition/serialdatasource.h"
#include "acquisition/simulationdatasource.h"
#include "plotting/spectrumwidget.h"
#include "plotting/waveformwidget.h"
#include "processing/fftengine.h"
#include "ui/mainwindow.h"
#include "ui/boardlinkwidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QtTest>

class UiSmokeTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void startsStreamsMeasuresAndAnalyzes();
    void cursorReadoutsUseXAxisReference();
    void boardControlsUseExternalVolts();
};

void UiSmokeTest::initTestCase()
{
    qRegisterMetaType<DataStreamInfo>();
    qRegisterMetaType<DataBlock>();
    qRegisterMetaType<DataBlockPtr>();
    qRegisterMetaType<FftConfig>();
    qRegisterMetaType<SpectrumResult>();
}

void UiSmokeTest::startsStreamsMeasuresAndAnalyzes()
{
    SimulationDataSource liveSource(20000.0, 2);
    FileReplayDataSource replaySource;
    MainWindow window(&liveSource, &replaySource);
    window.show();

    auto *waveform = window.findChild<WaveformWidget *>();
    auto *spectrum = window.findChild<SpectrumWidget *>();
    auto *controlTabs = window.findChild<QTabWidget *>(QStringLiteral("controlTabs"));
    auto *viewTabs = window.findChild<QTabWidget *>(QStringLiteral("viewTabs"));
    QVERIFY(waveform);
    QVERIFY(spectrum);
    QVERIFY(controlTabs);
    QVERIFY(viewTabs);
    QCOMPARE(controlTabs->count(), 5);
    QCOMPARE(viewTabs->count(), 2);

    QTRY_VERIFY_WITH_TIMEOUT(waveform->newestSnapshot(1024).isValid(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(spectrum->hasSpectrum(), 3000);

    auto *measurementTable = window.findChild<QTableWidget *>();
    QVERIFY(measurementTable);
    QTRY_VERIFY_WITH_TIMEOUT(measurementTable->item(5, 0)->text() != QStringLiteral("--"),
                             3000);

    auto *cursorCheck = window.findChild<QCheckBox *>(QStringLiteral("cursorCheck"));
    QVERIFY(cursorCheck);
    cursorCheck->setChecked(true);
    QTest::qWait(100);
    QVERIFY(cursorCheck->isChecked());

    auto *initializeButton = window.findChild<QPushButton *>(
        QStringLiteral("initializeButton"));
    auto *timePerDivision = window.findChild<QComboBox *>(
        QStringLiteral("timePerDivisionCombo"));
    auto *voltsPerDivision = window.findChild<QComboBox *>(
        QStringLiteral("voltsPerDivisionCombo"));
    auto *persistence = window.findChild<QComboBox *>(QStringLiteral("persistenceCombo"));
    auto *mathEnabled = window.findChild<QCheckBox *>(QStringLiteral("mathEnabledCheck"));
    auto *triggerEnabled = window.findChild<QCheckBox *>(
        QStringLiteral("triggerEnabledCheck"));
    auto *channelVisible = window.findChild<QCheckBox *>(
        QStringLiteral("channelVisibleCheck0"));
    auto *channelScale = window.findChild<QDoubleSpinBox *>(
        QStringLiteral("channelScaleSpin0"));
    auto *channelOffset = window.findChild<QDoubleSpinBox *>(
        QStringLiteral("channelOffsetSpin0"));
    auto *channelMarkerCheck = window.findChild<QCheckBox *>(
        QStringLiteral("channelMarkerCheck0"));
    auto *channelMarkerPosition = window.findChild<QDoubleSpinBox *>(
        QStringLiteral("channelMarkerPositionSpin0"));
    auto *channelMarkerReadout = window.findChild<QLabel *>(
        QStringLiteral("channelMarkerReadout0"));
    QVERIFY(initializeButton);
    QVERIFY(timePerDivision);
    QVERIFY(voltsPerDivision);
    QVERIFY(persistence);
    QVERIFY(mathEnabled);
    QVERIFY(triggerEnabled);
    QVERIFY(channelVisible);
    QVERIFY(channelScale);
    QVERIFY(channelOffset);
    QVERIFY(channelMarkerCheck);
    QVERIFY(channelMarkerPosition);
    QVERIFY(channelMarkerReadout);

    timePerDivision->setCurrentIndex(8);
    voltsPerDivision->setCurrentIndex(8);
    persistence->setCurrentIndex(3);
    mathEnabled->setChecked(true);
    triggerEnabled->setChecked(true);
    channelVisible->setChecked(false);
    channelScale->setValue(2.0);
    channelOffset->setValue(0.5);
    QCOMPARE(waveform->channelOffset(0), 0.5);
    channelMarkerPosition->setValue(42.0);
    QCOMPARE(waveform->measurementMarkerFraction(0), 0.42);
    controlTabs->setCurrentIndex(4);
    viewTabs->setCurrentIndex(1);

    timePerDivision->setCurrentIndex(0);
    QCOMPARE(waveform->timeWindowSeconds(), 0.001);
    timePerDivision->setCurrentIndex(8);

    QTest::mouseClick(initializeButton, Qt::LeftButton);
    QCOMPARE(timePerDivision->currentIndex(), 4);
    QCOMPARE(voltsPerDivision->currentIndex(), 4);
    QCOMPARE(persistence->currentIndex(), 0);
    QVERIFY(!mathEnabled->isChecked());
    QVERIFY(!triggerEnabled->isChecked());
    QVERIFY(!cursorCheck->isChecked());
    QVERIFY(channelVisible->isChecked());
    QCOMPARE(channelScale->value(), 1.0);
    QCOMPARE(channelOffset->value(), 0.0);
    QVERIFY(channelMarkerCheck->isChecked());
    QCOMPARE(channelMarkerPosition->value(), 30.0);
    QCOMPARE(waveform->measurementMarkerFraction(0), 0.3);
    QCOMPARE(controlTabs->currentIndex(), 0);
    QCOMPARE(viewTabs->currentIndex(), 0);
    QCOMPARE(waveform->timeWindowSeconds(), 0.1);
    QCOMPARE(waveform->timePerDivisionSeconds(), 0.01);
    QCOMPARE(waveform->voltsPerDivision(), 0.25);
    QVERIFY(waveform->isLiveView());
    QVERIFY(!waveform->newestSnapshot(1).isValid());
    QTRY_VERIFY_WITH_TIMEOUT(waveform->newestSnapshot(1).isValid(), 1000);

    QTRY_VERIFY_WITH_TIMEOUT(channelMarkerReadout->text().contains(QStringLiteral("U =")),
                             1000);
    const QPoint markerCenter = waveform->measurementMarkerCenter(0).toPoint();
    const QPoint markerMoved(markerCenter.x() + waveform->width() / 5, markerCenter.y());
    QTest::mousePress(waveform, Qt::LeftButton, Qt::NoModifier, markerCenter);
    QTest::mouseMove(waveform, markerMoved, 20);
    QTest::mouseRelease(waveform, Qt::LeftButton, Qt::NoModifier, markerMoved);
    QVERIFY(waveform->measurementMarkerFraction(0) > 0.45);
    QCOMPARE(channelMarkerPosition->value(),
             100.0 * waveform->measurementMarkerFraction(0));

    window.close();
}

void UiSmokeTest::cursorReadoutsUseXAxisReference()
{
    WaveformWidget waveform;
    DataStreamInfo info;
    info.sourceName = QStringLiteral("axis test");
    info.channelCount = 1;
    info.sampleRateHz = 1000.0;
    info.channelNames = {QStringLiteral("CH1")};
    info.unit = QStringLiteral("V");
    waveform.setStreamInfo(info);
    waveform.setTimePerDivision(0.01);
    waveform.setVoltsPerDivision(0.25);

    auto block = QSharedPointer<DataBlock>::create();
    block->firstSampleIndex = 0;
    block->channelCount = 1;
    block->frameCount = 1000;
    block->sampleRateHz = 1000.0;
    block->interleaved.fill(0.0F, 1000);
    waveform.appendBlock(block);

    QSignalSpy readouts(&waveform, &WaveformWidget::cursorReadoutChanged);
    waveform.setCursorsVisible(true);
    QCOMPARE(readouts.count(), 1);
    QList<QVariant> live = readouts.takeFirst();
    QVERIFY(std::abs(live[0].toDouble() + 0.07) < 1e-12);
    QVERIFY(std::abs(live[1].toDouble() + 0.03) < 1e-12);
    QVERIFY(std::abs(live[2].toDouble() - 0.04) < 1e-12);
    QVERIFY(std::abs(live[3].toDouble() - 0.25) < 1e-12);
    QVERIFY(std::abs(live[4].toDouble() + 0.25) < 1e-12);
    QVERIFY(std::abs(live[5].toDouble() + 0.5) < 1e-12);

    waveform.setCursorsVisible(false);
    waveform.freezeViewAt(950, 0.5);
    waveform.setCursorsVisible(true);
    QCOMPARE(readouts.count(), 1);
    const QList<QVariant> triggered = readouts.takeFirst();
    QVERIFY(std::abs(triggered[0].toDouble() + 0.02) < 1e-12);
    QVERIFY(std::abs(triggered[1].toDouble() - 0.02) < 1e-12);
    QVERIFY(std::abs(triggered[2].toDouble() - 0.04) < 1e-12);
}

void UiSmokeTest::boardControlsUseExternalVolts()
{
    SerialDataSource source;
    BoardLinkWidget controls(&source);

    auto *amplitude = controls.findChild<QDoubleSpinBox *>(
        QStringLiteral("dacAmplitudeSpin"));
    auto *offset = controls.findChild<QDoubleSpinBox *>(
        QStringLiteral("dacOffsetSpin"));
    QVERIFY(amplitude);
    QVERIFY(offset);
    QCOMPARE(amplitude->value(), 3.9);
    QCOMPARE(offset->value(), 0.0);

    QSignalSpy request(&controls, &BoardLinkWidget::generatorRequested);
    QVERIFY(QMetaObject::invokeMethod(&controls, "applyGeneratorSettings",
                                      Qt::DirectConnection));
    QCOMPARE(request.count(), 1);
    const QList<QVariant> arguments = request.takeFirst();
    QCOMPARE(arguments.at(0).toString(), QStringLiteral("SIN"));
    QCOMPARE(arguments.at(1).toInt(), 1000000);
    QCOMPARE(arguments.at(2).toInt(), 99);
    QCOMPARE(arguments.at(3).toInt(), 128);

    offset->setValue(4.0);
    QCOMPARE(amplitude->maximum(), 1.0);

    auto *waveform = controls.findChild<QComboBox *>(QStringLiteral("dacWaveformCombo"));
    auto *formula = controls.findChild<QLineEdit *>(QStringLiteral("dacFormulaEdit"));
    QVERIFY(waveform);
    QVERIFY(formula);
    waveform->setCurrentIndex(waveform->findData(QStringLiteral("LUT")));
    formula->setText(QStringLiteral("1.0*sin(x)+0.5*tri(2*x)+0.2"));
    QSignalSpy customRequest(&controls, &BoardLinkWidget::customWaveformRequested);
    QVERIFY(QMetaObject::invokeMethod(&controls, "applyGeneratorSettings",
                                      Qt::DirectConnection));
    QCOMPARE(customRequest.count(), 1);
    const QList<QVariant> customArguments = customRequest.takeFirst();
    QCOMPARE(customArguments.at(0).toByteArray().size(), 256);
    QCOMPARE(customArguments.at(1).toInt(), 1000000);
}

QTEST_MAIN(UiSmokeTest)

#include "test_uismoke.moc"
