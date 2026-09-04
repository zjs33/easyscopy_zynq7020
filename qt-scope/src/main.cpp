#include "acquisition/datablock.h"
#include "acquisition/dmacapturedatasource.h"
#include "acquisition/simulationdatasource.h"
#include "acquisition/filereplaydatasource.h"
#include "acquisition/idatasource.h"
#include "core/compilercompat.h"
#include "processing/fftengine.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QMessageBox>
#include <QMetaObject>
#include <QPixmap>
#include <QSharedMemory>
#include <QThread>
#include <QTabWidget>
#include <QTimer>

#include <algorithm>

namespace {
int boundedEnvironmentInteger(const char *name, int fallback,
                              int minimum, int maximum)
{
    bool ok = false;
    const int requested = qEnvironmentVariableIntValue(name, &ok);
    return ok ? clampValue(requested, minimum, maximum) : fallback;
}
}

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("AC880 Zynq-7020 Scope"));
    QApplication::setOrganizationName(QStringLiteral("AC880"));
    QApplication::setStyle(QStringLiteral("Fusion"));

    /* Keep a single UI instance. QSharedMemory is a kernel object and is
     * released automatically even if the previous instance crashed. */
    QSharedMemory instanceMemory(QStringLiteral("AC880Zynq7020ScopeSingleInstance"));
    const bool embeddedMode = qEnvironmentVariableIntValue("ZYNQ_SCOPE_EMBEDDED") != 0;
    if (!embeddedMode && !instanceMemory.create(1)) {
        qWarning("single-instance create failed: %d %s",
                 static_cast<int>(instanceMemory.error()),
                 qPrintable(instanceMemory.errorString()));
        QMessageBox::warning(
            nullptr, QStringLiteral("AC880 Zynq-7020 示波器"),
            QStringLiteral("另一个实例已经在运行。\n"
                           "请先关闭旧窗口。"));
        return 0;
    }

#ifdef Q_OS_WIN
    const int fontId = QFontDatabase::addApplicationFont(
        QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
    const QStringList fontFamilies = QFontDatabase::applicationFontFamilies(fontId);
    QFont applicationFont(fontFamilies.isEmpty()
                              ? QStringLiteral("Microsoft YaHei UI")
                              : fontFamilies.first(), 9);
#else
    QFont applicationFont(QStringLiteral("Noto Sans CJK SC"), 9);
#endif
    application.setFont(applicationFont);

    qRegisterMetaType<DataStreamInfo>("DataStreamInfo");
    qRegisterMetaType<DataBlock>("DataBlock");
    qRegisterMetaType<DataBlockPtr>("DataBlockPtr");
    qRegisterMetaType<FftConfig>("FftConfig");
    qRegisterMetaType<SpectrumResult>("SpectrumResult");

    QThread acquisitionThread;
    acquisitionThread.setObjectName(QStringLiteral("AcquisitionThread"));

    const QStringList arguments = application.arguments();
    const bool forceSimulation = arguments.contains(QStringLiteral("--simulation"));
    const int captureArgument = arguments.indexOf(QStringLiteral("--capture"));
    const int deviceArgument = arguments.indexOf(QStringLiteral("--dma-device"));
    QString capturePath = qEnvironmentVariable("AC880_CAPTURE_FILE").trimmed();
    QString devicePath = qEnvironmentVariable("AC880_CAPTURE_DEVICE").trimmed();
    if (captureArgument >= 0 && captureArgument + 1 < arguments.size()) {
        capturePath = arguments.at(captureArgument + 1);
    }
    if (deviceArgument >= 0 && deviceArgument + 1 < arguments.size()) {
        devicePath = arguments.at(deviceArgument + 1);
    }

    DmaCaptureDataSource *dmaSource = nullptr;
    IDataSource *dataSource = nullptr;
    if (!forceSimulation && !capturePath.isEmpty()) {
        dmaSource = new DmaCaptureDataSource;
        dmaSource->setPath(capturePath);
        dmaSource->setSampleRate(50000000.0);
        dataSource = dmaSource;
    } else if (!forceSimulation && !devicePath.isEmpty()) {
        dmaSource = new DmaCaptureDataSource;
        dmaSource->setPath(devicePath);
        dmaSource->setSampleRate(50000000.0);
        dataSource = dmaSource;
    }

    if (!dataSource) {
        const int simulationSampleRate = boundedEnvironmentInteger(
            "ZYNQ_SCOPE_SIM_SAMPLE_RATE", 1000000, 1000, 5000000);
        const int simulationBlockMs = boundedEnvironmentInteger(
            "ZYNQ_SCOPE_SIM_BLOCK_MS", 20, 5, 1000);
        dataSource = new SimulationDataSource(
            simulationSampleRate, 1, simulationBlockMs);
    }
    auto *replaySource = new FileReplayDataSource;
    dataSource->moveToThread(&acquisitionThread);
    replaySource->moveToThread(&acquisitionThread);
    QObject::connect(&acquisitionThread, &QThread::finished,
                     dataSource, &QObject::deleteLater);
    QObject::connect(&acquisitionThread, &QThread::finished,
                     replaySource, &QObject::deleteLater);
    acquisitionThread.start();

    MainWindow window(dataSource, replaySource, nullptr);
    const bool embeddedFullScreen = application.arguments().contains(
        QStringLiteral("--fullscreen"))
        || qEnvironmentVariableIntValue("ZYNQ_SCOPE_FULLSCREEN") != 0;
    if (embeddedFullScreen) {
        window.showFullScreen();
    } else {
        window.show();
    }

    const QString environmentScreenshot = qEnvironmentVariable("ZYNQ_SCOPE_SCREENSHOT");
    const int screenshotArgument = application.arguments().indexOf(
        QStringLiteral("--screenshot"));
    if (!environmentScreenshot.isEmpty()) {
        QTimer::singleShot(1500, &application,
                           [&application, &window, environmentScreenshot] {
            bool tabOk = false;
            const int tabIndex = qEnvironmentVariableIntValue("ZYNQ_SCOPE_TAB", &tabOk);
            if (tabOk) {
                if (auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("controlTabs"))) {
                    tabs->setCurrentIndex(tabIndex);
                }
            }
            bool viewTabOk = false;
            const int viewTabIndex = qEnvironmentVariableIntValue(
                "ZYNQ_SCOPE_VIEW_TAB", &viewTabOk);
            if (viewTabOk) {
                if (auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("viewTabs"))) {
                    tabs->setCurrentIndex(viewTabIndex);
                }
            }
            if (qEnvironmentVariableIsSet("ZYNQ_SCOPE_MATH")) {
                if (auto *mathCheck = window.findChild<QCheckBox *>(
                        QStringLiteral("mathEnabledCheck"))) {
                    mathCheck->setChecked(true);
                }
            }
            if (qEnvironmentVariableIsSet("ZYNQ_SCOPE_CURSOR")) {
                if (auto *cursorCheck = window.findChild<QCheckBox *>(
                        QStringLiteral("cursorCheck"))) {
                    cursorCheck->setChecked(true);
                }
            }
            window.grab().save(environmentScreenshot);
            application.quit();
        });
    } else if (screenshotArgument >= 0
        && screenshotArgument + 1 < application.arguments().size()) {
        const QString screenshotPath = application.arguments().at(screenshotArgument + 1);
        QTimer::singleShot(1500, &application, [&application, &window, screenshotPath] {
            window.grab().save(screenshotPath);
            application.quit();
        });
    } else if (application.arguments().contains(QStringLiteral("--smoke-test"))
               || qEnvironmentVariableIsSet("ZYNQ_SCOPE_SMOKE_TEST")) {
        bool durationOk = false;
        const int requestedDuration = qEnvironmentVariableIntValue(
            "ZYNQ_SCOPE_SMOKE_MS", &durationOk);
        const int duration = durationOk ? clampValue(requestedDuration, 100, 3600000)
                                        : 2000;
        QTimer::singleShot(duration, &application, &QCoreApplication::quit);
    }

    const int result = application.exec();
    qWarning("Qt event loop exited: %d", result);

    QMetaObject::invokeMethod(dataSource, "stop", Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(replaySource, "stop", Qt::BlockingQueuedConnection);
    acquisitionThread.quit();
    acquisitionThread.wait(3000);
    return result;
}
