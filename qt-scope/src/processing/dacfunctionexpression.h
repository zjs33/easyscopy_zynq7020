#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

struct DacFunctionResult
{
    bool valid = false;
    QString error;
    QVector<double> volts;
    QByteArray codes;
    int clippedCount = 0;
    quint16 crc16 = 0;
};

class DacFunctionExpression
{
public:
    static DacFunctionResult generate(const QString &formula, int pointCount = 256);
    static quint16 crc16Ccitt(const QByteArray &bytes);
};
