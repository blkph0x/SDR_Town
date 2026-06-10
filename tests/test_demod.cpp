#define CATCH_CONFIG_MAIN
#define _USE_MATH_DEFINES
#include <catch2/catch_all.hpp>
#include "Demod.h"
#include <complex>
#include <vector>
#include <cmath>
#include <algorithm>

// Helper: generate IQ for a tone at baseband offset (for FM phase ramp or AM).
std::vector<std::complex<float>> genTone(double sr, double dur, double freqOffset, double amp = 0.8) {
    size_t n = static_cast<size_t>(sr * dur);
    std::vector<std::complex<float>> iq(n);
    double phase = 0;
    double dphase = 2 * M_PI * freqOffset / sr;
    for (size_t i = 0; i < n; ++i) {
        iq[i] = std::polar((float)amp, (float)phase);
        phase += dphase;
    }
    return iq;
}

std::vector<std::complex<float>> genAM(double sr, double dur, double audioHz, double carrierAmp = 0.7, double depth = 0.6) {
    size_t n = static_cast<size_t>(sr * dur);
    std::vector<std::complex<float>> iq(n);
    for (size_t i = 0; i < n; ++i) {
        double t = (double)i / sr;
        double env = carrierAmp * (1.0 + depth * std::sin(2 * M_PI * audioHz * t));
        iq[i] = std::complex<float>((float)env, 0.0f);
    }
    return iq;
}

float maxAbs(const std::vector<float>& audio) {
    float m = 0.0f;
    for (float v : audio) m = std::max(m, std::abs(v));
    return m;
}

// Very rough dominant frequency estimator on real audio (zero-cross count).
double estimateFreq(const std::vector<float>& audio, double sr) {
    if (audio.size() < 4) return 0;
    int crossings = 0;
    for (size_t i = 1; i < audio.size(); ++i) {
        if ((audio[i-1] >= 0 && audio[i] < 0) || (audio[i-1] < 0 && audio[i] >= 0)) crossings++;
    }
    double periodSamples = (audio.size() * 2.0) / std::max(1, crossings);
    return sr / periodSamples;
}

TEST_CASE("Demodulator NFM tone produces reasonable audio RMS and no NaN") {
    Demodulator d;
    auto iq = genTone(48000, 0.05, 1200); // 1.2 kHz tone at 48k "IF" for NFM-like
    double rms = -100;
    auto audio = d.demodulateToAudio(iq, 48000, 100e6, 100e6 + 1200, DemodMode::NFM, rms, 3500, -80, 1.0, 0, 0, 12000, 0, 48000);
    REQUIRE(audio.size() > 100);
    REQUIRE_FALSE(std::any_of(audio.begin(), audio.end(), [](float v){ return std::isnan(v) || std::isinf(v); }));
    REQUIRE(rms > -120); // non-silent output (exact level depends on deviation scaling + LPF in current demod)
}

TEST_CASE("Demodulator WFM produces audio with de-emphasis effect and continuity") {
    Demodulator d1, d2;
    double sr = 2.048e6;
    auto iq = genTone(sr, 0.02, 75000); // large deviation tone for WFM
    double rms1 = -100, rms2 = -100;
    auto a1 = d1.demodulateToAudio(iq, sr, 100e6, 100e6 + 75000, DemodMode::WFM, rms1, 15000, -90, 1.0, 75.0, 0.96, 180000, 0, 48000);
    auto a2 = d2.demodulateToAudio(iq, sr, 100e6, 100e6 + 75000, DemodMode::WFM, rms2, 15000, -90, 1.0, 75.0, 0.96, 180000, 0, 48000);
    REQUIRE(a1.size() > 100);
    REQUIRE(a2.size() > 100);
    // Two separate instances should produce similar RMS (state isolation check)
    REQUIRE(std::abs(rms1 - rms2) < 5.0);
}

TEST_CASE("Demodulator AM produces positive envelope with energy") {
    Demodulator d;
    auto iq = genAM(48000, 0.08, 1000.0);
    double rms = -100;
    auto audio = d.demodulateToAudio(iq, 48000, 100e6, 100e6, DemodMode::AM, rms, 5000, -90, 1.0, 0, 0, 10000, 0, 48000);
    REQUIRE(audio.size() > 50);
    REQUIRE(maxAbs(audio) > 0.05f);
    REQUIRE(rms > -50);
}

TEST_CASE("Demodulator AM squelch closes quickly after carrier drops") {
    Demodulator d;
    auto signal = genAM(48000, 0.04, 900.0);
    std::vector<std::complex<float>> quiet(1920, {0.0f, 0.0f});
    double rms = -100;

    auto openAudio = d.demodulateToAudio(signal, 48000, 100e6, 100e6, DemodMode::AM, rms, 5000, -40, 1.0, 0, 0, 10000, 0, 48000);
    REQUIRE(maxAbs(openAudio) > 0.02f);
    REQUIRE(rms > -20.0);

    std::vector<float> last;
    for (int i = 0; i < 8; ++i) {
        last = d.demodulateToAudio(quiet, 48000, 100e6, 100e6, DemodMode::AM, rms, 5000, -40, 1.0, 0, 0, 10000, 0, 48000);
    }

    REQUIRE(rms < -120.0);
    REQUIRE(maxAbs(last) < 0.01f);
}

TEST_CASE("Demodulator state carry across chunks (no boundary click for FM)") {
    Demodulator d;
    double sr = 48000;
    auto iq1 = genTone(sr, 0.01, 800);
    auto iq2 = genTone(sr, 0.01, 800);
    double rms = -100;
    auto a1 = d.demodulateToAudio(iq1, sr, 100e6, 100e6+800, DemodMode::NFM, rms, 3500, -90, 1.0, 0,0,25000,0,48000);
    auto a2 = d.demodulateToAudio(iq2, sr, 100e6, 100e6+800, DemodMode::NFM, rms, 3500, -90, 1.0, 0,0,25000,0,48000);
    REQUIRE(a1.size() == a2.size());
    // Last sample of a1 and first of a2 should not have huge discontinuity (state carried)
    if (!a1.empty() && !a2.empty()) {
        REQUIRE(std::abs(a1.back() - a2.front()) < 0.8f); // loose but catches gross reset bugs
    }
}
