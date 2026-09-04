#include "ui/dacfunctionpreview.h"

#include <QPainter>
#include <QPainterPath>

DacFunctionPreview::DacFunctionPreview(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(90);
}

void DacFunctionPreview::setSamples(const QVector<double> &samples)
{
    m_samples = samples;
    update();
}

void DacFunctionPreview::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(255, 255, 255));
    painter.setPen(QColor(185, 198, 210));
    painter.drawLine(0, height() / 2, width(), height() / 2);
    if (m_samples.size() < 2) {
        return;
    }
    QPainterPath path;
    for (int index = 0; index < m_samples.size(); ++index) {
        const qreal x = static_cast<qreal>(index) * (width() - 1)
            / (m_samples.size() - 1);
        const qreal y = (5.0 - m_samples.at(index)) * (height() - 1) / 10.0;
        if (index == 0) {
            path.moveTo(x, y);
        } else {
            path.lineTo(x, y);
        }
    }
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(21, 101, 192), 1.5));
    painter.drawPath(path);
}
