#pragma once

#include <QVector>
#include <QWidget>

class DacFunctionPreview final : public QWidget
{
public:
    explicit DacFunctionPreview(QWidget *parent = nullptr);
    void setSamples(const QVector<double> &samples);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<double> m_samples;
};
