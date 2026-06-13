#pragma once

#include "Demod.h"
#include "P25LiveDecoder.h"
#include <vector>
#include <cstddef>
#include <mutex>

// Basic per-receiver state holder.
// Goal: each SDR/monitor path gets its own demod, audio targets, recorder stub, etc.
// This eliminates global monitor* state and enables true multi-device independence.

struct Receiver {
    mutable std::mutex stateMutex;

    size_t deviceIndex = 0;           // which DeviceManager device this receiver uses
    Demodulator demod;                // own demod instance (already per-state)

    double freqHz = 100e6;
    DemodMode mode = DemodMode::NFM;
    double channelBwHz = 12500.0;
    double lpfHz = 3000.0;
    bool audioLpfEnabled = true;
    double squelchDb = -105.0;

    // P1 audit: explicit separation (RF is hardware sensitivity on the SDR; audio is post-demod gain; display for viz/spectrum scaling).
    // setLiveGain on DeviceManager applies RF immediately to running Soapy device when possible.
    double rfGainDb = 20.0;
    double audioGain = 1.0;
    double displayGain = 1.0;

    double gain = 1.0; // legacy alias (maps to audioGain during transition)
    double wfmDeTauUs = 75.0;
    double wfmPilotNotchR = 0.96;

    // P25 Phase 1 voice follow state. When enabled, the DSP worker bypasses the
    // analog NFM demodulator and routes valid LDU IMBE frames through mbelib.
    bool p25VoiceDecodeEnabled = false;
    bool p25VoiceClearKnown = false;
    bool p25VoiceEncrypted = false;
    uint32_t p25VoiceTalkgroupId = 0;
    P25LiveDecoder p25VoiceLiveDecoder;
    P25ImbeVoiceDecoder p25ImbeVoiceDecoder;

    // Narrow-FM AFC: keeps the user tuned to the nominal channel while DSP
    // recenters the demodulator when SDR PPM/tuning error moves the carrier.
    bool afcEnabled = true;
    bool afcLocked = false;
    double afcOffsetHz = 0.0;

    // Future: per-receiver audio routing
    // std::vector<size_t> audioOutputIndices;  // which ActiveOutputs this receiver feeds

    // Stubs for future features (recording, waterfall data, scheduler)
    bool recordingAudio = false;
    bool recordingIQ = false;
    // TODO: std::unique_ptr<Recorder> recorder;
    // TODO: per-receiver spectrum/waterfall buffer
    // TODO: schedule / hunter / sat tracking state

    bool active = false;              // is this receiver currently processing?

    // S0 / audit-followup-2: per-receiver read cursor (absolute samples) for the device's IQ ring.
    // Allows each rx to consume its own new chronological data without fighting other rxs on the same device.
    uint64_t lastConsumedAbsolute = 0;

    // Simple reset helper (calls into Demodulator)
    void resetDemodState() { demod.resetState(); lastConsumedAbsolute = 0; afcLocked = false; afcOffsetHz = 0.0; }
    void resetP25VoiceState() { p25VoiceLiveDecoder.reset(); p25ImbeVoiceDecoder = P25ImbeVoiceDecoder(); }

    // Force immediate squelch gate close (bypass hang) when user raises threshold via main controls or CLI.
    // Makes "set sq higher" live without requiring a freq retune/click (which previously forced a full reset).
    void resetSquelchGate() { demod.resetSquelchGate(); }
};

// Placeholder for future per-receiver audio push (currently falls back to global engine)
// void pushReceiverAudio(Receiver& rx, const float* samples, size_t count);
