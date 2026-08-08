#include "P25Phase2TxFramer.h"

#include <fstream>

void P25Phase2TxFramer::setConfig(const P25Phase2TxFramerConfig& cfg)
{
    m_cfg = cfg;
    m_cfg.slot &= 0x01u;
}

uint64_t P25Phase2TxFramer::maskSeed() const noexcept
{
    // Same identity triple RX uses for mask parameters (exact LFSR poly is Sprint 4).
    return (static_cast<uint64_t>(m_cfg.wacn & 0xFFFFFu) << 28) |
           (static_cast<uint64_t>(m_cfg.systemId & 0xFFFu) << 12) |
           static_cast<uint64_t>(m_cfg.nac & 0xFFFu);
}

P25Phase2TxSuperframe P25Phase2TxFramer::buildSuperframe(const std::vector<P25AmbeEncodedFrame>& frames)
{
    P25Phase2TxSuperframe sf;
    // 360 ms * 6000 symbols/s = 2160 symbols; each symbol is one dibit.
    constexpr size_t kSymbols = 2160;
    sf.dibits.assign(kSymbols, 0);

    // Prototype: interleave available AMBE 96-bit frames as dibits into fixed
    // offsets approximating Voice4 payload regions. Bit-exact layout TBD vs capture.
    size_t used = 0;
    size_t cursor = 64 + static_cast<size_t>(m_cfg.slot) * 8; // coarse slot offset
    for (const auto& fr : frames) {
        if (!fr.valid) continue;
        for (size_t b = 0; b + 1 < 96 && cursor < kSymbols; b += 2) {
            const uint8_t d = static_cast<uint8_t>(((fr.ambe96[b] & 1u) << 1) | (fr.ambe96[b + 1] & 1u));
            sf.dibits[cursor++] = d;
        }
        // Gap between voice units (placeholder).
        cursor += 24;
        if (cursor >= kSymbols) break;
        ++used;
        if (used >= 18) break;
    }

    // Stamp NAC low bits into first symbols for offline identity checks.
    const uint16_t nac = m_cfg.nac & 0xFFFu;
    for (int i = 0; i < 12 && i < static_cast<int>(kSymbols); ++i) {
        sf.dibits[static_cast<size_t>(i)] = static_cast<uint8_t>((nac >> (11 - i)) & 0x1u);
    }
    // Superframe sequence nibble
    const uint8_t seq = static_cast<uint8_t>(m_superframeSeq++ & 0x0Fu);
    for (int i = 0; i < 4; ++i) {
        sf.dibits[static_cast<size_t>(16 + i)] = static_cast<uint8_t>((seq >> (3 - i)) & 1u);
    }

    sf.voiceFramesUsed = used;
    sf.valid = true;
    sf.note = used == 0 ? "empty-voice-skeleton" : "skeleton-with-voice-payload";
    return sf;
}

bool p25WriteTxDibitDump(const std::string& path, const P25Phase2TxSuperframe& sf)
{
    if (!sf.valid || sf.dibits.empty()) return false;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(sf.dibits.data()),
            static_cast<std::streamsize>(sf.dibits.size()));
    return static_cast<bool>(f);
}
