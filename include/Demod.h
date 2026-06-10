#pragma once
#include <vector>
#include <complex>
#include <cstddef>
#include <cstdint>

enum class DemodMode { NFM, WFM, AM, USB, LSB, AUTO };

double detectChannelBandwidth(const std::vector<float>& powerDb, double sampleRate);
DemodMode classifyMode(const std::vector<float>& powerDb, double sampleRate, double centerFreq);

// Per-receiver demodulator with its own state (P1: eliminates global/static bleed between
// receivers/modes/retunes, enables clean synthetic unit tests, and prepares for polyphase streaming channelizer).
class Demodulator {
public:
    Demodulator();
    ~Demodulator();

    // Same signature as the old free function. All previous "static" FIR/IIR/phase/resampler/AGC
    // state is now per-instance member state and is reset on large retune or mode change.
    std::vector<float> demodulateToAudio(const std::vector<std::complex<float>>& iq,
                                         double sr, double cf, double target,
                                         DemodMode mode,
                                         double& rmsOut,
                                         double lpfHz = 0,
                                         double squelchDb = -90,
                                         double gain = 1.0,
                                         double wfmDeTauUs = 75.0,
                                         double wfmPilotNotchR = 0.96,
                                         double channelBwHz = 0,
                                         size_t target_audio_samples = 0,
                                         double outputRate = 48000.0);

    // Explicit reset (call on large freq jump or mode switch if you want to be sure).
    void resetState();

private:
    // All previous global/static DSP state moved here (one set per Demodulator instance).
    std::complex<float> prev{1,0};
    double ph = 0;
    std::vector<std::complex<float>> firDelay;
    std::vector<float> chanTaps;
    double lastBw = -1, lastS = -1;

    float agcGain = 1.0f;
    float lpA = 0, lpB = 0, dcv = 0;
    float des = 0;
    float nx1=0, nx2=0, ny1=0, ny2=0;
    float flp1=0, flp2=0;
    float histYm2 = 0, histYm1 = 0, histY0 = 0;
    bool haveHist = false;

    bool dspStateNeedsReset = false;
    double lastResetTarget = -1e12;
    DemodMode lastResetMode = DemodMode::NFM;

    // Internal rate for the channelizer/decimator path (WFM uses ~192 kHz).
    double internalRate = 48000.0;

    // S0-6 (P2) + P1 calibration: smooth squelch state + live RMS + open/closed for indicator.
    float squelchGateGain = 0.0f;
    int   squelchHangLeft = 0;
    double lastRmsDb = -100.0;

    // Per-mode squelch defaults and hysteresis helpers (calibration support).
    double getSquelchDefaultForMode(DemodMode m) const;
    bool isSquelchOpen() const { return squelchGateGain > 0.15f; }
    double getLastRmsDb() const { return lastRmsDb; }
};

// Back-compat free functions (thin wrappers around a shared instance or the old implementation
// during transition). New code should prefer Demodulator instances.
std::vector<float> demodulateToAudio(const std::vector<std::complex<float>>& iq, double sr, double cf, double target, DemodMode mode, double& rmsOut, double lpfHz = 0, double squelchDb = -90, double gain = 1.0, double wfmDeTauUs = 75.0, double wfmPilotNotchR = 0.96, double channelBwHz = 0, size_t target_audio_samples = 0, double outputRate = 48000.0);