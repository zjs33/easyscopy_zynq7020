#include "plotting/waveformwidget.h"

#include "core/compilercompat.h"

#include <QFontMetrics>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPolygonF>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double kTimePerDivisionValues[] = {
    0.0001, 0.0005, 0.001, 0.005, 0.01, 0.05, 0.1, 0.2, 0.5
};
constexpr double kVoltsPerDivisionValues[] = {
    0.01, 0.02, 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0
};

int boundedEnvironmentInteger(const char *name, int fallback,
                              int minimum, int maximum)
{
    bool ok = false;
    const int requested = qEnvironmentVariableIntValue(name, &ok);
    return ok ? clampValue(requested, minimum, maximum) : fallback;
}

QPointF mousePosition(const QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}

QPointF wheelPosition(const QWheelEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->posF();
#endif
}

template <std::size_t Size>
double adjacentScale(double current, int direction, const double (&values)[Size])
{
    int nearest = 0;
    double bestDistance = std::abs(current - values[0]);
    for (int i = 1; i < static_cast<int>(Size); ++i) {
        const double distance = std::abs(current - values[i]);
        if (distance < bestDistance) {
            nearest = i;
            bestDistance = distance;
        }
    }
    return values[clampValue(nearest + direction, 0, static_cast<int>(Size) - 1)];
}
}

WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setAutoFillBackground(false);
    setMinimumSize(320, 200);

    m_lowPowerMode = qEnvironmentVariableIntValue("ZYNQ_SCOPE_LOW_POWER") != 0;
    m_renderRateLimitFps = boundedEnvironmentInteger(
        "ZYNQ_SCOPE_RENDER_FPS", 30, 5, 60);
    m_repaintTimer.setInterval(std::max(1, 1000 / m_renderRateLimitFps));
    m_repaintTimer.setTimerType(m_lowPowerMode ? Qt::CoarseTimer : Qt::PreciseTimer);
    connect(&m_repaintTimer, &QTimer::timeout, this, [this] {
        if (m_dirty) {
            update();
        }
    });
    m_repaintTimer.start();
    m_fpsClock.start();
    m_markerReadoutClock.start();
}

void WaveformWidget::setStreamInfo(const DataStreamInfo &info)
{
    if (!info.isValid()) {
        emit inputRejected(QStringLiteral("收到无效的数据流描述"));
        return;
    }

    const bool layoutChanged = info.channelCount != m_streamInfo.channelCount
        || !qFuzzyCompare(info.sampleRateHz, m_streamInfo.sampleRateHz);
    m_streamInfo = info;

    if (layoutChanged) {
        m_channelVisible.fill(1, info.channelCount);
        m_channelScales.fill(1.0, info.channelCount);
        m_channelOffsets.fill(0.0, info.channelCount);
        m_measurementMarkerEnabled.fill(1, info.channelCount);
        m_measurementMarkerFractions.resize(info.channelCount);
        for (int channel = 0; channel < info.channelCount; ++channel) {
            m_measurementMarkerFractions[channel] = info.channelCount == 1
                ? 0.5
                : 0.3 + 0.4 * channel / std::max(1, info.channelCount - 1);
        }
        ensureBufferConfiguration();
        m_cursorsInitialized = false;
    }
    m_dirty = true;
}

void WaveformWidget::appendBlock(const DataBlockPtr &block)
{
    if (!block || !block->isValid()) {
        emit inputRejected(QStringLiteral("收到无效的数据块"));
        return;
    }

    if (!m_streamInfo.isValid()) {
        DataStreamInfo inferred;
        inferred.sourceName = QStringLiteral("外部数据源");
        inferred.channelCount = block->channelCount;
        inferred.sampleRateHz = block->sampleRateHz;
        inferred.unit = QStringLiteral("V");
        for (int channel = 0; channel < inferred.channelCount; ++channel) {
            inferred.channelNames.append(QStringLiteral("CH%1").arg(channel + 1));
        }
        setStreamInfo(inferred);
    }

    if (block->channelCount != m_streamInfo.channelCount
        || !qFuzzyCompare(block->sampleRateHz, m_streamInfo.sampleRateHz)) {
        emit inputRejected(QStringLiteral("数据块格式与当前数据流不匹配"));
        return;
    }

    const qint64 previousNewestSample = m_buffer.newestSampleIndexExclusive();
    if (!m_buffer.append(*block)) {
        emit inputRejected(QStringLiteral("实时显示缓冲区拒绝了数据块"));
        return;
    }

    if (m_liveView) {
        m_viewRightSample = m_buffer.newestSampleIndexExclusive();
        if (m_cursorsInitialized && previousNewestSample > 0) {
            const qint64 sampleAdvance = m_buffer.newestSampleIndexExclusive()
                - previousNewestSample;
            m_timeCursors[0] += sampleAdvance;
            m_timeCursors[1] += sampleAdvance;
        }
    }
    if (m_cursorsVisible && !m_cursorsInitialized) {
        initializeCursorsIfNeeded();
        emitCursorReadout();
    }

    m_dirty = true;
    emit framesAccepted(block->frameCount);
}

void WaveformWidget::appendMathBlock(const DataBlock &block)
{
    if (!block.isValid() || block.channelCount != 1
        || !qFuzzyCompare(block.sampleRateHz, m_streamInfo.sampleRateHz)) {
        return;
    }
    if (m_mathBuffer.append(block)) {
        m_dirty = true;
    }
}

void WaveformWidget::clearMath()
{
    m_mathBuffer.clear();
    clearPersistence();
    m_dirty = true;
}

void WaveformWidget::clear()
{
    m_buffer.clear();
    m_mathBuffer.clear();
    m_viewRightSample = 0;
    m_triggerMarkerSample = -1;
    m_cursorsInitialized = false;
    clearPersistence();
    m_dirty = true;
    update();
    emitAllMeasurementMarkerReadouts();
}

void WaveformWidget::setTimeWindowSeconds(double seconds)
{
    m_timeWindowSeconds = clampValue(seconds, 0.001, m_maxHistorySeconds);
    clearPersistence();
    m_dirty = true;
    emit timeWindowChanged(m_timeWindowSeconds);
}

void WaveformWidget::setAmplitudeRange(double positiveRange)
{
    m_amplitudeRange = std::max(0.001, positiveRange);
    clearPersistence();
    m_dirty = true;
    emit amplitudeRangeChanged(m_amplitudeRange);
}

void WaveformWidget::setTimePerDivision(double seconds)
{
    setTimeWindowSeconds(seconds * ScopeAxisTransform::HorizontalDivisions);
}

void WaveformWidget::setVoltsPerDivision(double volts)
{
    setAmplitudeRange(volts * ScopeAxisTransform::VerticalDivisions * 0.5);
}

void WaveformWidget::setChannelVisible(int channel, bool visible)
{
    if (channel < 0 || channel >= m_channelVisible.size()) {
        return;
    }
    m_channelVisible[channel] = visible ? 1 : 0;
    clearPersistence();
    m_dirty = true;
}

void WaveformWidget::setChannelScale(int channel, double scale)
{
    if (channel < 0 || channel >= m_channelScales.size()) {
        return;
    }
    m_channelScales[channel] = clampValue(scale, 0.01, 100.0);
    clearPersistence();
    m_dirty = true;
}

void WaveformWidget::setChannelOffset(int channel, double offset)
{
    if (channel < 0 || channel >= m_channelOffsets.size()) {
        return;
    }
    const double boundedOffset = clampValue(offset, -1000.0, 1000.0);
    if (qFuzzyCompare(m_channelOffsets[channel] + 1.0, boundedOffset + 1.0)) {
        return;
    }
    m_channelOffsets[channel] = boundedOffset;
    clearPersistence();
    m_dirty = true;
    emit channelOffsetChanged(channel, boundedOffset);
}

double WaveformWidget::channelOffset(int channel) const
{
    if (channel < 0 || channel >= m_channelOffsets.size()) {
        return 0.0;
    }
    return m_channelOffsets[channel];
}

bool WaveformWidget::measurementMarkerEnabled(int channel) const
{
    return channel >= 0 && channel < m_measurementMarkerEnabled.size()
        && m_measurementMarkerEnabled[channel] != 0;
}

double WaveformWidget::measurementMarkerFraction(int channel) const
{
    if (channel < 0 || channel >= m_measurementMarkerFractions.size()) {
        return 0.0;
    }
    return m_measurementMarkerFractions[channel];
}

QPointF WaveformWidget::measurementMarkerCenter(int channel) const
{
    return measurementMarkerPoint(channel);
}

void WaveformWidget::setMeasurementMarkerEnabled(int channel, bool enabled)
{
    if (channel < 0 || channel >= m_measurementMarkerEnabled.size()) {
        return;
    }
    m_measurementMarkerEnabled[channel] = enabled ? 1 : 0;
    m_dirty = true;
    emitMeasurementMarkerReadout(channel);
}

void WaveformWidget::setMeasurementMarkerFraction(int channel, double fraction)
{
    if (channel < 0 || channel >= m_measurementMarkerFractions.size()) {
        return;
    }
    const double bounded = clampValue(fraction, 0.0, 1.0);
    m_measurementMarkerFractions[channel] = std::round(bounded * 1000.0) / 1000.0;
    m_dirty = true;
    emitMeasurementMarkerReadout(channel);
}

void WaveformWidget::setPersistenceMode(PersistenceMode mode)
{
    if (m_persistenceMode == mode) {
        return;
    }
    m_persistenceMode = mode;
    clearPersistence();
    m_dirty = true;
}

void WaveformWidget::setRunning(bool running)
{
    m_running = running;
    m_dirty = true;
}

void WaveformWidget::setCursorsVisible(bool visible)
{
    m_cursorsVisible = visible;
    if (visible) {
        initializeCursorsIfNeeded();
        emitCursorReadout();
    }
    m_dirty = true;
}

void WaveformWidget::setMathVisible(bool visible)
{
    m_mathVisible = visible;
    clearPersistence();
    m_dirty = true;
}

void WaveformWidget::returnToLive()
{
    if (!m_liveView) {
        m_liveView = true;
        m_viewRightSample = m_buffer.newestSampleIndexExclusive();
        m_triggerMarkerSample = -1;
        m_cursorsInitialized = false;
        if (m_cursorsVisible) {
            initializeCursorsIfNeeded();
            emitCursorReadout();
        }
        clearPersistence();
        m_dirty = true;
        emit liveViewChanged(true);
    }
}

void WaveformWidget::freezeViewAt(qint64 sampleIndex, double horizontalPosition)
{
    const qint64 frames = qCeil(m_timeWindowSeconds * m_streamInfo.sampleRateHz);
    const double position = clampValue(horizontalPosition, 0.0, 1.0);
    m_viewRightSample = sampleIndex + qRound64((1.0 - position) * frames);
    m_triggerMarkerSample = sampleIndex;
    m_liveView = false;
    m_cursorsInitialized = false;
    if (m_cursorsVisible) {
        initializeCursorsIfNeeded();
        emitCursorReadout();
    }
    clearPersistence();
    m_dirty = true;
    emit liveViewChanged(false);
}

DataBlock WaveformWidget::visibleSnapshot(int maximumFrames) const
{
    if (!m_streamInfo.isValid()) {
        return {};
    }
    const int frameCount = std::min(std::max(1, maximumFrames),
                                    qCeil(m_timeWindowSeconds * m_streamInfo.sampleRateHz));
    const qint64 right = currentViewRightSample();
    return m_buffer.snapshot(right - frameCount, frameCount,
                             m_streamInfo.sampleRateHz);
}

DataBlock WaveformWidget::newestSnapshot(int maximumFrames) const
{
    if (!m_streamInfo.isValid() || maximumFrames <= 0) {
        return {};
    }
    const int frames = std::min(maximumFrames, m_buffer.sizeFrames());
    const qint64 right = m_buffer.newestSampleIndexExclusive();
    return m_buffer.snapshot(right - frames, frames, m_streamInfo.sampleRateHz);
}

void WaveformWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), QColor(255, 255, 255));
    painter.setRenderHint(QPainter::TextAntialiasing, !m_lowPowerMode);

    const QRectF plotRect = plotAreaRect();
    drawGrid(painter, plotRect);
    if (m_persistenceMode == PersistenceMode::Off) {
        drawWaveforms(painter, plotRect);
    } else {
        if (m_persistenceLayer.size() != size()) {
            m_persistenceLayer = QImage(size(), QImage::Format_ARGB32_Premultiplied);
            m_persistenceLayer.fill(Qt::transparent);
        }
        if (m_dirty) {
            QPainter persistencePainter(&m_persistenceLayer);
            int retention = 255;
            if (m_persistenceMode == PersistenceMode::Short) {
                retention = 205;
            } else if (m_persistenceMode == PersistenceMode::Long) {
                retention = 240;
            }
            if (retention < 255) {
                persistencePainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                persistencePainter.fillRect(m_persistenceLayer.rect(),
                                            QColor(255, 255, 255, retention));
                persistencePainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            }
            drawWaveforms(persistencePainter, plotRect);
        }
        painter.drawImage(QPoint(0, 0), m_persistenceLayer);
    }

    painter.setPen(QColor(31, 48, 64));
    const QString sourceName = m_streamInfo.sourceName.isEmpty()
        ? QStringLiteral("等待数据源") : m_streamInfo.sourceName;
    painter.drawText(QRectF(12.0, 8.0, width() * 0.58, 22.0),
                     Qt::AlignLeft | Qt::AlignVCenter, sourceName);

    const QString stateText = m_running ? QStringLiteral("● 采集中")
                                        : QStringLiteral("■ 已暂停");
    painter.setPen(m_running ? QColor(24, 125, 72) : QColor(166, 92, 0));
    painter.drawText(QRectF(width() * 0.62, 8.0, width() * 0.35 - 12.0, 22.0),
                     Qt::AlignRight | Qt::AlignVCenter, stateText);

    if (!m_liveView) {
        painter.setPen(QColor(166, 92, 0));
        painter.drawText(QRectF(width() * 0.42, 8.0, width() * 0.2, 22.0),
                         Qt::AlignCenter, QStringLiteral("历史浏览 · 双击回实时"));
    }

    if (m_triggerMarkerSample >= 0 && m_streamInfo.isValid()) {
        const qint64 right = currentViewRightSample();
        const qint64 frames = qCeil(m_timeWindowSeconds * m_streamInfo.sampleRateHz);
        const qint64 first = right - frames;
        if (m_triggerMarkerSample >= first && m_triggerMarkerSample <= right) {
            const double ratio = static_cast<double>(m_triggerMarkerSample - first)
                / std::max<qint64>(1, right - first);
            const double x = plotRect.left() + ratio * plotRect.width();
            painter.setPen(QPen(QColor(199, 90, 0, 190), 1.0, Qt::DashLine));
            painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
            QPolygonF marker;
            marker << QPointF(x - 6.0, plotRect.top())
                   << QPointF(x + 6.0, plotRect.top())
                   << QPointF(x, plotRect.top() + 8.0);
            painter.setBrush(QColor(199, 90, 0));
            painter.setPen(Qt::NoPen);
            painter.drawPolygon(marker);
        }
    }

    drawMeasurementMarkers(painter, plotRect);
    // Marker values change with a rolling waveform, but updating the control
    // labels on every paint causes avoidable relayout work on linuxfb.
    if (m_markerReadoutClock.elapsed() >= 100) {
        emitAllMeasurementMarkerReadouts();
        m_markerReadoutClock.restart();
    }
    drawCursors(painter, plotRect);

    if (m_pointerInsidePlot) {
        const double xFraction = clampValue(
            (m_pointerPosition.x() - plotRect.left()) / plotRect.width(), 0.0, 1.0);
        const double yFraction = clampValue(
            (m_pointerPosition.y() - plotRect.top()) / plotRect.height(), 0.0, 1.0);
        const ScopeAxisTransform axis = axisTransform();
        painter.save();
        painter.setClipRect(plotRect);
        painter.setPen(QPen(QColor(84, 116, 145, 150), 1.0, Qt::DashLine));
        painter.drawLine(QPointF(m_pointerPosition.x(), plotRect.top()),
                         QPointF(m_pointerPosition.x(), plotRect.bottom()));
        painter.drawLine(QPointF(plotRect.left(), m_pointerPosition.y()),
                         QPointF(plotRect.right(), m_pointerPosition.y()));
        painter.restore();
        const QString coordinateText = QStringLiteral("X %1   Y %2")
            .arg(formatTime(axis.relativeTimeAtHorizontalFraction(xFraction)),
                 formatVoltage(axis.voltageAtVerticalFraction(yFraction)));
        const QRectF readoutRect(plotRect.right() - 194.0, plotRect.top() + 7.0,
                                186.0, 23.0);
        painter.fillRect(readoutRect, QColor(247, 250, 252, 235));
        painter.setPen(QColor(31, 48, 64));
        painter.drawText(readoutRect.adjusted(5.0, 0.0, -5.0, 0.0),
                         Qt::AlignRight | Qt::AlignVCenter, coordinateText);
    }

    m_dirty = false;
    ++m_paintCounter;
    const qint64 elapsedMs = m_fpsClock.elapsed();
    if (elapsedMs >= 1000) {
        emit renderRateChanged(1000.0 * m_paintCounter / static_cast<double>(elapsedMs));
        m_paintCounter = 0;
        m_fpsClock.restart();
    }
}

void WaveformWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    clearPersistence();
    m_dirty = true;
}

void WaveformWidget::leaveEvent(QEvent *event)
{
    m_pointerInsidePlot = false;
    m_dirty = true;
    QWidget::leaveEvent(event);
}

void WaveformWidget::wheelEvent(QWheelEvent *event)
{
    const QPointF position = wheelPosition(event);
    if (!plotAreaRect().contains(position)) {
        QWidget::wheelEvent(event);
        return;
    }

    const int direction = event->angleDelta().y() > 0 ? -1 : 1;
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        setVoltsPerDivision(adjacentScale(voltsPerDivision(), direction,
                                           kVoltsPerDivisionValues));
    } else {
        const QRectF area = plotAreaRect();
        const double anchor = clampValue((position.x() - area.left())
                                             / area.width(), 0.0, 1.0);
        const qint64 oldFrames = qCeil(m_timeWindowSeconds * m_streamInfo.sampleRateHz);
        const qint64 oldRight = currentViewRightSample();
        const qint64 anchorSample = oldRight - oldFrames
            + qRound64(anchor * oldFrames);
        setTimePerDivision(adjacentScale(timePerDivisionSeconds(), direction,
                                         kTimePerDivisionValues));
        if (!m_liveView) {
            const qint64 newFrames = qCeil(m_timeWindowSeconds * m_streamInfo.sampleRateHz);
            m_viewRightSample = anchorSample + qRound64((1.0 - anchor) * newFrames);
        }
    }
    event->accept();
}

void WaveformWidget::mousePressEvent(QMouseEvent *event)
{
    const QPointF position = mousePosition(event);
    if (event->button() == Qt::LeftButton) {
        const int markerChannel = measurementMarkerAt(position);
        if (markerChannel >= 0) {
            m_measurementMarkerDrag = markerChannel;
            setCursor(Qt::SizeHorCursor);
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton && plotAreaRect().contains(position)) {
        if (m_cursorsVisible) {
            initializeCursorsIfNeeded();
            const QRectF area = plotAreaRect();
            const qint64 right = currentViewRightSample();
            const qint64 frames = qCeil(m_timeWindowSeconds * m_streamInfo.sampleRateHz);
            const qint64 first = right - frames;
            double bestDistance = 9.0;
            int bestCursor = 0;
            for (int i = 0; i < 2; ++i) {
                const double x = area.left() + area.width()
                    * static_cast<double>(m_timeCursors[i] - first)
                    / std::max<qint64>(1, right - first);
                const double distance = std::abs(position.x() - x);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestCursor = i + 1;
                }
            }
            for (int i = 0; i < 2; ++i) {
                const double y = area.center().y()
                    - m_voltageCursors[i] / m_amplitudeRange * area.height() * 0.5;
                const double distance = std::abs(position.y() - y);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestCursor = i + 3;
                }
            }
            if (bestCursor != 0) {
                m_cursorDrag = bestCursor;
                setCursor(Qt::SizeAllCursor);
                event->accept();
                return;
            }
        }
        m_dragging = true;
        m_lastMousePosition = position;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void WaveformWidget::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF eventPosition = mousePosition(event);
    m_pointerInsidePlot = plotAreaRect().contains(eventPosition);
    m_pointerPosition = eventPosition;
    m_dirty = true;
    if (m_measurementMarkerDrag >= 0) {
        const QRectF area = plotAreaRect();
        const double x = clampValue(eventPosition.x(), area.left(), area.right());
        setMeasurementMarkerFraction(m_measurementMarkerDrag,
                                     (x - area.left()) / area.width());
        event->accept();
        return;
    }
    if (m_cursorDrag != 0 && m_streamInfo.isValid()) {
        const QRectF area = plotAreaRect();
        const QPointF position(
            clampValue(eventPosition.x(), area.left(), area.right()),
            clampValue(eventPosition.y(), area.top(), area.bottom()));
        if (m_cursorDrag <= 2) {
            const qint64 right = currentViewRightSample();
            const qint64 frames = qCeil(m_timeWindowSeconds * m_streamInfo.sampleRateHz);
            const qint64 first = right - frames;
            const double ratio = (position.x() - area.left()) / area.width();
            m_timeCursors[m_cursorDrag - 1] = first
                + qRound64(ratio * std::max<qint64>(1, right - first));
        } else {
            const double fraction = (position.y() - area.top()) / area.height();
            m_voltageCursors[m_cursorDrag - 3] = axisTransform()
                .voltageAtVerticalFraction(fraction);
        }
        emitCursorReadout();
        m_dirty = true;
        event->accept();
        return;
    }
    if (!m_dragging || !m_streamInfo.isValid()) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const double deltaX = eventPosition.x() - m_lastMousePosition.x();
    const double framesPerPixel = m_timeWindowSeconds * m_streamInfo.sampleRateHz
        / std::max(1.0, plotAreaRect().width());
    if (m_liveView) {
        m_liveView = false;
        m_viewRightSample = m_buffer.newestSampleIndexExclusive();
        emit liveViewChanged(false);
    }
    m_viewRightSample -= qRound64(deltaX * framesPerPixel);
    const qint64 minimumRight = m_buffer.oldestSampleIndex() + 2;
    const qint64 maximumRight = m_buffer.newestSampleIndexExclusive();
    m_viewRightSample = clampValue(m_viewRightSample, minimumRight, maximumRight);
    m_lastMousePosition = eventPosition;
    clearPersistence();
    m_dirty = true;
    event->accept();
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_measurementMarkerDrag >= 0) {
        m_measurementMarkerDrag = -1;
        unsetCursor();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_cursorDrag != 0) {
        m_cursorDrag = 0;
        unsetCursor();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void WaveformWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton
        && plotAreaRect().contains(mousePosition(event))) {
        returnToLive();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

QSize WaveformWidget::minimumSizeHint() const
{
    return {640, 360};
}

void WaveformWidget::ensureBufferConfiguration()
{
    if (!m_streamInfo.isValid()) {
        return;
    }

    // Keep the default high-speed view complete while bounding memory on the
    // Zynq CPU. The old fixed 2M-point cap showed only 40 ms at 50 MSa/s,
    // although the default 5 ms/div view spans 50 ms.
    constexpr int displayBufferBudgetBytes = 16 * 1024 * 1024;
    const int bytesPerFrame = std::max(1, m_streamInfo.channelCount)
        * static_cast<int>(sizeof(float));
    const int memoryLimitedFrames = std::max(
        1024, displayBufferBudgetBytes / bytesPerFrame);
    const int requestedHistoryFrames = qCeil(
        m_streamInfo.sampleRateHz * m_maxHistorySeconds);
    const int capacity = clampValue(requestedHistoryFrames,
                                    1024, memoryLimitedFrames);
    m_buffer.configure(m_streamInfo.channelCount, capacity);
    m_mathBuffer.configure(1, capacity);
}

void WaveformWidget::drawGrid(QPainter &painter, const QRectF &plotRect)
{
    painter.save();
    painter.setPen(QPen(QColor(218, 226, 234), 1.0));

    constexpr int verticalDivisions = ScopeAxisTransform::HorizontalDivisions;
    constexpr int horizontalDivisions = ScopeAxisTransform::VerticalDivisions;
    for (int i = 0; i <= verticalDivisions; ++i) {
        const double x = plotRect.left() + plotRect.width() * i / verticalDivisions;
        painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
    }
    for (int i = 0; i <= horizontalDivisions; ++i) {
        const double y = plotRect.top() + plotRect.height() * i / horizontalDivisions;
        painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
    }

    painter.setPen(QPen(QColor(132, 151, 169), 1.0));
    painter.drawRect(plotRect);
    painter.drawLine(QPointF(plotRect.left(), plotRect.center().y()),
                     QPointF(plotRect.right(), plotRect.center().y()));

    painter.setPen(QColor(79, 99, 119));
    const ScopeAxisTransform axis = axisTransform();
    for (int i = 0; i <= horizontalDivisions; ++i) {
        const double y = plotRect.top() + plotRect.height() * i / horizontalDivisions;
        const double voltage = axis.voltageAtVerticalFraction(
            static_cast<double>(i) / horizontalDivisions);
        painter.drawText(QRectF(1.0, y - 9.0, 68.0, 18.0),
                         Qt::AlignRight | Qt::AlignVCenter,
                         formatVoltage(voltage));
    }

    for (int i = 0; i <= verticalDivisions; ++i) {
        const double x = plotRect.left() + plotRect.width() * i / verticalDivisions;
        const double seconds = axis.relativeTimeAtHorizontalFraction(
            static_cast<double>(i) / verticalDivisions);
        painter.drawText(QRectF(x - 40.0, plotRect.bottom() + 7.0, 80.0, 18.0),
                         Qt::AlignHCenter | Qt::AlignTop,
                         formatTime(seconds));
    }

    painter.setPen(QColor(65, 87, 107));
    painter.drawText(QRectF(plotRect.left() + 6.0, plotRect.top() + 5.0,
                            plotRect.width() * 0.5, 20.0),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("X: %1/div · 10 div    Y: %2/div · 8 div")
                         .arg(formatTime(timePerDivisionSeconds()),
                              formatVoltage(voltsPerDivision())));
    painter.restore();
}

void WaveformWidget::drawWaveforms(QPainter &painter, const QRectF &plotRect)
{
    const int bufferedFrames = m_buffer.sizeFrames();
    if (!m_streamInfo.isValid() || bufferedFrames <= 1 || plotRect.width() < 2.0) {
        painter.setPen(QColor(115, 133, 151));
        painter.drawText(plotRect, Qt::AlignCenter,
                         QStringLiteral("等待实时数据…"));
        return;
    }

    const int requestedFrames = qCeil(m_timeWindowSeconds * m_streamInfo.sampleRateHz);
    const qint64 viewRight = currentViewRightSample();
    const qint64 nominalViewFirst = viewRight - requestedFrames;
    const qint64 viewFirst = std::max(m_buffer.oldestSampleIndex(),
                                      nominalViewFirst);
    const int framesToDraw = static_cast<int>(std::max<qint64>(0, viewRight - viewFirst));
    const int pixelWidth = std::max(1, qFloor(plotRect.width()));
    const double samplesPerPixel = static_cast<double>(framesToDraw) / pixelWidth;

    painter.save();
    painter.setClipRect(plotRect);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const int traceCount = m_streamInfo.channelCount
        + ((m_mathVisible && m_mathBuffer.sizeFrames() > 1) ? 1 : 0);
    for (int channel = 0; channel < traceCount; ++channel) {
        const bool mathTrace = channel == m_streamInfo.channelCount;
        if (!mathTrace
            && (channel >= m_channelVisible.size() || m_channelVisible[channel] == 0)) {
            continue;
        }

        painter.setPen(QPen(channelColor(channel), 1.15));
        if (samplesPerPixel <= 2.0) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0)
            m_pathScratch.clear();
#else
            m_pathScratch = QPainterPath();
#endif
            bool segmentStarted = false;
            for (int i = 0; i < framesToDraw; ++i) {
                const double x = plotRect.left() + plotRect.width()
                    * static_cast<double>(viewFirst + i - nominalViewFirst)
                    / std::max(1, requestedFrames);
                const float value = mathTrace
                    ? m_mathBuffer.valueAtSample(0, viewFirst + i)
                    : m_buffer.valueAtSample(channel, viewFirst + i);
                if (!std::isfinite(value)) {
                    segmentStarted = false;
                    continue;
                }
                const QPointF point(x, mapValueToY(channel, value, plotRect));
                if (!segmentStarted) {
                    m_pathScratch.moveTo(point);
                    segmentStarted = true;
                } else {
                    m_pathScratch.lineTo(point);
                }
            }
            painter.drawPath(m_pathScratch);
            continue;
        }

        m_lineScratch.clear();
        m_lineScratch.reserve(pixelWidth);
        for (int pixel = 0; pixel < pixelWidth; ++pixel) {
            int begin = qFloor(pixel * samplesPerPixel);
            int end = qFloor((pixel + 1) * samplesPerPixel);
            end = clampValue(end, begin + 1, framesToDraw);

            const SampleRingBuffer::Range range = mathTrace
                ? m_mathBuffer.rangeForSamples(0, viewFirst + begin, end - begin)
                : m_buffer.rangeForSamples(channel, viewFirst + begin, end - begin);

            if (range.valid) {
                const double sampleOffset = viewFirst - nominalViewFirst
                    + pixel * samplesPerPixel;
                const double x = plotRect.left() + plotRect.width() * sampleOffset
                    / std::max(1, requestedFrames);
                m_lineScratch.append(QLineF(x, mapValueToY(channel, range.minimum, plotRect),
                                           x, mapValueToY(channel, range.maximum, plotRect)));
            }
        }
        painter.drawLines(m_lineScratch);
    }
    painter.restore();
}

double WaveformWidget::mapValueToY(int channel, float value, const QRectF &plotRect) const
{
    const double scale = channel < m_channelScales.size() ? m_channelScales[channel] : 1.0;
    const double offset = channel < m_channelOffsets.size() ? m_channelOffsets[channel] : 0.0;
    const double normalized = clampValue(
        (static_cast<double>(value) * scale + offset) / m_amplitudeRange, -1.0, 1.0);
    return plotRect.center().y() - normalized * plotRect.height() * 0.5;
}

QPointF WaveformWidget::measurementMarkerPoint(int channel, bool *valid) const
{
    if (valid) {
        *valid = false;
    }
    const QRectF area = plotAreaRect();
    if (!measurementMarkerEnabled(channel) || !m_streamInfo.isValid()
        || channel >= m_buffer.channelCount() || m_buffer.sizeFrames() <= 0) {
        return {};
    }
    const double fraction = measurementMarkerFraction(channel);
    const qint64 sample = measurementMarkerSample(channel);
    if (sample < m_buffer.oldestSampleIndex()
        || sample >= m_buffer.newestSampleIndexExclusive()) {
        return {};
    }
    if (valid) {
        *valid = true;
    }
    const float value = m_buffer.valueAtSample(channel, sample);
    return QPointF(area.left() + fraction * area.width(),
                   mapValueToY(channel, value, area));
}

qint64 WaveformWidget::measurementMarkerSample(int channel) const
{
    const ScopeAxisTransform axis = axisTransform();
    const qint64 right = currentViewRightSample();
    return clampValue(axis.sampleAtHorizontalFraction(
                          measurementMarkerFraction(channel)),
                      right - axis.visibleFrameCount(), right - 1);
}

int WaveformWidget::measurementMarkerAt(const QPointF &position) const
{
    for (int channel = m_streamInfo.channelCount - 1; channel >= 0; --channel) {
        if (channel >= m_channelVisible.size() || m_channelVisible[channel] == 0) {
            continue;
        }
        bool valid = false;
        const QPointF point = measurementMarkerPoint(channel, &valid);
        if (valid && QRectF(point.x() - 11.0, point.y() - 11.0, 22.0, 22.0)
                         .contains(position)) {
            return channel;
        }
    }
    return -1;
}

void WaveformWidget::drawMeasurementMarkers(QPainter &painter, const QRectF &plotRect)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (int channel = 0; channel < m_streamInfo.channelCount; ++channel) {
        if (channel >= m_channelVisible.size() || m_channelVisible[channel] == 0
            || !measurementMarkerEnabled(channel)) {
            continue;
        }
        bool valid = false;
        const QPointF point = measurementMarkerPoint(channel, &valid);
        if (!valid) {
            continue;
        }
        const QColor color = channelColor(channel);
        painter.setClipRect(plotRect);
        painter.setPen(QPen(QColor(color.red(), color.green(), color.blue(), 125),
                            1.0, Qt::DashLine));
        painter.drawLine(QPointF(point.x(), plotRect.top()),
                         QPointF(point.x(), plotRect.bottom()));
        painter.setPen(QPen(color.lighter(135), 2.0));
        painter.setBrush(QColor(12, 20, 29, 230));
        painter.drawEllipse(point, 6.0, 6.0);
        painter.drawLine(QPointF(point.x() - 10.0, point.y()),
                         QPointF(point.x() + 10.0, point.y()));
        painter.drawLine(QPointF(point.x(), point.y() - 10.0),
                         QPointF(point.x(), point.y() + 10.0));
        painter.setClipping(false);
        const QRectF label(point.x() - 16.0, std::max(plotRect.top() + 2.0,
                                                      point.y() - 29.0), 32.0, 18.0);
        painter.fillRect(label, QColor(12, 20, 29, 215));
        painter.setPen(color);
        painter.drawText(label, Qt::AlignCenter, QStringLiteral("M%1").arg(channel + 1));
    }
    painter.restore();
}

void WaveformWidget::emitMeasurementMarkerReadout(int channel)
{
    const double percentage = 100.0 * measurementMarkerFraction(channel);
    bool valid = false;
    const QPointF markerPoint = measurementMarkerPoint(channel, &valid);
    Q_UNUSED(markerPoint)
    if (!valid) {
        emit measurementMarkerChanged(channel, false, percentage, 0.0, 0.0);
        return;
    }
    const qint64 sample = measurementMarkerSample(channel);
    emit measurementMarkerChanged(
        channel, true, percentage, axisTransform().relativeTimeForSample(sample),
        m_buffer.valueAtSample(channel, sample));
}

void WaveformWidget::emitAllMeasurementMarkerReadouts()
{
    for (int channel = 0; channel < m_streamInfo.channelCount; ++channel) {
        emitMeasurementMarkerReadout(channel);
    }
}

qint64 WaveformWidget::timeReferenceSample() const
{
    if (m_triggerMarkerSample >= 0 && !m_liveView) {
        return m_triggerMarkerSample;
    }
    return m_buffer.newestSampleIndexExclusive();
}

ScopeAxisTransform WaveformWidget::axisTransform() const
{
    const qint64 right = currentViewRightSample();
    const qint64 frames = qCeil(m_timeWindowSeconds
                                * std::max(1.0, m_streamInfo.sampleRateHz));
    return ScopeAxisTransform(std::max(1.0, m_streamInfo.sampleRateHz),
                              timePerDivisionSeconds(), voltsPerDivision(),
                              right - frames, timeReferenceSample());
}

QString WaveformWidget::formatTime(double seconds) const
{
    const double absolute = std::abs(seconds);
    if (absolute < 1e-15) {
        return QStringLiteral("0 s");
    }
    if (absolute < 1e-6) {
        return QStringLiteral("%1 ns").arg(seconds * 1e9, 0, 'g', 4);
    }
    if (absolute < 1e-3) {
        return QStringLiteral("%1 μs").arg(seconds * 1e6, 0, 'g', 4);
    }
    if (absolute < 1.0) {
        return QStringLiteral("%1 ms").arg(seconds * 1e3, 0, 'g', 4);
    }
    return QStringLiteral("%1 s").arg(seconds, 0, 'g', 4);
}

QString WaveformWidget::formatVoltage(double volts) const
{
    if (std::abs(volts) < 1e-15) {
        return QStringLiteral("0 V");
    }
    if (std::abs(volts) < 1e-3) {
        return QStringLiteral("%1 μV").arg(volts * 1e6, 0, 'g', 4);
    }
    if (std::abs(volts) < 1.0) {
        return QStringLiteral("%1 mV").arg(volts * 1e3, 0, 'g', 4);
    }
    return QStringLiteral("%1 V").arg(volts, 0, 'g', 4);
}

QRectF WaveformWidget::plotAreaRect() const
{
    return QRectF(rect()).adjusted(66.0, 38.0, -18.0, -38.0);
}

qint64 WaveformWidget::currentViewRightSample() const
{
    if (m_liveView) {
        return m_buffer.newestSampleIndexExclusive();
    }
    return clampValue(m_viewRightSample,
                      m_buffer.oldestSampleIndex(),
                      m_buffer.newestSampleIndexExclusive());
}

void WaveformWidget::clearPersistence()
{
    if (!m_persistenceLayer.isNull()) {
        m_persistenceLayer.fill(Qt::transparent);
    }
}

void WaveformWidget::initializeCursorsIfNeeded()
{
    if (m_cursorsInitialized || !m_streamInfo.isValid()
        || m_buffer.sizeFrames() < 2) {
        return;
    }
    const qint64 right = currentViewRightSample();
    const qint64 frames = qCeil(m_timeWindowSeconds * m_streamInfo.sampleRateHz);
    const qint64 first = right - frames;
    m_timeCursors[0] = first + qRound64(frames * 0.3);
    m_timeCursors[1] = first + qRound64(frames * 0.7);
    m_voltageCursors[0] = m_amplitudeRange * 0.25;
    m_voltageCursors[1] = -m_amplitudeRange * 0.25;
    m_cursorsInitialized = true;
}

void WaveformWidget::emitCursorReadout()
{
    if (!m_cursorsInitialized || !m_streamInfo.isValid()) {
        return;
    }
    const ScopeAxisTransform axis = axisTransform();
    const double t1 = axis.relativeTimeForSample(m_timeCursors[0]);
    const double t2 = axis.relativeTimeForSample(m_timeCursors[1]);
    emit cursorReadoutChanged(t1, t2, t2 - t1,
                              m_voltageCursors[0], m_voltageCursors[1],
                              m_voltageCursors[1] - m_voltageCursors[0]);
}

void WaveformWidget::drawCursors(QPainter &painter, const QRectF &plotRect)
{
    if (!m_cursorsVisible || !m_streamInfo.isValid()) {
        return;
    }
    initializeCursorsIfNeeded();
    if (!m_cursorsInitialized) {
        return;
    }

    const qint64 right = currentViewRightSample();
    const qint64 frames = qCeil(m_timeWindowSeconds * m_streamInfo.sampleRateHz);
    const qint64 first = right - frames;
    const QColor colors[2] = {QColor(238, 238, 245), QColor(255, 145, 205)};
    painter.save();
    for (int i = 0; i < 2; ++i) {
        const double x = plotRect.left() + plotRect.width()
            * static_cast<double>(m_timeCursors[i] - first)
            / std::max<qint64>(1, right - first);
        if (x >= plotRect.left() && x <= plotRect.right()) {
            painter.setPen(QPen(colors[i], 1.0, Qt::DashLine));
            painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
            painter.drawText(QRectF(x - 18.0, plotRect.top() + 4.0, 36.0, 18.0),
                             Qt::AlignCenter, QStringLiteral("T%1").arg(i + 1));
        }

        const double y = plotRect.center().y()
            - m_voltageCursors[i] / m_amplitudeRange * plotRect.height() * 0.5;
        if (y >= plotRect.top() && y <= plotRect.bottom()) {
            painter.setPen(QPen(colors[i], 1.0, Qt::DashLine));
            painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
            painter.drawText(QRectF(plotRect.left() + 4.0, y - 18.0, 36.0, 18.0),
                             Qt::AlignCenter, QStringLiteral("V%1").arg(i + 1));
        }
    }
    painter.restore();
}

QColor WaveformWidget::channelColor(int channel) const
{
    static const QColor colors[] = {
        QColor(21, 101, 192),
        QColor(0, 137, 123),
        QColor(198, 40, 40),
        QColor(106, 27, 154),
        QColor(239, 108, 0),
        QColor(46, 125, 50),
        QColor(93, 64, 55),
        QColor(69, 90, 100)
    };
    return colors[channel % 8];
}
