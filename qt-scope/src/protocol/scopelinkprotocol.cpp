#include "protocol/scopelinkprotocol.h"

#include <QStringList>

namespace {
bool parseUnsigned(const QString &token, quint32 *value)
{
    bool ok = false;
    const quint32 parsed = token.toUInt(&ok);
    if (ok && value) {
        *value = parsed;
    }
    return ok;
}

bool parseMillivolts(const QString &token, float *volts)
{
    bool ok = false;
    const int millivolts = token.toInt(&ok);
    if (ok && volts) {
        *volts = static_cast<float>(millivolts) / 1000.0F;
    }
    return ok;
}
}

ScopeLinkMessage ScopeLinkProtocol::parseLine(const QByteArray &line)
{
    ScopeLinkMessage message;
    message.raw = QString::fromUtf8(line.trimmed());
    const QStringList fields = message.raw.split(QLatin1Char(' '), QString::SkipEmptyParts);
    if (fields.isEmpty()) {
        return message;
    }

    const QString kind = fields.first().toUpper();
    if (kind == QStringLiteral("HELLO")) {
        message.type = ScopeLinkMessage::Type::Hello;
        message.valid = fields.size() >= 3;
        return message;
    }
    if (kind == QStringLiteral("INFO")) {
        message.type = ScopeLinkMessage::Type::Info;
        message.valid = fields.size() >= 2;
        return message;
    }
    if (kind == QStringLiteral("ACK")) {
        message.type = ScopeLinkMessage::Type::Ack;
        message.valid = fields.size() >= 2;
        return message;
    }
    if (kind == QStringLiteral("ERR")) {
        message.type = ScopeLinkMessage::Type::Error;
        message.valid = true;
        return message;
    }
    if (kind == QStringLiteral("PONG")) {
        message.type = ScopeLinkMessage::Type::Pong;
        message.valid = fields.size() >= 3
            && parseUnsigned(fields.at(1), &message.sequence)
            && parseUnsigned(fields.at(2), &message.deviceMilliseconds);
        return message;
    }
    if (kind == QStringLiteral("SAMPLE")) {
        message.type = ScopeLinkMessage::Type::Sample;
        if (fields.size() < 5
            || !parseUnsigned(fields.at(1), &message.sequence)
            || !parseUnsigned(fields.at(2), &message.deviceMilliseconds)) {
            return message;
        }

        for (int index = 3; index < fields.size(); ++index) {
            float volts = 0.0F;
            if (!parseMillivolts(fields.at(index), &volts)) {
                message.channels.clear();
                return message;
            }
            message.channels.append(volts);
        }
        message.valid = message.channels.size() == 2;
        return message;
    }
    if (kind == QStringLiteral("FFT")) {
        quint32 frequencyMilliHz = 0;
        quint32 amplitudeMilliVolts = 0;
        message.type = ScopeLinkMessage::Type::Fft;
        message.valid = fields.size() == 6
            && parseUnsigned(fields.at(1), &message.sequence)
            && parseUnsigned(fields.at(2), &message.deviceMilliseconds)
            && parseUnsigned(fields.at(3), &frequencyMilliHz)
            && parseUnsigned(fields.at(4), &amplitudeMilliVolts)
            && parseUnsigned(fields.at(5), &message.computeMicroseconds);
        if (message.valid) {
            message.peakFrequencyHz = static_cast<float>(frequencyMilliHz) / 1000.0F;
            message.peakAmplitudeVolts = static_cast<float>(amplitudeMilliVolts) / 1000.0F;
        }
        return message;
    }

    return message;
}
