#pragma once

#include "acquisition/datablock.h"
#include "plotting/sampleringbuffer.h"
#include "plotting/scopeaxistransform.h"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QLineF>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QTimer>
#include <QWidget>

class WaveformWidget final : public QWidget
{
    Q_OBJECT

public:
    enum class PersistenceMode {
        Off,
        Short,
        Long,
        Infinite
    };
    Q_ENUM(PersistenceMode)

    explicit WaveformWidget(QWidget *parent = nullptr);

    [[nodiscard]] double sampleRateHz() const { return m_streamInfo.sampleRateHz; }
    [[nodiscard]] int channelCount() const { return m_streamInfo.channelCount; }
    [[nodiscard]] double timeWindowSeconds() const { return m_timeWindowSeconds; }
    [[nodiscard]] double timePerDivisionSeconds() const
        { return m_timeWindowSeconds / ScopeAxisTransform::HorizontalDivisions; }
    [[nodiscard]] double voltsPerDivision() const
        { return 2.0 * m_amplitudeRange / ScopeAxisTransform::VerticalDivisions; }
    [[nodiscard]] int renderRateLimitFps() const { return m_renderRateLimitFps; }
    [[nodiscard]] double channelOffset(int channel) const;
    [[nodiscard]] bool measurementMarkerEnabled(int channel) const;
    [[nodiscard]] double measurementMarkerFraction(int channel) const;
    [[nodiscard]] QPointF measurementMarkerCenter(int channel) const;
    [[nodiscard]] bool isLiveView() const { return m_liveView; }
    [[nodiscard]] DataBlock visibleSnapshot(int maximumFrames = 1000000) const;
    [[nodiscard]] DataBlock newestSnapshot(int maximumFrames) const;

public slots:
    void setStreamInfo(const DataStreamInfo &info);
    void appendBlock(const DataBlockPtr &block);
    void appendMathBlock(const DataBlock &block);
    void clearMath();
    void clear();
    void setTimeWindowSeconds(double seconds);
    void setAmplitudeRange(double positiveRange);
    void setTimePerDivision(double seconds);
    void setVoltsPerDivision(double volts);
    void setChannelVisible(int channel, bool visible);
    void setChannelScale(int channel, double scale);
    void setChannelOffset(int channel, double offset);
    void setMeasurementMarkerEnabled(int channel, bool enabled);
    void setMeasurementMarkerFraction(int channel, double fraction);
    void setPersistenceMode(PersistenceMode mode);
    void setRunning(bool running);
    void setCursorsVisible(bool visible);
    void setMathVisible(bool visible);
    void returnToLive();
    void freezeViewAt(qint64 sampleIndex, double horizontalPosition = 0.5);

signals:
    void framesAccepted(qint64 frameCount);
    void renderRateChanged(double framesPerSecond);
    void inputRejected(const QString &message);
    void timeWindowChanged(double seconds);
    void amplitudeRangeChanged(double range);
    void channelOffsetChanged(int channel, double offset);
    void measurementMarkerChanged(int channel,
                                  bool valid,
                                  double positionPercent,
                                  double relativeTimeSeconds,
                                  double voltage);
    void liveViewChanged(bool live);
    void cursorReadoutChanged(double time1Seconds,
                              double time2Seconds,
                              double deltaTimeSeconds,
                              double voltage1,
                              double voltage2,
                              double deltaVoltage);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    QSize minimumSizeHint() const override;

private:
    void ensureBufferConfiguration();
    void drawGrid(QPainter &painter, const QRectF &plotRect);
    void drawWaveforms(QPainter &painter, const QRectF &plotRect);
    void drawMeasurementMarkers(QPainter &painter, const QRectF &plotRect);
    [[nodiscard]] double mapValueToY(int channel, float value, const QRectF &plotRect) const;
    [[nodiscard]] QColor channelColor(int channel) const;
    [[nodiscard]] QRectF plotAreaRect() const;
    [[nodiscard]] QPointF measurementMarkerPoint(int channel,
                                                 bool *valid = nullptr) const;
    [[nodiscard]] qint64 measurementMarkerSample(int channel) const;
    [[nodiscard]] int measurementMarkerAt(const QPointF &position) const;
    [[nodiscard]] qint64 currentViewRightSample() const;
    [[nodiscard]] qint64 timeReferenceSample() const;
    [[nodiscard]] ScopeAxisTransform axisTransform() const;
    [[nodiscard]] QString formatTime(double seconds) const;
    [[nodiscard]] QString formatVoltage(double volts) const;
    void clearPersistence();
    void drawCursors(QPainter &painter, const QRectF &plotRect);
    void initializeCursorsIfNeeded();
    void emitCursorReadout();
    void emitMeasurementMarkerReadout(int channel);
    void emitAllMeasurementMarkerReadouts();

    DataStreamInfo m_streamInfo;
    SampleRingBuffer m_buffer;
    SampleRingBuffer m_mathBuffer;
    QVector<quint8> m_channelVisible;
    QVector<double> m_channelScales;
    QVector<double> m_channelOffsets;
    QVector<quint8> m_measurementMarkerEnabled;
    QVector<double> m_measurementMarkerFractions;
    QTimer m_repaintTimer;
    QElapsedTimer m_fpsClock;
    QElapsedTimer m_markerReadoutClock;
    double m_timeWindowSeconds = 0.1;
    double m_amplitudeRange = 1.0;
    double m_maxHistorySeconds = 5.0;
    int m_paintCounter = 0;
    int m_renderRateLimitFps = 30;
    bool m_lowPowerMode = false;
    bool m_dirty = true;
    bool m_running = false;
    bool m_liveView = true;
    bool m_dragging = false;
    bool m_pointerInsidePlot = false;
    int m_cursorDrag = 0;
    int m_measurementMarkerDrag = -1;
    QPointF m_lastMousePosition;
    QPointF m_pointerPosition;
    qint64 m_viewRightSample = 0;
    qint64 m_triggerMarkerSample = -1;
    PersistenceMode m_persistenceMode = PersistenceMode::Off;
    QImage m_persistenceLayer;
    QPolygonF m_pointScratch;
    QVector<QLineF> m_lineScratch;
    QPainterPath m_pathScratch;
    bool m_mathVisible = false;
    bool m_cursorsVisible = false;
    bool m_cursorsInitialized = false;
    qint64 m_timeCursors[2] = {0, 0};
    double m_voltageCursors[2] = {0.25, -0.25};
};
