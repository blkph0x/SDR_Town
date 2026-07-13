/*
 * P25TrafficChannelProcessor.h
 *
 * Clean SDRTrunk-style persistent traffic channel processor for P25 Phase 1/2.
 * One instance per active voice call. Keeps superframe/mask/ESS state for the full call.
 */

#pragma once

#include "P25LiveDecoder.h"

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

class P25TrafficChannelProcessor {
public:
    P25TrafficChannelProcessor(uint64_t sessionId, uint32_t talkgroup, uint32_t voiceFreqHz, int grantedSlot);
    ~P25TrafficChannelProcessor();

    void processDibits(const int16_t* dibits, size_t count, uint64_t absoluteDibitIndex);
    void feedHardDibits(const std::vector<int>& dibits, uint64_t absoluteDibitIndex);
    void observeDecodeResult(const P25LiveDecodeResult& result, uint64_t absoluteDibitIndex);
    void setPhase2MaskParameters(uint16_t nac, uint32_t wacn, uint16_t systemId);
    void clearPhase2MaskParameters();

    bool isCallStillActive() const;
    bool mayEmitSustainedAudio() const;
    void requestTeardown(const std::string& reason);

    struct Diag {
        uint32_t talkgroup = 0;
        uint64_t sessionId = 0;
        int grantedSlot = -1;
        uint32_t voiceFreqHz = 0;
        int p2bursts = 0;
        int p2vcw = 0;
        int p2mac = 0;
        int p2macPdus = 0;
        int p2macCrcValid = 0;
        int p2sf = 0;
        int p2mask = 0;
        bool sfLocked = false;
        bool maskLocked = false;
        bool macTrusted = false;
        bool essTrusted = false;
        bool encrypted = false;
        bool audioOpen = false;
        bool macPttSeen = false;
        bool macActiveSeen = false;
        bool macEndPttSeen = false;
        bool macIdleSeen = false;
        bool macHangtimeSeen = false;
        bool callEnded = false;
        std::string state;
        std::string endReason;
        std::string teardownReason;
        uint64_t lastActiveMs = 0;
        uint64_t lastVoiceMs = 0;
        uint64_t lastMacMs = 0;
        uint64_t lastEssMs = 0;
        uint64_t endedMs = 0;
        uint64_t lastAbsoluteDibit = 0;
    };

    Diag getDiag() const;

private:
    uint64_t m_sessionId = 0;
    uint32_t m_talkgroup = 0;
    uint32_t m_voiceFreqHz = 0;
    int      m_grantedSlot = -1;

    std::atomic<int>  m_p2bursts{0};
    std::atomic<int>  m_p2vcw{0};
    std::atomic<int>  m_p2mac{0};
    std::atomic<int>  m_p2macPdus{0};
    std::atomic<int>  m_p2macCrcValid{0};
    std::atomic<int>  m_p2sf{0};
    std::atomic<int>  m_p2mask{0};

    std::atomic<bool> m_sfLocked{false};
    std::atomic<bool> m_maskLocked{false};
    std::atomic<bool> m_macTrusted{false};
    std::atomic<bool> m_essTrusted{false};
    std::atomic<bool> m_encrypted{false};
    std::atomic<bool> m_audioOpen{false};
    std::atomic<bool> m_teardownRequested{false};
    std::atomic<bool> m_macPttSeen{false};
    std::atomic<bool> m_macActiveSeen{false};
    std::atomic<bool> m_macEndPttSeen{false};
    std::atomic<bool> m_macIdleSeen{false};
    std::atomic<bool> m_macHangtimeSeen{false};
    std::atomic<bool> m_callEnded{false};

    std::atomic<uint64_t> m_lastActiveMs{0};
    std::atomic<uint64_t> m_lastVoiceMs{0};
    std::atomic<uint64_t> m_lastMacMs{0};
    std::atomic<uint64_t> m_lastEssMs{0};
    std::atomic<uint64_t> m_endedMs{0};
    std::atomic<uint64_t> m_createdMs{0};
    std::atomic<uint64_t> m_lastAbsoluteDibit{0};

    mutable std::mutex m_mutex;
    mutable std::mutex m_decoderMutex;
    P25LiveDecoder m_decoder;
    std::string m_teardownReason;
    std::string m_endReason;

    static constexpr uint64_t kMaxSilenceMs = 6000;  // 6s max, much shorter after voice to avoid sticking on inactive/noisy TGs
    static constexpr uint64_t kEndedHoldMs = 750;
    static constexpr uint64_t kHangtimeHoldMs = 1500;
};
