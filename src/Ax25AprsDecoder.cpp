#include "Ax25AprsDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <sstream>

void Ax25AprsDecoder::reset() {
    phase_ = markPhase_ = spacePhase_ = 0.0;
    bitClock_ = 0;
    lastBit_ = true;
    ones_ = 0;
    inFrame_ = false;
    byteAcc_ = 0;
    bitCount_ = 0;
    frameBuf_.clear();
    outFrames_.clear();
}

uint16_t Ax25AprsDecoder::crc16Fcs(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 1) crc = static_cast<uint16_t>((crc >> 1) ^ 0x8408);
            else crc = static_cast<uint16_t>(crc >> 1);
        }
    }
    return static_cast<uint16_t>(~crc);
}

bool Ax25AprsDecoder::verifyFcs(const std::vector<uint8_t>& frameWithFcs) {
    if (frameWithFcs.size() < 3) return false;
    const size_t n = frameWithFcs.size() - 2;
    const uint16_t calc = crc16Fcs(frameWithFcs.data(), n);
    const uint16_t got = static_cast<uint16_t>(frameWithFcs[n] | (frameWithFcs[n + 1] << 8));
    return calc == got;
}

std::string Ax25AprsDecoder::formatUiFrame(const std::vector<uint8_t>& infoFrame) {
    // Strip FCS if present; parse UI callsigns coarsely.
    std::vector<uint8_t> f = infoFrame;
    if (f.size() >= 2) f.resize(f.size() - 2);
    if (f.size() < 16) return {};
    auto call = [](const uint8_t* p) {
        char c[8]{};
        int n = 0;
        for (int i = 0; i < 6; ++i) {
            char ch = static_cast<char>((p[i] >> 1) & 0x7F);
            if (ch > 32) c[n++] = ch;
        }
        c[n] = 0;
        const int ssid = (p[6] >> 1) & 0x0F;
        std::ostringstream o;
        o << c;
        if (ssid) o << "-" << ssid;
        return o.str();
    };
    std::string dest = call(f.data());
    std::string src = call(f.data() + 7);
    size_t i = 14;
    while (i + 7 <= f.size() && (f[i - 1] & 0x01) == 0) i += 7; // digis
    if (i + 2 >= f.size()) return dest + ">" + src;
    // control + PID
    i += 2;
    std::string info;
    for (; i < f.size(); ++i) {
        char ch = static_cast<char>(f[i]);
        if (ch >= 32 && ch < 127) info.push_back(ch);
    }
    return dest + ">" + src + ":" + info;
}

void Ax25AprsDecoder::onByte(uint8_t b) {
    if (b == 0x7E) { // flag
        if (inFrame_ && frameBuf_.size() >= 17) {
            if (verifyFcs(frameBuf_)) {
                auto text = formatUiFrame(frameBuf_);
                if (!text.empty()) outFrames_.push_back(std::move(text));
            }
        }
        frameBuf_.clear();
        inFrame_ = true;
        return;
    }
    if (!inFrame_) return;
    frameBuf_.push_back(b);
    if (frameBuf_.size() > 512) {
        inFrame_ = false;
        frameBuf_.clear();
    }
}

void Ax25AprsDecoder::bitIn(bool bit) {
    // NRZI: transition = 0, no transition = 1
    const bool dataBit = (bit == lastBit_);
    lastBit_ = bit;

    if (dataBit) {
        ++ones_;
        if (ones_ >= 7) { // abort
            inFrame_ = false;
            frameBuf_.clear();
            ones_ = 0;
            bitCount_ = 0;
            return;
        }
    } else {
        if (ones_ == 5) {
            // bit stuffing: discard stuffed 0
            ones_ = 0;
            return;
        }
        ones_ = 0;
    }

    byteAcc_ |= static_cast<uint8_t>((dataBit ? 1 : 0) << bitCount_);
    ++bitCount_;
    if (bitCount_ >= 8) {
        onByte(byteAcc_);
        byteAcc_ = 0;
        bitCount_ = 0;
    }
}

std::vector<std::string> Ax25AprsDecoder::processAudio(const float* samples, size_t count, double sampleRateHz) {
    outFrames_.clear();
    if (!samples || count == 0 || sampleRateHz < 8000.0) return {};
    samplesPerBit_ = std::max(8, static_cast<int>(std::lround(sampleRateHz / 1200.0)));
    const double mark = 1200.0;
    const double space = 2200.0;
    const double twoPi = 2.0 * std::numbers::pi_v<double>;

    for (size_t i = 0; i < count; ++i) {
        const float x = samples[i];
        markPhase_ += twoPi * mark / sampleRateHz;
        spacePhase_ += twoPi * space / sampleRateHz;
        if (markPhase_ > twoPi) markPhase_ -= twoPi;
        if (spacePhase_ > twoPi) spacePhase_ -= twoPi;
        const double markCorr = x * std::cos(markPhase_);
        const double spaceCorr = x * std::cos(spacePhase_);
        // crude integrate & dump
        phase_ += (markCorr - spaceCorr);
        ++bitClock_;
        if (bitClock_ >= samplesPerBit_) {
            bitIn(phase_ >= 0.0);
            phase_ = 0.0;
            bitClock_ = 0;
        }
    }
    return outFrames_;
}
