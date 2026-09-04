#include "acquisition/serialdatasource.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QtTest>

/*
 * Hardware loopback check for the GUI high-speed acquisition path.
 *
 * Requires: CH347 adapter + STM32F103 board with the HS firmware, DLL path
 * in SCOPE_CH347_DLL. Opens the CH347 SPI transport, switches the data
 * source into high-speed mode and verifies DataBlocks of AD9280 samples
 * arrive with the expected single-channel layout.
 *
 * Run: ctest -R hs_hardware   (skipped when the DLL/board is unavailable)
 */
class HsHardwareTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void highSpeedStreamDeliversBlocks();
};

void HsHardwareTest::initTestCase()
{
    qRegisterMetaType<DataStreamInfo>();
    qRegisterMetaType<DataBlock>();
    qRegisterMetaType<DataBlockPtr>();
}

void HsHardwareTest::highSpeedStreamDeliversBlocks()
{
    const QString dll = qEnvironmentVariable("SCOPE_CH347_DLL");
    if (dll.isEmpty()) {
        QSKIP("SCOPE_CH347_DLL not set; skipping hardware test");
    }

    SerialDataSource source;
    source.setTransportMode(
        static_cast<int>(SerialDataSource::TransportMode::Ch347Spi));
    source.setCh347DllPath(dll);
    source.setCh347DeviceIndex(0);
    source.setCh347ClockIndex(3);

    int blockCount = 0;
    qint64 totalSamples = 0;
    double lastSampleRate = 0.0;
    bool sawChannelCountOne = false;
    QObject::connect(&source, &SerialDataSource::blockReady,
                     [&](const DataBlockPtr &block) {
        if (!block || !block->isValid()) {
            return;
        }
        ++blockCount;
        totalSamples += block->frameCount;
        lastSampleRate = block->sampleRateHz;
        if (block->channelCount == 1) {
            sawChannelCountOne = true;
        }
    });

    QElapsedTimer clock;
    clock.start();
    source.start();
    source.setHighSpeedMode(true);
    QTRY_VERIFY_WITH_TIMEOUT(source.highSpeedMode(), 2000);

    QTRY_VERIFY_WITH_TIMEOUT(blockCount >= 5, 10000);
    const int blocksAtTimeout = blockCount;

    source.setHighSpeedMode(false);
    source.stop();

    qInfo("HS blocks=%d samples=%lld rate=%.0f Hz ch1=%s",
          blocksAtTimeout, static_cast<long long>(totalSamples),
          lastSampleRate, sawChannelCountOne ? "yes" : "no");
    QVERIFY(blocksAtTimeout >= 5);
    QVERIFY(totalSamples > 0);
    QVERIFY(lastSampleRate > 0.0);
    QVERIFY(sawChannelCountOne);
}

QTEST_GUILESS_MAIN(HsHardwareTest)

#include "test_hs_hardware.moc"
