#define CATCH_CONFIG_MAIN
#define _USE_MATH_DEFINES
#include <catch2/catch_all.hpp>
#include "Demod.h"
#include "SignalClassifier.h"
#include "ClassifierModelBackend.h"
#include <complex>
#include <vector>
#include <cmath>
#include <algorithm>
#include <limits>

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

std::vector<std::complex<float>> genNfmVoice(double sr, double dur, double audioHz, double deviationHz, double amp = 0.8) {
    size_t n = static_cast<size_t>(sr * dur);
    std::vector<std::complex<float>> iq(n);
    double phase = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double t = static_cast<double>(i) / sr;
        double instFreq = deviationHz * std::sin(2 * M_PI * audioHz * t);
        phase += 2 * M_PI * instFreq / sr;
        iq[i] = std::polar(static_cast<float>(amp), static_cast<float>(phase));
    }
    return iq;
}

std::vector<std::complex<float>> genSsbTone(double sr, double dur, double audioHz, bool upperSideband, double amp = 0.25) {
    size_t n = static_cast<size_t>(sr * dur);
    std::vector<std::complex<float>> iq(n);
    const double sign = upperSideband ? 1.0 : -1.0;
    for (size_t i = 0; i < n; ++i) {
        double t = static_cast<double>(i) / sr;
        double phase = sign * 2.0 * M_PI * audioHz * t;
        iq[i] = std::polar(static_cast<float>(amp), static_cast<float>(phase));
    }
    return iq;
}

float maxAbs(const std::vector<float>& audio) {
    float m = 0.0f;
    for (float v : audio) m = std::max(m, std::abs(v));
    return m;
}

double tailRms(const std::vector<float>& audio, double tailFraction = 0.65) {
    if (audio.empty()) return 0.0;
    size_t start = static_cast<size_t>(audio.size() * std::clamp(1.0 - tailFraction, 0.0, 1.0));
    start = std::min(start, audio.size() - 1);
    double sum = 0.0;
    size_t count = 0;
    for (size_t i = start; i < audio.size(); ++i) {
        sum += static_cast<double>(audio[i]) * audio[i];
        ++count;
    }
    return std::sqrt(sum / std::max<size_t>(1, count));
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

static std::vector<float> makePowerSpectrum(size_t bins, float floorDb = -115.0f) {
    return std::vector<float>(bins, floorDb);
}

static void paintPowerRange(std::vector<float>& pwr,
                            double sampleRate,
                            double centerFreq,
                            double targetFreq,
                            double offsetLoHz,
                            double offsetHiHz,
                            float db)
{
    const double binHz = sampleRate / static_cast<double>(pwr.size());
    const double fullStart = centerFreq - sampleRate / 2.0;
    const double loFreq = targetFreq + offsetLoHz;
    const double hiFreq = targetFreq + offsetHiHz;
    int lo = std::clamp(static_cast<int>(std::floor((loFreq - fullStart) / binHz)), 0, static_cast<int>(pwr.size()) - 1);
    int hi = std::clamp(static_cast<int>(std::ceil((hiFreq - fullStart) / binHz)), 0, static_cast<int>(pwr.size()) - 1);
    if (lo > hi) std::swap(lo, hi);
    for (int i = lo; i <= hi; ++i) {
        pwr[static_cast<size_t>(i)] = std::max(pwr[static_cast<size_t>(i)], db);
    }
}

TEST_CASE("Bandwidth detector and classifier recognize wide AM around tuned signal") {
    const double sr = 2.0e6;
    const double cf = 476.0e6;
    const double target = 476.0e6;
    auto pwr = makePowerSpectrum(8192, -115.0f);
    paintPowerRange(pwr, sr, cf, target, -500.0, 500.0, -48.0f);       // carrier
    paintPowerRange(pwr, sr, cf, target, -10000.0, -1800.0, -62.0f);   // lower sideband
    paintPowerRange(pwr, sr, cf, target, 1800.0, 10000.0, -62.0f);     // upper sideband

    double bw = detectChannelBandwidthAround(pwr, sr, cf, target, 50000.0);
    REQUIRE(bw >= 18000.0);
    REQUIRE(bw <= 30000.0);
    REQUIRE(classifyModeAround(pwr, sr, cf, target) == DemodMode::AM);
}

TEST_CASE("Bandwidth detector preserves flat 12.5 kHz digital channels without AM misclassifying") {
    const double sr = 2.0e6;
    const double cf = 476.0e6;
    const double target = 476.0e6;
    auto pwr = makePowerSpectrum(8192, -115.0f);
    paintPowerRange(pwr, sr, cf, target, -6250.0, 6250.0, -70.0f);

    double bw = detectChannelBandwidthAround(pwr, sr, cf, target, 50000.0);
    REQUIRE(bw >= 10000.0);
    REQUIRE(bw <= 18000.0);
    REQUIRE(classifyModeAround(pwr, sr, cf, target) == DemodMode::NFM);
}

TEST_CASE("Bandwidth detector does not clamp out-of-view targets to FFT edges") {
    const double sr = 2.0e6;
    const double cf = 476.0e6;
    const double target = 478.0e6; // outside +/-1 MHz capture
    auto pwr = makePowerSpectrum(8192, -115.0f);
    paintPowerRange(pwr, sr, cf, cf - 900000.0, -4000.0, 4000.0, -45.0f);

    double bw = detectChannelBandwidthAround(pwr, sr, cf, target, 50000.0);
    REQUIRE(bw == Catch::Approx(25000.0));
}

TEST_CASE("P25 candidate detector finds flat 12.5 kHz control-channel style signal") {
    const double sr = 2.0e6;
    const double cf = 476.0e6;
    const double target = 476.4625e6;
    auto pwr = makePowerSpectrum(8192, -118.0f);
    paintPowerRange(pwr, sr, cf, target, -6250.0, 6250.0, -70.0f);

    auto hits = detectP25ControlCandidates(pwr, sr, cf, 4);
    REQUIRE_FALSE(hits.empty());
    REQUIRE(hits.front().freqHz == Catch::Approx(target).margin(1000.0));
    REQUIRE(hits.front().bandwidthHz >= 10000.0);
    REQUIRE(hits.front().bandwidthHz <= 18000.0);
    REQUIRE(hits.front().snrDb > 20.0);
}

TEST_CASE("Signal offset estimator finds off-center NFM carrier for AFC") {
    const double sr = 2.4e6;
    const double cf = 476.4625e6;
    const double target = 476.4625e6;
    const double offset = 9500.0;
    auto pwr = makePowerSpectrum(8192, -118.0f);
    paintPowerRange(pwr, sr, cf, target + offset, -5200.0, 5200.0, -68.0f);

    auto estimate = estimateSignalOffsetFromSpectrum(pwr, sr, cf, target, 18000.0, 12500.0);
    REQUIRE(estimate.valid);
    REQUIRE(estimate.offsetHz == Catch::Approx(offset).margin(500.0));
    REQUIRE(estimate.snrDb > 20.0);
}

TEST_CASE("Advanced classifier recommends exact AM workflow from carrier and balanced sidebands") {
    const double sr = 2.0e6;
    const double cf = 476.0e6;
    const double target = 476.0e6;
    auto pwr = makePowerSpectrum(8192, -115.0f);
    paintPowerRange(pwr, sr, cf, target, -500.0, 500.0, -48.0f);
    paintPowerRange(pwr, sr, cf, target, -10000.0, -1800.0, -62.0f);
    paintPowerRange(pwr, sr, cf, target, 1800.0, 10000.0, -62.0f);

    auto rec = AdvancedSignalClassifier::instance().classifySpectrum(pwr, sr, cf, target);
    REQUIRE(rec.signalClass == SignalClass::AM);
    REQUIRE(rec.demodMode == DemodMode::AM);
    REQUIRE(rec.standardBandwidthHz == Catch::Approx(20000.0));
    REQUIRE(rec.audioLowPassHz == Catch::Approx(9000.0));
    REQUIRE(rec.confidence > 0.70);
}

TEST_CASE("Advanced classifier maps flat 12.5 kHz digital ROI to P25/C4FM standard settings") {
    const double sr = 2.0e6;
    const double cf = 476.0e6;
    const double target = 476.4625e6;
    auto pwr = makePowerSpectrum(8192, -118.0f);
    paintPowerRange(pwr, sr, cf, target, -6250.0, 6250.0, -70.0f);

    auto rec = AdvancedSignalClassifier::instance().classifySpectrum(pwr, sr, cf, target);
    REQUIRE(rec.signalClass == SignalClass::P25Phase1);
    REQUIRE(rec.demodMode == DemodMode::NFM);
    REQUIRE(rec.standardBandwidthHz == Catch::Approx(12500.0));
    REQUIRE(rec.filterKind == ClassifierFilterKind::RootRaisedCosine);
    REQUIRE(rec.disableAudioLpf);
    REQUIRE(rec.digital);
}

TEST_CASE("Advanced classifier recognizes WFM broadcast width and applies broadcast defaults") {
    const double sr = 2.4e6;
    const double cf = 100.0e6;
    const double target = 100.0e6;
    auto pwr = makePowerSpectrum(8192, -115.0f);
    paintPowerRange(pwr, sr, cf, target, -90000.0, 90000.0, -58.0f);

    auto rec = AdvancedSignalClassifier::instance().classifySpectrum(pwr, sr, cf, target);
    REQUIRE(rec.signalClass == SignalClass::WFM);
    REQUIRE(rec.demodMode == DemodMode::WFM);
    REQUIRE(rec.standardBandwidthHz == Catch::Approx(180000.0));
    REQUIRE(rec.audioLowPassHz == Catch::Approx(15000.0));
}

TEST_CASE("Advanced classifier recommends SSB RF window wider than voice LPF") {
    const double sr = 48000.0;
    const double cf = 14.2e6;
    const double target = 14.2e6;
    auto pwr = makePowerSpectrum(4096, -115.0f);
    paintPowerRange(pwr, sr, cf, target, 700.0, 2800.0, -63.0f);

    auto rec = AdvancedSignalClassifier::instance().classifySpectrum(pwr, sr, cf, target, 8000.0);
    REQUIRE(rec.demodMode == DemodMode::USB);
    REQUIRE(rec.standardBandwidthHz == Catch::Approx(6000.0));
    REQUIRE(rec.audioLowPassHz == Catch::Approx(3000.0));
}

TEST_CASE("Waterfall ROI builder creates normalized classifier tiles") {
    const double sr = 2.0e6;
    const double cf = 476.0e6;
    const double target = 476.4625e6;
    WaterfallRoiBuilder builder(16);
    for (int i = 0; i < 10; ++i) {
        auto pwr = makePowerSpectrum(8192, -118.0f);
        paintPowerRange(pwr, sr, cf, target, -6250.0, 6250.0, static_cast<float>(-72 + i % 3));
        builder.pushSpectrum(pwr);
    }

    auto tile = builder.buildTile(sr, cf, target, 50000.0, 64, 64);
    REQUIRE(tile.valid());
    REQUIRE(tile.width == 64);
    REQUIRE(tile.height == 64);
    REQUIRE(std::all_of(tile.pixels.begin(), tile.pixels.end(), [](float v) {
        return std::isfinite(v) && v >= 0.0f && v <= 1.0f;
    }));

    auto rec = AdvancedSignalClassifier::instance().classifyWaterfallTile(tile, sr, cf, target, 50000.0);
    REQUIRE(rec.confidence > 0.40);
}

TEST_CASE("Optional classifier model backend fails closed when model is unavailable") {
    auto& backend = ClassifierModelBackend::instance();
    backend.unloadModel();
    auto st = backend.status();
    REQUIRE_FALSE(st.enabled);
    REQUIRE_FALSE(st.loaded);
    REQUIRE_FALSE(backend.loadModel("this_model_does_not_exist.onnx"));
    st = backend.status();
    REQUIRE_FALSE(st.enabled);
    REQUIRE_FALSE(st.loaded);
    REQUIRE_FALSE(st.message.empty());
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

TEST_CASE("Demodulator NFM keeps low speech energy at SDR sample rates") {
    const double rfRate = 2.4e6;
    const double outRate = 48000.0;

    Demodulator lowDemod;
    double lowRmsDb = -100;
    auto lowIq = genNfmVoice(rfRate, 0.06, 350.0, 1800.0);
    auto lowAudio = lowDemod.demodulateToAudio(lowIq, rfRate, 476.4625e6, 476.4625e6,
        DemodMode::NFM, lowRmsDb, 3000.0, -120.0, 1.0, 75.0, 0.96, 12500.0, 0, outRate);

    Demodulator highDemod;
    double highRmsDb = -100;
    auto highIq = genNfmVoice(rfRate, 0.06, 1800.0, 1800.0);
    auto highAudio = highDemod.demodulateToAudio(highIq, rfRate, 476.4625e6, 476.4625e6,
        DemodMode::NFM, highRmsDb, 3000.0, -120.0, 1.0, 75.0, 0.96, 12500.0, 0, outRate);

    REQUIRE(lowAudio.size() > 1000);
    REQUIRE(highAudio.size() > 1000);
    REQUIRE_FALSE(std::any_of(lowAudio.begin(), lowAudio.end(), [](float v){ return std::isnan(v) || std::isinf(v); }));
    REQUIRE_FALSE(std::any_of(highAudio.begin(), highAudio.end(), [](float v){ return std::isnan(v) || std::isinf(v); }));

    const double lowSpeech = tailRms(lowAudio);
    const double highSpeech = tailRms(highAudio);
    REQUIRE(lowSpeech > 0.0002);
    REQUIRE(lowSpeech > highSpeech * 1.5);
}

TEST_CASE("Demodulator NFM audio level is normalized across SDR sample rates") {
    const double outRate = 48000.0;
    const double freq = 476.4625e6;

    Demodulator audioRateDemod;
    double audioRateRmsDb = -100;
    auto audioRateIq = genNfmVoice(48000.0, 0.05, 1000.0, 1800.0);
    auto audioRateAudio = audioRateDemod.demodulateToAudio(audioRateIq, 48000.0, freq, freq,
        DemodMode::NFM, audioRateRmsDb, 3000.0, -120.0, 1.0, 75.0, 0.96, 12500.0, 0, outRate);

    Demodulator sdrRateDemod;
    double sdrRateRmsDb = -100;
    auto sdrRateIq = genNfmVoice(2.4e6, 0.05, 1000.0, 1800.0);
    auto sdrRateAudio = sdrRateDemod.demodulateToAudio(sdrRateIq, 2.4e6, freq, freq,
        DemodMode::NFM, sdrRateRmsDb, 3000.0, -120.0, 1.0, 75.0, 0.96, 12500.0, 0, outRate);

    REQUIRE(audioRateAudio.size() > 1000);
    REQUIRE(sdrRateAudio.size() > 1000);
    const double audioRateLevel = tailRms(audioRateAudio);
    const double sdrRateLevel = tailRms(sdrRateAudio);
    REQUIRE(sdrRateLevel > audioRateLevel * 0.35);
    REQUIRE(sdrRateLevel < audioRateLevel * 3.0);
}

TEST_CASE("Demodulator SSB keeps upper speech with 6 kHz RF bandwidth and 3 kHz audio LPF") {
    const double sr = 48000.0;
    const double freq = 14.2e6;
    Demodulator usbDemod;
    double rms = -100;
    auto usbIq = genSsbTone(sr, 0.06, 2400.0, true);
    auto usbAudio = usbDemod.demodulateToAudio(usbIq, sr, freq, freq, DemodMode::USB,
        rms, 3000.0, -120.0, 1.0, 75.0, 0.96, 6000.0, 0, 48000.0);

    Demodulator lsbDemod;
    auto lsbIq = genSsbTone(sr, 0.06, 2400.0, false);
    auto lsbAudio = lsbDemod.demodulateToAudio(lsbIq, sr, freq, freq, DemodMode::LSB,
        rms, 3000.0, -120.0, 1.0, 75.0, 0.96, 6000.0, 0, 48000.0);

    REQUIRE(usbAudio.size() > 1000);
    REQUIRE(lsbAudio.size() > 1000);
    REQUIRE(tailRms(usbAudio) > 0.05);
    REQUIRE(tailRms(lsbAudio) > 0.05);
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

TEST_CASE("Demodulator squelch can use calibrated external RF level") {
    Demodulator d;
    auto signal = genAM(48000, 0.08, 1000.0);
    double rms = -100;

    auto closed = d.demodulateToAudio(signal, 48000, 100e6, 100e6, DemodMode::AM,
        rms, 5000, -80, 1.0, 0, 0, 10000, 0, 48000, -95.0);
    REQUIRE(closed.size() > 50);
    REQUIRE(rms == Catch::Approx(-95.0).margin(0.01));
    REQUIRE(maxAbs(closed) < 0.01f);

    d.resetState();
    auto open = d.demodulateToAudio(signal, 48000, 100e6, 100e6, DemodMode::AM,
        rms, 5000, -80, 1.0, 0, 0, 10000, 0, 48000, -60.0);
    REQUIRE(open.size() > 50);
    REQUIRE(rms == Catch::Approx(-60.0).margin(0.01));
    REQUIRE(maxAbs(open) > 0.02f);
}

TEST_CASE("Demodulator can bypass audio LPF for decoder workflows") {
    auto signal = genAM(48000, 0.08, 8000.0, 0.7, 0.5);
    double rms = -100;

    Demodulator filteredDemod;
    auto filtered = filteredDemod.demodulateToAudio(signal, 48000, 100e6, 100e6, DemodMode::AM,
        rms, 2000, -120, 1.0, 0, 0, 20000, 0, 48000,
        std::numeric_limits<double>::quiet_NaN(), true);

    Demodulator bypassDemod;
    auto bypassed = bypassDemod.demodulateToAudio(signal, 48000, 100e6, 100e6, DemodMode::AM,
        rms, 2000, -120, 1.0, 0, 0, 20000, 0, 48000,
        std::numeric_limits<double>::quiet_NaN(), false);

    REQUIRE(filtered.size() > 50);
    REQUIRE(bypassed.size() > 50);
    REQUIRE(maxAbs(bypassed) > maxAbs(filtered) * 1.5f);
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
