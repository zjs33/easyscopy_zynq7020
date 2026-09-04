#include "plotting/spectrumwidget.h"

#include "core/compilercompat.h"

#include <QPainter>
#include <QPaintEvent>

#include <algorithm>

SpectrumWidget::SpectrumWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(320, 200);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void SpectrumWidget::setSpectrum(const SpectrumResult &result)
{
    m_result = result;
    update();
}

void SpectrumWidget::clear()
{
    m_result = {};
    m_points.clear();
    update();
}

void SpectrumWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.fillRect(rect(), QColor(255, 255, 255));
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    const QRectF plot = QRectF(rect()).adjusted(70.0, 42.0, -20.0, -42.0);

    painter.setPen(QPen(QColor(218, 226, 234), 1.0));
    for (int i = 0; i <= 10; ++i) {
        const double x = plot.left() + plot.width() * i / 10.0;
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
    for (int i = 0; i <= 8; ++i) {
        const double y = plot.top() + plot.height() * i / 8.0;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    painter.setPen(QColor(79, 99, 119));
    painter.drawRect(plot);

    if (!m_result.valid || m_result.magnitudes.isEmpty()) {
        painter.drawText(plot, Qt::AlignCenter, QStringLiteral("等待 FFT 数据…"));
        return;
    }

    const double xMaximum = m_result.sampleRateHz * 0.5;
    double yMinimum = 0.0;
    double yMaximum = 0.0;
    if (m_result.decibels) {
        yMaximum = std::max(0.0, m_result.peakMagnitude + 6.0);
        yMinimum = yMaximum - 120.0;
    } else {
        yMaximum = std::max(1e-12, m_result.peakMagnitude * 1.15);
    }

    m_points.clear();
    m_points.reserve(m_result.magnitudes.size());
    for (int i = 0; i < m_result.magnitudes.size(); ++i) {
        const double x = plot.left() + plot.width()
            * m_result.frequenciesHz[i] / xMaximum;
        const double value = clampValue(m_result.magnitudes[i], yMinimum, yMaximum);
        const double y = plot.bottom() - plot.height()
            * (value - yMinimum) / std::max(1e-12, yMaximum - yMinimum);
        m_points.append(QPointF(x, y));
    }
    painter.setClipRect(plot);
    painter.setPen(QPen(QColor(106, 27, 154), 1.2));
    painter.drawPolyline(m_points);
    painter.setClipping(false);

    painter.setPen(QColor(31, 48, 64));
    painter.drawText(QRectF(plot.left(), 8.0, plot.width(), 24.0),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("FFT %1 点 · Δf %2 Hz · 峰值 %3 Hz / %4 %5")
                         .arg(m_result.fftSize)
                         .arg(m_result.binWidthHz, 0, 'g', 6)
                         .arg(m_result.peakFrequencyHz, 0, 'g', 7)
                         .arg(m_result.peakMagnitude, 0, 'g', 6)
                         .arg(m_result.decibels ? QStringLiteral("dB")
                                               : QStringLiteral("V")));

    for (int i = 0; i <= 5; ++i) {
        const double frequency = xMaximum * i / 5.0;
        const double x = plot.left() + plot.width() * i / 5.0;
        painter.drawText(QRectF(x - 45.0, plot.bottom() + 8.0, 90.0, 20.0),
                         Qt::AlignHCenter | Qt::AlignTop,
                         frequency >= 1000.0
                             ? QStringLiteral("%1 kHz").arg(frequency / 1000.0, 0, 'g', 4)
                             : QStringLiteral("%1 Hz").arg(frequency, 0, 'g', 4));
    }
    for (int i = 0; i <= 4; ++i) {
        const double value = yMaximum - (yMaximum - yMinimum) * i / 4.0;
        const double y = plot.top() + plot.height() * i / 4.0;
        painter.drawText(QRectF(3.0, y - 9.0, 61.0, 18.0),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("%1").arg(value, 0, 'g', 4));
    }
}
