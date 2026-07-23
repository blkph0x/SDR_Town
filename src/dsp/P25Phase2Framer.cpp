#include "dsp/P25Phase2Framer.h"

#include "dsp/P25DspTypes.h"

#include <algorithm>

namespace p25dsp {

namespace {
constexpr uint64_t kSyncMask = (1ull << (p25dsp::kPhase2FrameSyncDibits * 2)) - 1ull;
constexpr uint64_t kSyncWord = p25dsp::kPhase2FrameSyncWord;
constexpr size_t kSync1Index = 360;
constexpr size_t kSync2Index = 540;
constexpr size_t kSplitDibitIndex = 540;

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
    m_ringWriteIndex = 0;
    m_ringTotalDibits = 0;
    m_superframeDibitsSinceEmit = 0;
    m_pendingBursts.clear();
    m_pendingSuperframes.clear();
}

int P25Phase2Framer::syncHammingDistance(uint64_t reg, uint64_t expected) noexcept
{
    return popcount64((reg ^ expected) & kSyncMask);
}

bool P25Phase2Framer::isValidSyncOffset(int offset) noexcept
{
    return offset >= kSyncOffsetMin && offset <= kSyncOffsetMax;
}

void P25Phase2Framer::expireSyncIfNeeded() noexcept
{
    if (m_inSyncAllowance == 0) {
        m_synchronized = false;
        m_syncConfidence = 0;
        m_burstFill = 0;
    }
}

void P25Phase2Framer::pushSuperframeDibit(int dibit) noexcept
{
    m_superframeRing[m_ringWriteIndex] = dibit & 0x03;
    m_ringWriteIndex = (m_ringWriteIndex + 1) % kSuperframeRingCapacity;
    ++m_ringTotalDibits;
    ++m_superframeDibitsSinceEmit;
}

int P25Phase2Framer::superframeRingAt(size_t ageFromNewest) const noexcept
{
    if (ageFromNewest >= m_ringTotalDibits || ageFromNewest >= kSuperframeRingCapacity) {
        return 0;
    }
    const size_t idx = (m_ringWriteIndex + kSuperframeRingCapacity - 1 - ageFromNewest) %
        kSuperframeRingCapacity;
    return m_superframeRing[idx];
}

int P25Phase2Framer::dibitInCurrentWindow(size_t posIn720, int shift) const noexcept
{
    const int age = static_cast<int>(kPhase2SuperframeDibits - 1 - posIn720) + shift;
    if (age < 0) return 0;
    return superframeRingAt(static_cast<size_t>(age));
}

int P25Phase2Framer::syncHammingAt(size_t syncStartIndex, int shift) const noexcept
{
    uint64_t reg = 0;
    for (size_t i = 0; i < kPhase2FrameSyncDibits; ++i) {
        const int dib = dibitInCurrentWindow(syncStartIndex + i, shift);
        reg = ((reg << 2) | static_cast<uint64_t>(dib & 0x03)) & kSyncMask;
    }
    return syncHammingDistance(reg, kSyncWord);
}

int P25Phase2Framer::checkSynchronizedOffset(size_t syncStartIndex) const noexcept
{
    if (syncHammingAt(syncStartIndex, -1) <= kSyncThresholdSynchronized - 1) return -1;
    if (syncHammingAt(syncStartIndex, 1) <= kSyncThresholdSynchronized - 1) return 1;
    if (syncHammingAt(syncStartIndex, -2) <= kSyncThresholdSynchronized - 2) return -2;
    if (syncHammingAt(syncStartIndex, 2) <= kSyncThresholdSynchronized - 2) return 2;
    return 999;
}

void P25Phase2Framer::emitSuperframeFragment(int sync1Errors,
                                             int sync2Errors,
                                             int sync1Offset,
                                             int sync2Offset,
                                             bool split)
{
    P25Phase2FramerSuperframe frame;
    for (size_t i = 0; i < kPhase2SuperframeDibits; ++i) {
        const int offset = (!split || static_cast<int>(i) < static_cast<int>(kSplitDibitIndex))
            ? sync1Offset
            : sync2Offset;
        frame.dibits[i] = dibitInCurrentWindow(i, offset);
    }
    frame.absoluteStartDibit = m_absoluteDibit >= kPhase2SuperframeDibits
        ? m_absoluteDibit - kPhase2SuperframeDibits
        : 0;
    frame.sync1Errors = sync1Errors;
    frame.sync2Errors = sync2Errors;
    frame.dibitOffsetCorrection = sync1Offset;
    frame.sync2OffsetCorrection = sync2Offset;
    frame.splitFragment = split;
    m_pendingSuperframes.push_back(frame);
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
        expireSyncIfNeeded();
    }
}

void P25Phase2Framer::tryEmitSuperframe()
{
    if (m_ringTotalDibits < kPhase2SuperframeDibits) return;
    if (m_superframeDibitsSinceEmit < kPhase2SuperframeDibits) return;

    const int threshold = synchronized() ? kSyncThresholdSynchronized : kSyncThresholdUnsynchronized;
    int sync1 = syncHammingAt(kSync1Index, 0);
    int sync2 = syncHammingAt(kSync2Index, 0);

    if (synchronized()) {
        if (sync1 <= threshold) {
            if (sync2 <= threshold) {
                emitSuperframeFragment(sync1, sync2, 0, 0, false);
                m_superframeDibitsSinceEmit = 0;
                return;
            }

            const int sync2Offset = checkSynchronizedOffset(kSync2Index);
            if (isValidSyncOffset(sync2Offset)) {
                sync2 = syncHammingAt(kSync2Index, sync2Offset);
                emitSuperframeFragment(sync1, sync2, 0, sync2Offset, true);
                m_synchronized = false;
                m_inSyncAllowance = 0;
                m_superframeDibitsSinceEmit = static_cast<uint64_t>(sync2Offset);
                return;
            }

            m_synchronized = false;
            m_inSyncAllowance = 0;
            return;
        }

        const int sync1Offset = checkSynchronizedOffset(kSync1Index);
        if (isValidSyncOffset(sync1Offset)) {
            sync1 = syncHammingAt(kSync1Index, sync1Offset);
            sync2 = syncHammingAt(kSync2Index, sync1Offset);
            if (sync2 <= threshold) {
                emitSuperframeFragment(sync1, sync2, sync1Offset, sync1Offset, false);
                m_synchronized = false;
                m_inSyncAllowance = 0;
                m_superframeDibitsSinceEmit = static_cast<uint64_t>(sync1Offset);
                return;
            }

            const int sync2Offset = checkSynchronizedOffset(kSync2Index + static_cast<size_t>(sync1Offset));
            if (isValidSyncOffset(sync2Offset)) {
                const int totalOffset = sync1Offset + sync2Offset;
                if (isValidSyncOffset(totalOffset)) {
                    sync2 = syncHammingAt(kSync2Index, sync2Offset);
                    emitSuperframeFragment(sync1, sync2, sync1Offset, sync2Offset, true);
                    m_synchronized = false;
                    m_inSyncAllowance = 0;
                    m_superframeDibitsSinceEmit = static_cast<uint64_t>(sync2Offset);
                    return;
                }
            }
        }

        m_synchronized = false;
        m_inSyncAllowance = 0;
        return;
    }

    if (sync1 <= threshold) {
        emitSuperframeFragment(sync1, sync2, 0, 0, false);
        m_synchronized = true;
        m_inSyncAllowance = 10;
        m_superframeDibitsSinceEmit = 0;
    }
}

void P25Phase2Framer::consumeDibits(std::span<const int> dibits)
{
    for (int dibit : dibits) {
        const int normalized = dibit & 0x03;
        m_syncRegister = ((m_syncRegister << 2) | static_cast<uint64_t>(normalized)) & kSyncMask;
        ++m_absoluteDibit;

        const int normalErrors = syncHammingDistance(m_syncRegister, kSyncWord);
        const int invertedErrors = syncHammingDistance(m_syncRegister, kSyncWord ^ kSyncMask);
        const int threshold = synchronized() ? kSyncThresholdSynchronized : kSyncThresholdUnsynchronized;

        if (normalErrors <= threshold || invertedErrors <= threshold) {
            m_synchronized = true;
            m_syncConfidence = std::min(100, m_syncConfidence + 10);
            m_inSyncAllowance = 10;
            m_burstFill = 0;
            uint64_t reg = m_syncRegister;
            for (int i = static_cast<int>(kPhase2FrameSyncDibits) - 1; i >= 0; --i) {
                m_burstBody[static_cast<size_t>(i)] = static_cast<int>(reg & 0x03u);
                reg >>= 2;
            }
            m_burstFill = kPhase2FrameSyncDibits;
            pushSuperframeDibit(normalized);
            if (m_superframeDibitsSinceEmit >= kPhase2SuperframeDibits) {
                tryEmitSuperframe();
            }
            continue;
        }

        if (synchronized()) {
            if (m_burstFill < kPhase2BurstDibits) {
                m_burstBody[m_burstFill++] = normalized;
            }
            if (m_burstFill >= kPhase2BurstDibits) {
                tryEmitBurst(normalErrors, invertedErrors < normalErrors, 0);
            }
        } else {
            m_syncConfidence = std::max(0, m_syncConfidence - 1);
        }

        pushSuperframeDibit(normalized);
        if (m_superframeDibitsSinceEmit >= kPhase2SuperframeDibits) {
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
