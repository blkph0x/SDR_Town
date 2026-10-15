#pragma once
#include <QJsonObject>
#include <cmath>

// DEC-0223: HTTP query values are strings; JSON callers may use integers.
// The message store retains 500 rows. Snapshot continuation is handled by the
// control server before this source-range parser is called.
inline bool controlMessageRange(const QJsonObject& body, int& limit, int& offset) {
    const auto read = [&](const char* key, int fallback, int low, int high, int& out) {
        const auto value = body.value(key);
        if (value.isUndefined()) { out = fallback; return true; }
        if (value.isString()) {
            const auto text = value.toString();
            if (text.isEmpty()) return false;
            for (const auto c : text) if (c < QChar('0') || c > QChar('9')) return false;
            bool ok = false;
            const auto number = text.toLongLong(&ok);
            if (!ok || number < low || number > high) return false;
            out = int(number); return true;
        }
        if (!value.isDouble()) return false;
        const double number = value.toDouble();
        if (!std::isfinite(number) || number < low || number > high || std::floor(number) != number) return false;
        out = int(number); return true;
    };
    return read("limit", 100, 1, 500, limit) && read("offset", 0, 0, 100000, offset);
}
