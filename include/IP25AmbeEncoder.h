#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Sprint 3: AMBE+2 half-rate encode adapter for P25 Phase 2 clear TX.
// Real speech requires a licensed DVSI encoder or hardware dongle.
// Built-in backends are pipeline/test only (silence / PCM energy placeholder).

struct P25AmbeEncodedFrame {
    // 49 soft bits as 0/1 (mbelib decodeAmbe2450Data layout).
    std::array<uint8_t, 49> bits49{};
    // Packed 49 bits → 7 bytes (MSB first).
    std::array<uint8_t, 7> packed7{};
    // 96-bit FEC frame placeholder (filled by framer later; zeros for silence).
    std::array<uint8_t, 96> ambe96{};
    // Source PCM peak |sample| for diagnostics (0..1).
    float pcmPeak = 0.0f;
    float pcmRms = 0.0f;
    bool valid = false;
};

class IP25AmbeEncoder {
public:
    virtual ~IP25AmbeEncoder() = default;

    // pcm: exactly 160 mono samples at 8 kHz (20 ms).
    virtual bool encodeFrame(const float* pcm160, P25AmbeEncodedFrame& out) = 0;
    virtual const char* name() const noexcept = 0;
    // True only for licensed/hardware backends that produce on-air speech.
    virtual bool producesRealSpeech() const noexcept { return false; }
};

// Zero-parameter / comfort-noise-ish placeholder frames (not real AMBE speech).
class P25AmbeSilenceEncoder final : public IP25AmbeEncoder {
public:
    bool encodeFrame(const float* pcm160, P25AmbeEncodedFrame& out) override;
    const char* name() const noexcept override { return "silence-placeholder"; }
};

// Embeds crude PCM energy into parameter bits for pipeline tests only.
// Must NOT be used for over-the-air TX (not a legal AMBE encoder).
class P25AmbeEnergyPlaceholderEncoder final : public IP25AmbeEncoder {
public:
    bool encodeFrame(const float* pcm160, P25AmbeEncodedFrame& out) override;
    const char* name() const noexcept override { return "energy-placeholder"; }
};

// Factory: "silence" (default), "energy", future "dvsi"/"dongle".
std::unique_ptr<IP25AmbeEncoder> p25CreateAmbeEncoder(const std::string& backendName);

// Slices 8 kHz PCM into 20 ms frames and encodes.
class P25TxVoicePacketizer {
public:
    explicit P25TxVoicePacketizer(std::unique_ptr<IP25AmbeEncoder> encoder);

    void reset();
    void setEncoder(std::unique_ptr<IP25AmbeEncoder> encoder);
    IP25AmbeEncoder* encoder() const noexcept { return m_encoder.get(); }

    // Push any number of 8 kHz mono samples; completed frames appended to out.
    size_t pushPcm8k(const float* samples, size_t count, std::vector<P25AmbeEncodedFrame>& out);

    uint64_t framesEncoded() const noexcept { return m_framesEncoded; }
    size_t pendingSamples() const noexcept { return m_pending.size(); }

private:
    std::unique_ptr<IP25AmbeEncoder> m_encoder;
    std::vector<float> m_pending;
    uint64_t m_framesEncoded = 0;
};

// Optional binary dump of packed7 frames (7 bytes each) for offline inspection.
bool p25WriteAmbePackedDump(const std::string& path,
                            const std::vector<P25AmbeEncodedFrame>& frames);
