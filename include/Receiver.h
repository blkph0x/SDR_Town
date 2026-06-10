#pragma once

#include "Demod.h"
#include <vector>
#include <cstddef>

// Basic per-receiver state holder.
// Goal: each SDR/monitor path gets its own demod, audio targets, recorder stub, etc.
// This eliminates global monitor* state and enables true multi-device independence.

struct Receiver {
    size_t deviceIndex = 0;           // which DeviceManager device this receiver uses
    Demodulator demod;                // own demod instance (already per-state)

    double freqHz = 100e6;
    DemodMode mode = DemodMode::NFM;
    double channelBwHz = 25000.0;
    double lpfHz = 3500.0;
    double squelchDb = -90.0;

    // P1 audit: explicit separation (RF is hardware sensitivity on the SDR; audio is post-demod gain; display for viz/spectrum scaling).
    // setLiveGain on DeviceManager applies RF immediately to running Soapy device when possible.
    double rfGainDb = 20.0;
    double audioGain = 1.0;
    double displayGain = 1.0;

    double gain = 1.0; // legacy alias (maps to audioGain during transition)
    double wfmDeTauUs = 75.0;
    double wfmPilotNotchR = 0.96;

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
    void resetDemodState() { demod.resetState(); lastConsumedAbsolute = 0; }
};

// Placeholder for future per-receiver audio push (currently falls back to global engine)
// void pushReceiverAudio(Receiver& rx, const float* samples, size_t count);
