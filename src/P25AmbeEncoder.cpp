#include "IP25AmbeEncoder.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace {

void fillPcmStats(const float* pcm160, P25AmbeEncodedFrame& out)
{
    if (!pcm160) {
        out.pcmPeak = 0.0f;
        out.pcmRms = 0.0f;
        return;
    }
    double sumSq = 0.0;
    float peak = 0.0f;
    for (int i = 0; i < 160; ++i) {
        const float s = pcm160[i];
        sumSq += static_cast<double>(s) * static_cast<double>(s);
        peak = std::max(peak, std::abs(s));
    }
    out.pcmPeak = peak;
    out.pcmRms = static_cast<float>(std::sqrt(sumSq / 160.0));
}

void pack49To7(const std::array<uint8_t, 49>& bits, std::array<uint8_t, 7>& packed)
{
    packed.fill(0);
    for (size_t i = 0; i < 49; ++i) {
        if (bits[i] & 1u) {
            packed[i / 8] |= static_cast<uint8_t>(1u << (7 - (i % 8)));
        }
    }
}

void bits49ToAmbe96Placeholder(const std::array<uint8_t, 49>& bits, std::array<uint8_t, 96>& ambe96)
{
    // Structural placeholder: copy 49 payload bits into the front of the 96-bit
    // FEC frame and leave parity bits zero. Real FEC encoding lands in Sprint 4.
    ambe96.fill(0);
    for (size_t i = 0; i < 49; ++i) {
        ambe96[i] = bits[i] ? 1u : 0u;
    }
}

} // namespace

bool P25AmbeSilenceEncoder::encodeFrame(const float* pcm160, P25AmbeEncodedFrame& out)
{
    out = {};
    fillPcmStats(pcm160, out);
    out.bits49.fill(0);
    pack49To7(out.bits49, out.packed7);
    bits49ToAmbe96Placeholder(out.bits49, out.ambe96);
    out.valid = true;
    return true;
}

bool P25AmbeEnergyPlaceholderEncoder::encodeFrame(const float* pcm160, P25AmbeEncodedFrame& out)
{
    out = {};
    fillPcmStats(pcm160, out);
    out.bits49.fill(0);

    // Non-AMBE diagnostic pattern only: quantize RMS/peak into a few bits so
    // offline dumps prove the packetizer is feeding live mic energy. Never TX.
    const uint8_t rmsQ = static_cast<uint8_t>(std::min(31.0f, out.pcmRms * 200.0f));
    const uint8_t peakQ = static_cast<uint8_t>(std::min(31.0f, out.pcmPeak * 100.0f));
    for (int b = 0; b < 5; ++b) {
        out.bits49[static_cast<size_t>(b)] = (rmsQ >> b) & 1u;
        out.bits49[static_cast<size_t>(8 + b)] = (peakQ >> b) & 1u;
    }
    // Frame counter nibble so successive frames differ when energy is flat.
    static thread_local uint8_t seq = 0;
    const uint8_t s = seq++;
    for (int b = 0; b < 8; ++b) {
        out.bits49[static_cast<size_t>(16 + b)] = (s >> b) & 1u;
    }

    pack49To7(out.bits49, out.packed7);
    bits49ToAmbe96Placeholder(out.bits49, out.ambe96);
    out.valid = true;
    return true;
}

std::unique_ptr<IP25AmbeEncoder> p25CreateAmbeEncoder(const std::string& backendName)
{
    std::string name = backendName;
    for (char& c : name) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (name.empty() || name == "silence" || name == "default" || name == "placeholder") {
        return std::make_unique<P25AmbeSilenceEncoder>();
    }
    if (name == "energy" || name == "energy-placeholder" || name == "test") {
        return std::make_unique<P25AmbeEnergyPlaceholderEncoder>();
    }
    if (name == "dvsi" || name == "dongle" || name == "hardware") {
        spdlog::warn("P25 AMBE backend '{}' is not linked; using silence placeholder", backendName);
        return std::make_unique<P25AmbeSilenceEncoder>();
    }
    spdlog::warn("Unknown AMBE backend '{}'; using silence", backendName);
    return std::make_unique<P25AmbeSilenceEncoder>();
}

P25TxVoicePacketizer::P25TxVoicePacketizer(std::unique_ptr<IP25AmbeEncoder> encoder)
    : m_encoder(std::move(encoder))
{
    if (!m_encoder) m_encoder = p25CreateAmbeEncoder("silence");
    m_pending.reserve(320);
}

void P25TxVoicePacketizer::reset()
{
    m_pending.clear();
    m_framesEncoded = 0;
}

void P25TxVoicePacketizer::setEncoder(std::unique_ptr<IP25AmbeEncoder> encoder)
{
    m_encoder = std::move(encoder);
    if (!m_encoder) m_encoder = p25CreateAmbeEncoder("silence");
}

size_t P25TxVoicePacketizer::pushPcm8k(const float* samples, size_t count, std::vector<P25AmbeEncodedFrame>& out)
{
    if (!samples || count == 0 || !m_encoder) return 0;
    m_pending.insert(m_pending.end(), samples, samples + count);
    size_t produced = 0;
    while (m_pending.size() >= 160) {
        P25AmbeEncodedFrame frame;
        if (m_encoder->encodeFrame(m_pending.data(), frame) && frame.valid) {
            out.push_back(frame);
            ++produced;
            ++m_framesEncoded;
        }
        m_pending.erase(m_pending.begin(), m_pending.begin() + 160);
    }
    return produced;
}

bool p25WriteAmbePackedDump(const std::string& path, const std::vector<P25AmbeEncodedFrame>& frames)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    for (const auto& fr : frames) {
        f.write(reinterpret_cast<const char*>(fr.packed7.data()), 7);
    }
    return static_cast<bool>(f);
}
