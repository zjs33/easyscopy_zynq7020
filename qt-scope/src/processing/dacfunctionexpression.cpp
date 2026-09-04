#include "processing/dacfunctionexpression.h"

#include <QtMath>

namespace {
class Parser
{
public:
    Parser(const QString &text, double phase)
        : m_text(text), m_phase(phase) {}

    double parse()
    {
        const double value = expression();
        skipSpace();
        if (m_error.isEmpty() && m_pos != m_text.size()) {
            fail(QStringLiteral("位置 %1 存在非法字符").arg(m_pos + 1));
        }
        return value;
    }

    QString error() const { return m_error; }

private:
    void skipSpace()
    {
        while (m_pos < m_text.size() && m_text.at(m_pos).isSpace()) {
            ++m_pos;
        }
    }

    bool accept(QChar token)
    {
        skipSpace();
        if (m_pos < m_text.size() && m_text.at(m_pos) == token) {
            ++m_pos;
            return true;
        }
        return false;
    }

    void fail(const QString &message)
    {
        if (m_error.isEmpty()) {
            m_error = message;
        }
    }

    double expression()
    {
        double value = term();
        while (m_error.isEmpty()) {
            if (accept(QLatin1Char('+'))) {
                value += term();
            } else if (accept(QLatin1Char('-'))) {
                value -= term();
            } else {
                break;
            }
        }
        return value;
    }

    double term()
    {
        double value = unary();
        while (m_error.isEmpty()) {
            if (accept(QLatin1Char('*'))) {
                value *= unary();
            } else if (accept(QLatin1Char('/'))) {
                const double divisor = unary();
                if (qAbs(divisor) < 1.0e-15) {
                    fail(QStringLiteral("除数不能为零"));
                    return 0.0;
                }
                value /= divisor;
            } else {
                break;
            }
        }
        return value;
    }

    double unary()
    {
        if (accept(QLatin1Char('+'))) {
            return unary();
        }
        if (accept(QLatin1Char('-'))) {
            return -unary();
        }
        return primary();
    }

    double primary()
    {
        skipSpace();
        if (accept(QLatin1Char('('))) {
            const double value = expression();
            if (!accept(QLatin1Char(')'))) {
                fail(QStringLiteral("缺少右括号"));
            }
            return value;
        }
        if (m_pos >= m_text.size()) {
            fail(QStringLiteral("公式不完整"));
            return 0.0;
        }
        if (m_text.at(m_pos).isDigit() || m_text.at(m_pos) == QLatin1Char('.')) {
            const int start = m_pos;
            while (m_pos < m_text.size()
                   && (m_text.at(m_pos).isDigit() || m_text.at(m_pos) == QLatin1Char('.')
                       || m_text.at(m_pos) == QLatin1Char('e')
                       || m_text.at(m_pos) == QLatin1Char('E')
                       || ((m_text.at(m_pos) == QLatin1Char('+')
                            || m_text.at(m_pos) == QLatin1Char('-'))
                           && m_pos > start
                           && (m_text.at(m_pos - 1) == QLatin1Char('e')
                               || m_text.at(m_pos - 1) == QLatin1Char('E'))))) {
                ++m_pos;
            }
            bool ok = false;
            const double value = m_text.mid(start, m_pos - start).toDouble(&ok);
            if (!ok) {
                fail(QStringLiteral("数字格式错误"));
            }
            return value;
        }
        if (m_text.at(m_pos).isLetter()) {
            const int start = m_pos;
            while (m_pos < m_text.size() && m_text.at(m_pos).isLetter()) {
                ++m_pos;
            }
            const QString name = m_text.mid(start, m_pos - start).toLower();
            if (name == QStringLiteral("x")) {
                return m_phase;
            }
            if (!accept(QLatin1Char('('))) {
                fail(QStringLiteral("函数 %1 缺少左括号").arg(name));
                return 0.0;
            }
            const double argument = expression();
            if (!accept(QLatin1Char(')'))) {
                fail(QStringLiteral("函数 %1 缺少右括号").arg(name));
                return 0.0;
            }
            const double fractional = argument - qFloor(argument);
            if (name == QStringLiteral("sin")) {
                return qSin(2.0 * M_PI * argument);
            }
            if (name == QStringLiteral("cos")) {
                return qCos(2.0 * M_PI * argument);
            }
            if (name == QStringLiteral("tri")) {
                return 1.0 - 4.0 * qAbs(fractional - 0.5);
            }
            if (name == QStringLiteral("square")) {
                return fractional < 0.5 ? 1.0 : -1.0;
            }
            if (name == QStringLiteral("saw")) {
                return 2.0 * fractional - 1.0;
            }
            fail(QStringLiteral("不支持函数 %1").arg(name));
            return 0.0;
        }
        fail(QStringLiteral("位置 %1 需要数字、x 或函数").arg(m_pos + 1));
        return 0.0;
    }

    QString m_text;
    double m_phase = 0.0;
    int m_pos = 0;
    QString m_error;
};
}

DacFunctionResult DacFunctionExpression::generate(const QString &formula, int pointCount)
{
    DacFunctionResult result;
    if (formula.trimmed().isEmpty() || pointCount <= 0) {
        result.error = QStringLiteral("公式不能为空");
        return result;
    }
    result.volts.reserve(pointCount);
    result.codes.reserve(pointCount);
    for (int index = 0; index < pointCount; ++index) {
        Parser parser(formula, static_cast<double>(index) / pointCount);
        const double raw = parser.parse();
        if (!parser.error().isEmpty() || !qIsFinite(raw)) {
            result.error = parser.error().isEmpty()
                ? QStringLiteral("计算结果不是有限数") : parser.error();
            result.volts.clear();
            result.codes.clear();
            return result;
        }
        const double volts = qBound(-5.0, raw, 5.0);
        if (!qFuzzyCompare(volts + 6.0, raw + 6.0)) {
            ++result.clippedCount;
        }
        result.volts.append(volts);
        result.codes.append(static_cast<char>(qBound(0, qRound((5.0 - volts) * 25.5), 255)));
    }
    result.crc16 = crc16Ccitt(result.codes);
    result.valid = true;
    return result;
}

quint16 DacFunctionExpression::crc16Ccitt(const QByteArray &bytes)
{
    quint16 crc = 0xFFFFU;
    for (const char byte : bytes) {
        crc ^= static_cast<quint16>(static_cast<quint8>(byte)) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) != 0U
                ? static_cast<quint16>((crc << 1) ^ 0x1021U)
                : static_cast<quint16>(crc << 1);
        }
    }
    return crc;
}
