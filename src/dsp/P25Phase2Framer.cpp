#include "dsp/P25Phase2Framer.h"

#include "dsp/P25DspTypes.h"
#include "dsp/P25FilterCache.h"

namespace p25dsp {

namespace {
constexpr uint64_t kSyncMask = (1ull << (p25dsp::kPhase2FrameSyncDibits * 2)) - 1ull;
constexpr uint64_t kSyncWord = p25dsp::kPhase2FrameSyncWord;

int popcount64(uint64_t v) noexcept
{
#if defined(_MSC_VER)
    return static_cast<int>(__popcnt64(v));
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(v);
#else
    int count = 0;
    while (v) {
        count += static_cast<int>(v & 1u);
        v >>= 1;
    }
    return count;
#endif
}

} // namespace

void P25Phase2Framer::reset() noexcept
{
    m_absoluteDibit = 0;
    m_syncRegister = 0;
    m_synchronized = false;
    m_syncConfidence = 0;
    m_inSyncAllowance = 0;
    m_burstFill = 0;
    m_burstBody = {};
    m_superframeRing = {};
    m_superframeFill = 0;
    m_pendingBursts.clear();
    m_pendingSuperframes.clear();
}

int P25Phase2Framer::syncHammingDistance(uint64_t reg, uint64_t expected) noexcept
{
    return popcount64((reg ^ expected) & kSyncMask);
}

void P25Phase2Framer::tryEmitBurst(int syncErrors, bool inverted, int offsetCorrection)
{
    P25Phase2FramerBurst burst;
    burst.dibits = m_burstBody;
    burst.absoluteStartDibit = m_absoluteDibit >= kPhase2BurstDibits
        ? m_absoluteDibit - kPhase2BurstDibits
        : 0;
    burst.syncErrors = syncErrors;
    burst.inverted = inverted;
    burst.dibitOffsetCorrection = offsetCorrection;
    m_pendingBursts.push_back(burst);
    m_burstFill = 0;
    if (m_inSyncAllowance > 0) {
        --m_inSyncAllowance;
    }
}

void P25Phase2Framer::tryEmitSuperframe()
{
    if (m_superframeFill < kPhase2SuperframeDibits) return;

    // SDRTrunk checks sync at dibit offsets 360 and 540 within the 720-dibit fragment.
    constexpr size_t kSync1Index = 360;
    constexpr size_t kSync2Index = 540;
    const auto syncAt = [&](size_t index) {
        uint64_t reg = 0;
        for (size_t i = 0; i < p25dsp::kPhase2FrameSyncDibits; ++i) {
            const size_t pos = index + i;
            if (pos >= m_superframeFill) return 999;
            const uint64_t dib = static_cast<uint64_t>(m_superframeRing[pos] & 0x03);
            reg = ((reg << 2) | dib) & kSyncMask;
        }
        return syncHammingDistance(reg, kSyncWord);
    };

    const int sync1 = syncAt(kSync1Index);
    const int sync2 = syncAt(kSync2Index);
    const int threshold = m_synchronized ? kSyncThresholdSynchronized : kSyncThresholdUnsynchronized;
    int offsetCorrection = 0;

    if (sync1 > threshold) {
        offsetCorrection = checkSynchronizedOffset(static_cast<int>(kSync1Index));
    } else if (sync2 > threshold) {
        const int sync2Offset = checkSynchronizedOffset(static_cast<int>(kSync2Index));
        if (sync2Offset != 999) offsetCorrection = sync2Offset;
    }

    if (sync1 <= threshold || sync2 <= threshold || offsetCorrection != 999) {
        P25Phase2FramerSuperframe frame;
        for (size_t i = 0; i < kPhase2SuperframeDibits; ++i) {
            const int src = static_cast<int>(i) + offsetCorrection;
            frame.dibits[i] = (src >= 0 && static_cast<size_t>(src) < m_superframeFill)
                ? m_superframeRing[static_cast<size_t>(src)]
                : 0;
        }
        frame.absoluteStartDibit = m_absoluteDibit >= kPhase2SuperframeDibits
            ? m_absoluteDibit - kPhase2SuperframeDibits
            : 0;
        frame.sync1Errors = sync1;
        frame.sync2Errors = sync2;
        frame.dibitOffsetCorrection = offsetCorrection == 999 ? 0 : offsetCorrection;
        m_pendingSuperframes.push_back(frame);
    }

    if (m_superframeFill > kPhase2SuperframeDibits / 2) {
        const size_t keep = std::min(m_superframeFill, kPhase2SuperframeDibits + 4);
        for (size_t i = 0; i < keep && i + (m_superframeFill - keep) < m_superframeFill; ++i) {
            m_superframeRing[i] = m_superframeRing[m_superframeFill - keep + i];
        }
        m_superframeFill = keep;
    }
}

int P25Phase2Framer::checkSynchronizedOffset(int baseIndex) const noexcept
{
    // SDRTrunk getSynchronizedSyncOffset: test ±1/±2 dibits with relaxed thresholds.
    const auto scoreAt = [&](int delta) {
        uint64_t reg = 0;
        const int index = baseIndex + delta;
        if (index < 0) return 999;
        for (size_t i = 0; i < p25dsp::kPhase2FrameSyncDibits; ++i) {
            const size_t pos = static_cast<size_t>(index) + i;
            if (pos >= m_superframeFill) return 999;
            const uint64_t dib = static_cast<uint64_t>(m_superframeRing[pos] & 0x03);
            reg = ((reg << 2) | dib) & kSyncMask;
        }
        return syncHammingDistance(reg, kSyncWord);
    };

    if (scoreAt(-1) <= kSyncThresholdSynchronized - 1) return -1;
    if (scoreAt(1) <= kSyncThresholdSynchronized - 1) return 1;
    if (scoreAt(-2) <= kSyncThresholdSynchronized - 2) return -2;
    if (scoreAt(2) <= kSyncThresholdSynchronized - 2) return 2;
    return 999;
}

void P25Phase2Framer::consumeDibits(std::span<const int> dibits)
{
    for (int dibit : dibits) {
        const int normalized = dibit & 0x03;
        m_syncRegister = ((m_syncRegister << 2) | static_cast<uint64_t>(normalized)) & kSyncMask;
        ++m_absoluteDibit;

        const int normalErrors = syncHammingDistance(m_syncRegister, kSyncWord);
        const int invertedErrors = syncHammingDistance(m_syncRegister, kSyncWord ^ kSyncMask);
        const int threshold = m_synchronized ? kSyncThresholdSynchronized : kSyncThresholdUnsynchronized;

        if (normalErrors <= threshold || invertedErrors <= threshold) {
            // OP25 p25p2_framer::rx_sym: on sync hit, seed 40-bit sync then set d_in_sync=10.
            m_synchronized = true;
            m_syncConfidence = std::min(100, m_syncConfidence + 10);
            m_inSyncAllowance = 10;
            m_burstFill = 0;
            uint64_t reg = m_syncRegister;
            for (int i = static_cast<int>(p25dsp::kPhase2FrameSyncDibits) - 1; i >= 0; --i) {
                m_burstBody[static_cast<size_t>(i)] = static_cast<int>(reg & 0x03u);
                reg >>= 2;
            }
            m_burstFill = p25dsp::kPhase2FrameSyncDibits;
            continue;
        }

        if (m_synchronized && m_inSyncAllowance > 0) {
            if (m_burstFill < kPhase2BurstDibits) {
                m_burstBody[m_burstFill++] = normalized;
            }
            if (m_burstFill >= kPhase2BurstDibits) {
                tryEmitBurst(normalErrors, invertedErrors < normalErrors, 0);
            }
        } else if (!m_synchronized) {
            m_syncConfidence = std::max(0, m_syncConfidence - 1);
        }

        if (m_superframeFill < m_superframeRing.size()) {
            m_superframeRing[m_superframeFill++] = normalized;
        }
        if (m_superframeFill >= kPhase2SuperframeDibits) {
            tryEmitSuperframe();
        }
    }
}

std::vector<P25Phase2FramerBurst> P25Phase2Framer::takeBursts()
{
    std::vector<P25Phase2FramerBurst> out;
    out.swap(m_pendingBursts);
    return out;
}

std::vector<P25Phase2FramerSuperframe> P25Phase2Framer::takeSuperframes()
{
    std::vector<P25Phase2FramerSuperframe> out;
    out.swap(m_pendingSuperframes);
    return out;
}

} // namespace p25dsp
