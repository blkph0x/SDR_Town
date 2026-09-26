#include <catch2/catch_test_macros.hpp>
#include "DeviceManager.h"
#include "InmarsatEngine.h"
#include "SdrplayControl.h"
#ifdef HAVE_SOAPYSDR
#include "SdrplayControlFixture.h"
#include <SoapySDR/Registry.hpp>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <algorithm>

namespace {
DeviceInfo rsp() {
    DeviceInfo d; d.driver = "sdrplay_controls_fixture"; d.label = "RSPdx";
    d.isSdrplay = true; d.antenna = "RX"; return d;
}
void apply(SdrplayControlFixture& driver, DeviceInfo& d, SdrplayControl::Change c) {
    auto desired = d;
    SdrplayControl::prepare(desired, c);
    SdrplayControl::applyChange(driver, desired, c);
    d = desired;
}
using SdrplayControl::Kind;
}

TEST_CASE("SDRplay live probe repairs generic antenna and exposes only real controls", "[sdrplay][controls]") {
    auto d = rsp(); SdrplayControlFixture driver;
    REQUIRE_THROWS(SdrplayControl::prepare(d, {Kind::Setting, "biasT_ctrl", "true"}));
    SdrplayControl::probe(driver, d);
    REQUIRE(d.sdrplayProbed);
    CHECK(d.antenna == "Antenna A");
    CHECK(d.antennas.size() == 3);
    CHECK(d.sdrplayHasAgc);
    CHECK(d.sdrplaySettingKeys.size() == 6);
    CHECK(d.soapySettings.at("biasT_ctrl") == "false");
    CHECK(d.soapySettings.count("extref_ctrl") == 0);
    CHECK(d.soapySettings.count("rfgain_sel") == 0);
    auto latest = rsp(); latest.agcEnabled = true; latest.rfgrDb = 10;
    SdrplayControl::mergeCapabilities(d, latest);
    CHECK(latest.agcEnabled); CHECK(latest.rfgrDb == 10); CHECK(latest.antenna == "Antenna A");
}

TEST_CASE("RSPdx all physical controls reach and confirm the driver", "[sdrplay][controls]") {
    auto d = rsp(); SdrplayControlFixture driver; SdrplayControl::probe(driver, d);
    REQUIRE_THROWS(apply(driver, d, {Kind::Setting, "biasT_ctrl", "true"}));
    apply(driver, d, {Kind::Antenna, "antenna", "Antenna B"});
    apply(driver, d, {Kind::Setting, "biasT_ctrl", "true"});
    CHECK(driver.settings.at("biasT_ctrl") == "true");
    driver.calls.clear();
    apply(driver, d, {Kind::Antenna, "antenna", "Antenna C"});
    REQUIRE(driver.calls.size() == 2);
    CHECK(driver.calls[0] == "biasT_ctrl=false");
    CHECK(driver.calls[1] == "antenna=Antenna C");
    REQUIRE_THROWS(apply(driver, d, {Kind::Setting, "biasT_ctrl", "true"}));
    for (const auto* key : {"rfnotch_ctrl", "dabnotch_ctrl", "hdr_ctrl", "iqcorr_ctrl"}) {
        for (const auto* value : {"true", "false"}) {
            apply(driver, d, {Kind::Setting, key, value}); CHECK(driver.settings.at(key) == value);
        }
    }
    apply(driver, d, {Kind::Agc, "AGC", "", 1}); CHECK(driver.agc);
    apply(driver, d, {Kind::Gain, "RFGR", "", 9}); CHECK(driver.rf == 9); CHECK(driver.agc);
    apply(driver, d, {Kind::Setting, "agc_setpoint", "-42"}); CHECK(driver.settings.at("agc_setpoint") == "-42");
    apply(driver, d, {Kind::Gain, "IFGR", "", 45}); CHECK(driver.ifgr == 45); CHECK_FALSE(driver.agc);
    for (const auto bw : driver.listBandwidths(0, 0)) {
        apply(driver, d, {Kind::Bandwidth, "", "", bw}); CHECK(driver.bw == bw);
    }
    apply(driver, d, {Kind::Bandwidth, "", "", 0}); CHECK(driver.bw == 1536000);
    for (const auto& c : std::vector<SdrplayControl::Change>{
        {Kind::Setting, "extref_ctrl", "true"}, {Kind::Setting, "biasT_ctrl", "potato"},
        {Kind::Setting, "agc_setpoint", "20"}, {Kind::Setting, "agc_setpoint", "-4x"},
        {Kind::Gain, "RFGR", "", 28}, {Kind::Gain, "IFGR", "", 0},
        {Kind::Gain, "RFGR", "", 4.5}, {Kind::Bandwidth, "", "", 1234},
        {Kind::Antenna, "", "RX"}}) REQUIRE_THROWS(apply(driver, d, c));
}

TEST_CASE("SDRplay rejected and ignored writes do not become confirmed values", "[sdrplay][controls]") {
    auto d = rsp(); SdrplayControlFixture driver; SdrplayControl::probe(driver, d);
    driver.fail = "rfnotch_ctrl";
    REQUIRE_THROWS(apply(driver, d, {Kind::Setting, "rfnotch_ctrl", "true"}));
    CHECK(d.soapySettings.at("rfnotch_ctrl") == "false");
    driver.fail.clear(); driver.ignoreWrites = true;
    REQUIRE_THROWS(apply(driver, d, {Kind::Setting, "rfnotch_ctrl", "true"}));
    REQUIRE_THROWS(apply(driver, d, {Kind::Antenna, "antenna", "Antenna B"}));
    REQUIRE_THROWS(apply(driver, d, {Kind::Gain, "RFGR", "", 9}));
    CHECK(d.antenna == "Antenna A"); CHECK(d.rfgrDb == 4);
}

TEST_CASE("SDRplay startup profile preserves AGC and RF state", "[sdrplay][controls]") {
    auto d = rsp(); SdrplayControlFixture driver; SdrplayControl::probe(driver, d);
    d.agcEnabled = true; d.rfgrDb = 13;
    d.sdrplaySettingKeys.push_back("rfgain_sel");
    d.soapySettings["rfgain_sel"] = "4"; // Old saved alias must not overwrite RFGR.
    SdrplayControl::apply(driver, d);
    CHECK(driver.agc); CHECK(driver.rf == 13);
}

TEST_CASE("SDRplay light discovery to real open controls restart and persistence", "[.sdrplay-controls-live]") {
    int argc = 1; char name[] = "sdrplay-control-test"; char* argv[] = {name, nullptr};
    QCoreApplication app(argc, argv);
    app.setOrganizationName("SDRTownTests"); app.setApplicationName("RSPdxControls");
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir settings;
    REQUIRE(settings.isValid());
    // Unique test-only registry: no calls open an attached real receiver.
    SoapySDR::Registry registry("sdrplay_controls_fixture",
        [](const SoapySDR::Kwargs&) -> SoapySDR::KwargsList { return {{{"driver", "sdrplay_controls_fixture"}, {"label", "RSPdx fixture"}, {"serial", "test-rspdx"}}}; },
        [](const SoapySDR::Kwargs&) -> SoapySDR::Device* { return new SdrplayControlFixture; }, SOAPY_SDR_ABI_VERSION);
    auto& mgr = DeviceManager::instance();
    auto devices = mgr.enumerateDevices(false);
    auto it = std::find_if(devices.begin(), devices.end(), [](const auto& d) { return d.driver == "sdrplay_controls_fixture"; });
    REQUIRE(it != devices.end()); const size_t index = it - devices.begin();
    REQUIRE_FALSE(it->sdrplayProbed);
    std::string error;
    REQUIRE_FALSE(mgr.setLiveSdrplaySetting(index, "biasT_ctrl", "true", &error));
    const auto wait = [&]() {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < end) {
            auto d = mgr.getDevices();
            if (d[index].sdrplayControlStatus == "Driver readback confirmed" && mgr.getRuntimeStateLabel(index) == "live hardware") return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return false;
    };
    REQUIRE(mgr.startStreaming(index, true));
    const bool started = wait();
    if (!started) mgr.stopStreaming(index);
    REQUIRE(started);
    CHECK(mgr.getDevices()[index].antennas.size() == 3);
    CHECK(mgr.setLiveAntenna(index, "Antenna B", &error));
    CHECK(mgr.setLiveSdrplaySetting(index, "biasT_ctrl", "true", &error));
    CHECK(mgr.setLiveAgc(index, true, &error));
    mgr.setLiveGain(index, 8);
    CHECK(mgr.getDevices()[index].agcEnabled);
    CHECK_FALSE(mgr.setLiveSdrplaySetting(index, "extref_ctrl", "true", &error));
    CHECK(mgr.setLiveBandwidth(index, 600000, &error));
    CHECK(mgr.setLiveBandwidth(index, 0, &error));
    mgr.setFrequencyCorrection(index, 2.5);
    CHECK(mgr.getDevices()[index].frequencyCorrectionPpm == 2.5);
    mgr.applyLiveSampleRate(index, 2e6);
    CHECK(wait());
    CHECK(mgr.getCurrentSampleRate(index) == 2e6);
    CHECK(mgr.getDevices()[index].agcEnabled);
    mgr.stopStreaming(index);
    mgr.loadSettings();
    CHECK(mgr.getDevices()[index].antenna == "Antenna B");
    CHECK(mgr.getDevices()[index].agcEnabled);
    REQUIRE(mgr.startStreaming(index, true));
    const bool restarted = wait();
    CHECK(restarted);
    CHECK(mgr.getDevices()[index].antenna == "Antenna B");
    CHECK(mgr.getDevices()[index].agcEnabled);
    CHECK(mgr.setLiveAntenna(index, "Antenna A", &error));
    CHECK(mgr.getDevices()[index].soapySettings.at("biasT_ctrl") == "false");
    // DEC-0137: extend fubarzi's five-cycle test with proof of decoder input.
    struct InmarsatCleanup {
        DeviceManager& manager;size_t index;
        ~InmarsatCleanup() {
            InmarsatEngine::instance().stop();
            manager.stopStreaming(index);
            SdrplayControlFixture::produceSamples.store(false,std::memory_order_release);
        }
    } cleanup{mgr,index};
    SdrplayControlFixture::produceSamples.store(true,std::memory_order_release);
    auto& inmarsat=InmarsatEngine::instance();
    auto config=InmarsatEngineConfig::defaults();
    config.deviceIndex=index;config.deviceStableKey=mgr.getDevices()[index].stableKey;
    config.playAudio=false;config.recordVoice=false;
    REQUIRE(inmarsat.setConfig(config));
    for(int cycle=0;cycle<5;++cycle) {
        CAPTURE(cycle);
        REQUIRE(inmarsat.start(true));
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(std::chrono::steady_clock::now()<deadline &&
              inmarsat.snapshot().diagnostics.value("samples",uint64_t{0})==0)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const auto snapshot=inmarsat.snapshot();
        CHECK(snapshot.deviceConnected);
        CHECK(snapshot.diagnostics.value("samples",uint64_t{0})>0);
        CHECK(snapshot.validatedFrames==0);CHECK(snapshot.voiceFrames==0);
        CHECK(snapshot.diagnostics.value("pcmSamples",uint64_t{0})==0);
        inmarsat.stop();
        CHECK(inmarsat.snapshot().state==InmarsatEngineState::Idle);
        CHECK(mgr.isStreaming(index));
        CHECK(mgr.getRuntimeStateLabel(index)=="live hardware");
    }
    mgr.stopStreaming(index);
}
#endif
