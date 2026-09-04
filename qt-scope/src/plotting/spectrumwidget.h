#pragma once

#include "processing/fftengine.h"

#include <QPolygonF>
#include <QWidget>

class SpectrumWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit SpectrumWidget(QWidget *parent = nullptr);
    [[nodiscard]] bool hasSpectrum() const { return m_result.valid; }

public slots:
    void setSpectrum(const SpectrumResult &result);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    SpectrumResult m_result;
    QPolygonF m_points;
};
