#include "InmarsatAcars.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <regex>
#include <sstream>

namespace {

double unixNow() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

bool isPrintableAcars(unsigned char c) {
    return (c >= 32 && c < 127) || c == '\n' || c == '\r' || c == '\t';
}

} // namespace

void InmarsatAcars::reset() { buf_.clear(); }

void InmarsatAcars::emit(InmarsatMessage msg) {
    if (msg.unixTime <= 0) msg.unixTime = unixNow();
    if (sink_) sink_(msg);
}

InmarsatMessage InmarsatAcars::parseAcarsText(const std::string& text, double freqHz) {
    InmarsatMessage m;
    m.kind = InmarsatMsgKind::Acars;
    m.freqHz = freqHz;
    m.text = text;
    m.unixTime = unixNow();

    // Label often appears as "Label XX" or after registration
    static const std::regex labelRe(R"((?:Label|Lbl)[:\s]*([A-Z0-9]{2}))", std::regex::icase);
    std::smatch sm;
    if (std::regex_search(text, sm, labelRe)) m.label = sm[1].str();

    static const std::regex icaoRe(R"(\b([0-9A-F]{6})\b)", std::regex::icase);
    if (std::regex_search(text, sm, icaoRe)) {
        m.icaoHex = sm[1].str();
        for (auto& c : m.icaoHex) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        unsigned v = 0;
        if (std::sscanf(m.icaoHex.c_str(), "%x", &v) == 1) m.aesId = v;
    }

    double lat = 0, lon = 0;
    if (tryParseAdscPosition(text, &lat, &lon)) {
        m.hasPosition = true;
        m.latDeg = lat;
        m.lonDeg = lon;
    }

    uint32_t aes = 0;
    double rx = 0, tx = 0;
    if (tryParseCassign(text, &aes, &rx, &tx)) {
        m.kind = InmarsatMsgKind::CAssign;
        m.aesId = aes ? aes : m.aesId;
        m.voiceRxHz = rx;
        m.voiceTxHz = tx;
        m.label = "CASSIGN";
    }

    if (text.find("EGC") != std::string::npos || text.find("SafetyNET") != std::string::npos ||
        text.find("FleetNET") != std::string::npos || text.find("NAVAREA") != std::string::npos) {
        m.kind = InmarsatMsgKind::Egc;
    }
    return m;
}

bool InmarsatAcars::tryParseAdscPosition(const std::string& text, double* lat, double* lon) {
    if (!lat || !lon) return false;
    // Forms: 3352.1S 15112.4E  or  -33.87 151.21  or  N335212 E1511214
    static const std::regex dms(
        R"(([NS])\s*(\d{2})(\d{2})(?:\.(\d+))?[\s,]+([EW])\s*(\d{2,3})(\d{2})(?:\.(\d+))?)",
        std::regex::icase);
    std::smatch sm;
    if (std::regex_search(text, sm, dms)) {
        double la = std::stod(sm[2].str()) + std::stod(sm[3].str()) / 60.0;
        if (sm[4].matched) la += std::stod(std::string("0.") + sm[4].str()) / 60.0;
        double lo = std::stod(sm[6].str()) + std::stod(sm[7].str()) / 60.0;
        if (sm[8].matched) lo += std::stod(std::string("0.") + sm[8].str()) / 60.0;
        if (std::toupper(sm[1].str()[0]) == 'S') la = -la;
        if (std::toupper(sm[5].str()[0]) == 'W') lo = -lo;
        *lat = la;
        *lon = lo;
        return std::isfinite(la) && std::isfinite(lo);
    }
    static const std::regex dec(R"((-?\d{1,2}\.\d+)\s*[,/\s]\s*(-?\d{1,3}\.\d+))");
    if (std::regex_search(text, sm, dec)) {
        *lat = std::stod(sm[1].str());
        *lon = std::stod(sm[2].str());
        return std::abs(*lat) <= 90.0 && std::abs(*lon) <= 180.0;
    }
    static const std::regex hemis(
        R"((\d{2,4}(?:\.\d+)?)([NS])\s+(\d{2,5}(?:\.\d+)?)([EW]))", std::regex::icase);
    if (std::regex_search(text, sm, hemis)) {
        double la = std::stod(sm[1].str());
        double lo = std::stod(sm[3].str());
        if (la > 90.0) la = std::floor(la / 100.0) + std::fmod(la, 100.0) / 60.0;
        if (lo > 180.0) lo = std::floor(lo / 100.0) + std::fmod(lo, 100.0) / 60.0;
        if (std::toupper(sm[2].str()[0]) == 'S') la = -la;
        if (std::toupper(sm[4].str()[0]) == 'W') lo = -lo;
        *lat = la;
        *lon = lo;
        return true;
    }
    return false;
}

bool InmarsatAcars::tryParseCassign(const std::string& text, uint32_t* aes, double* rxHz, double* txHz) {
    if (!aes || !rxHz || !txHz) return false;
    static const std::regex re(
        R"(C[-\s]?ASSIGN.*?AES[:=\s]*([0-9A-Fa-f]+).*?(?:RX|Rx)[:=\s]*([0-9.]+).*?(?:TX|Tx)[:=\s]*([0-9.]+))",
        std::regex::icase);
    std::smatch sm;
    if (!std::regex_search(text, sm, re)) {
        // Alternate: "voice rx=1544.500 tx=1645.000 aes=ABCDEF"
        static const std::regex re2(
            R"((?:voice|cassign).*?rx[=:\s]+([0-9.]+).*?tx[=:\s]+([0-9.]+).*?aes[=:\s]+([0-9A-Fa-f]+))",
            std::regex::icase);
        if (!std::regex_search(text, sm, re2)) return false;
        *rxHz = std::stod(sm[1].str());
        *txHz = std::stod(sm[2].str());
        unsigned v = 0;
        std::sscanf(sm[3].str().c_str(), "%x", &v);
        *aes = v;
    } else {
        unsigned v = 0;
        std::sscanf(sm[1].str().c_str(), "%x", &v);
        *aes = v;
        *rxHz = std::stod(sm[2].str());
        *txHz = std::stod(sm[3].str());
    }
    if (*rxHz > 1000.0 && *rxHz < 3000.0) *rxHz *= 1e6; // MHz → Hz
    if (*txHz > 1000.0 && *txHz < 3000.0) *txHz *= 1e6;
    return *rxHz > 1e9 && *rxHz < 2e9;
}

void InmarsatAcars::feedBytes(const uint8_t* data, size_t len, double freqHz) {
    if (!data || len == 0) return;
    buf_.insert(buf_.end(), data, data + len);
    if (buf_.size() > 8192) buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(buf_.size() - 4096));
    scanBuffer(freqHz);
}

void InmarsatAcars::scanBuffer(double freqHz) {
    // Look for printable runs bookended by SOH (0x01) / STX (0x02) / ETX (0x03) or long ASCII.
    while (buf_.size() >= 16) {
        size_t start = 0;
        bool framed = false;
        for (; start + 8 < buf_.size(); ++start) {
            if (buf_[start] == 0x01 || buf_[start] == 0x02) {
                framed = true;
                break;
            }
        }
        if (!framed) {
            // Scan for long printable sequences
            size_t i = 0;
            while (i < buf_.size() && !isPrintableAcars(buf_[i])) ++i;
            size_t j = i;
            while (j < buf_.size() && isPrintableAcars(buf_[j])) ++j;
            if (j - i >= 24) {
                std::string text(reinterpret_cast<const char*>(buf_.data() + i), j - i);
                emit(parseAcarsText(text, freqHz));
                buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(j));
                continue;
            }
            if (i > 0) buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
        size_t end = start + 1;
        for (; end < buf_.size(); ++end) {
            if (buf_[end] == 0x03 || buf_[end] == 0x04) break;
        }
        if (end >= buf_.size()) break;
        std::string text;
        for (size_t k = start + 1; k < end; ++k) {
            if (isPrintableAcars(buf_[k])) text.push_back(static_cast<char>(buf_[k]));
        }
        if (text.size() >= 8) emit(parseAcarsText(text, freqHz));
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(end + 1));
    }
}
