#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

struct ScopeLinkMessage
{
    enum class Type {
        Unknown,
        Hello,
        Info,
        Pong,
        Sample,
        Fft,
        Ack,
        Error
    };

    Type type = Type::Unknown;
    QString raw;
    quint32 sequence = 0;
    quint32 deviceMilliseconds = 0;
    QVector<float> channels;
    float peakFrequencyHz = 0.0F;
    float peakAmplitudeVolts = 0.0F;
    quint32 computeMicroseconds = 0;
    bool valid = false;
};

class ScopeLinkProtocol
{
public:
    static ScopeLinkMessage parseLine(const QByteArray &line);
};
