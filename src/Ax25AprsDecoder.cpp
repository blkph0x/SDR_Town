#include "Ax25AprsDecoder.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>

void Ax25AprsDecoder::reset() {
    markPhase_ = 0.0;
    spacePhase_ = 0.0;
    configuredRateHz_ = 0.0;
    samplesPerBit_ = 40;
    candidates_.clear();
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
    const uint16_t got = static_cast<uint16_t>(
        frameWithFcs[n] | (static_cast<uint16_t>(frameWithFcs[n + 1]) << 8));
    return calc == got;
}

std::string Ax25AprsDecoder::formatUiFrame(const std::vector<uint8_t>& infoFrame) {
    // Strip FCS if present; parse UI callsigns coarsely.
    std::vector<uint8_t> frame = infoFrame;
    if (frame.size() >= 2) frame.resize(frame.size() - 2);
    if (frame.size() < 16) return {};

    auto call = [](const uint8_t* p) {
        char text[8]{};
        int n = 0;
        for (int i = 0; i < 6; ++i) {
            const char ch = static_cast<char>((p[i] >> 1) & 0x7F);
            if (ch > 32) text[n++] = ch;
        }
        text[n] = 0;
        const int ssid = (p[6] >> 1) & 0x0F;
        std::ostringstream out;
        out << text;
        if (ssid) out << "-" << ssid;
        return out.str();
    };

    const std::string destination = call(frame.data());
    const std::string source = call(frame.data() + 7);
    size_t offset = 14;
    while (offset + 7 <= frame.size() && (frame[offset - 1] & 0x01) == 0)
        offset += 7; // digipeater addresses
    if (offset + 2 >= frame.size()) return destination + ">" + source;

    offset += 2; // control + PID
    std::string info;
    for (; offset < frame.size(); ++offset) {
        const char ch = static_cast<char>(frame[offset]);
        if (ch >= 32 && ch < 127) info.push_back(ch);
    }
    return destination + ">" + source + ":" + info;
}

void Ax25AprsDecoder::resetCandidate(Candidate& candidate, bool keepTiming) {
    const int timing = candidate.samplesUntilDecision;
    candidate = Candidate{};
    if (keepTiming) candidate.samplesUntilDecision = std::max(1, timing);
}

void Ax25AprsDecoder::configureForRate(double sampleRateHz) {
    if (!candidates_.empty() && std::abs(sampleRateHz - configuredRateHz_) < 0.5)
        return;

    configuredRateHz_ = sampleRateHz;
    samplesPerBit_ = std::max(8, static_cast<int>(std::lround(sampleRateHz / 1200.0)));
    markPhase_ = 0.0;
    spacePhase_ = 0.0;

    // Eight phase hypotheses are sufficient at the normal 48 kHz input while
    // keeping packet processing inexpensive.  A valid CRC arbitrates between
    // candidates and duplicate successful decodes are collapsed.
    const int phaseCount = std::max(1, std::min(8, samplesPerBit_));
    candidates_.assign(static_cast<size_t>(phaseCount), Candidate{});
    for (int phase = 0; phase < phaseCount; ++phase) {
        candidates_[static_cast<size_t>(phase)].samplesUntilDecision =
            std::max(1, ((phase + 1) * samplesPerBit_) / phaseCount);
    }
}

void Ax25AprsDecoder::emitFrame(const std::vector<uint8_t>& frameWithFcs) {
    if (!verifyFcs(frameWithFcs)) return;
    const std::string text = formatUiFrame(frameWithFcs);
    if (text.empty()) return;
    if (std::find(outFrames_.begin(), outFrames_.end(), text) == outFrames_.end())
        outFrames_.push_back(text);
}

void Ax25AprsDecoder::onByte(Candidate& candidate, uint8_t byte) {
    if (byte == 0x7E) {
        if (candidate.inFrame && candidate.frameBuf.size() >= 17)
            emitFrame(candidate.frameBuf);
        candidate.frameBuf.clear();
        candidate.inFrame = true;
        return;
    }
    if (!candidate.inFrame) return;
    candidate.frameBuf.push_back(byte);
    if (candidate.frameBuf.size() > 2048) {
        candidate.inFrame = false;
        candidate.frameBuf.clear();
    }
}

void Ax25AprsDecoder::bitIn(Candidate& candidate, bool toneIsMark) {
    // NRZI: a tone transition represents data 0; no transition represents 1.
    const bool dataBit = toneIsMark == candidate.lastTone;
    candidate.lastTone = toneIsMark;

    if (dataBit) {
        ++candidate.ones;
        if (candidate.ones >= 7) {
            // HDLC abort/idle.  Keep the tone and timing hypotheses, but reset
            // byte/frame assembly so the next flag can acquire cleanly.
            candidate.inFrame = false;
            candidate.frameBuf.clear();
            candidate.ones = 0;
            candidate.byteAcc = 0;
            candidate.bitCount = 0;
            return;
        }
    } else {
        if (candidate.ones == 5) {
            // Stuffed zero after five consecutive one bits.
            candidate.ones = 0;
            return;
        }
        candidate.ones = 0;
    }

    candidate.byteAcc |= static_cast<uint8_t>((dataBit ? 1 : 0) << candidate.bitCount);
    ++candidate.bitCount;
    if (candidate.bitCount >= 8) {
        onByte(candidate, candidate.byteAcc);
        candidate.byteAcc = 0;
        candidate.bitCount = 0;
    }
}

std::vector<std::string> Ax25AprsDecoder::processAudio(
    const float* samples, size_t count, double sampleRateHz)
{
    outFrames_.clear();
    if (!samples || count == 0 || !std::isfinite(sampleRateHz) || sampleRateHz < 8000.0)
        return {};

    configureForRate(sampleRateHz);
    const double twoPi = 2.0 * std::numbers::pi_v<double>;
    const double markIncrement = twoPi * 1200.0 / sampleRateHz;
    const double spaceIncrement = twoPi * 2200.0 / sampleRateHz;

    for (size_t i = 0; i < count; ++i) {
        const double sample = static_cast<double>(samples[i]);
        const double markCos = std::cos(markPhase_);
        const double markSin = std::sin(markPhase_);
        const double spaceCos = std::cos(spacePhase_);
        const double spaceSin = std::sin(spacePhase_);

        for (auto& candidate : candidates_) {
            candidate.markI += sample * markCos;
            candidate.markQ += sample * markSin;
            candidate.spaceI += sample * spaceCos;
            candidate.spaceQ += sample * spaceSin;

            --candidate.samplesUntilDecision;
            if (candidate.samplesUntilDecision <= 0) {
                const double markEnergy = candidate.markI * candidate.markI +
                                          candidate.markQ * candidate.markQ;
                const double spaceEnergy = candidate.spaceI * candidate.spaceI +
                                           candidate.spaceQ * candidate.spaceQ;
                bitIn(candidate, markEnergy >= spaceEnergy);
                candidate.markI = candidate.markQ = 0.0;
                candidate.spaceI = candidate.spaceQ = 0.0;
                candidate.samplesUntilDecision = samplesPerBit_;
            }
        }

        markPhase_ += markIncrement;
        spacePhase_ += spaceIncrement;
        if (markPhase_ >= twoPi) markPhase_ -= twoPi;
        if (spacePhase_ >= twoPi) spacePhase_ -= twoPi;
    }
    return outFrames_;
}
