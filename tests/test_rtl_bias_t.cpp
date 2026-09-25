#include "RtlBiasT.h"
#include "DeviceManager.h"
#include <catch2/catch_test_macros.hpp>
#ifdef HAVE_SOAPYSDR
#include <SoapySDR/Registry.hpp>
#include <SoapySDR/Errors.hpp>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace {
std::atomic<int> ons{0}, offs{0}, unsafeCloses{0}, makes{0};
std::atomic<bool> advertise{true}, failSetup{false}, failRead{false};
class RtlFixture : public SoapySDR::Device {
public:
    bool advertised = advertise.load(), ignored = false, reject = false, badRead = false;
    bool value = false;
    int writes = 0;
    SoapySDR::ArgInfo::Type type = SoapySDR::ArgInfo::BOOL;
    double rate = 2048000, frequency = 100e6;
    SoapySDR::ArgInfoList getSettingInfo() const override {
        SoapySDR::ArgInfo info; info.key = "biastee"; info.type = type;
        return advertised ? SoapySDR::ArgInfoList{info} : SoapySDR::ArgInfoList{};
    }
    std::string readSetting(const std::string& key) const override {
        if (key == "direct_samp") return "0";
        return badRead ? "invalid" : value ? "true" : "false";
    }
    void writeSetting(const std::string& key, const std::string& text) override {
        if (key == "direct_samp") return;
        if (key != "biastee" || (text != "true" && text != "false")) throw std::runtime_error("wrong bias-T API");
        ++writes;
        if (reject) throw std::runtime_error("write rejected");
        if (!ignored) { value = text == "true"; if (value) ++ons; else ++offs; }
    }
    std::string getDriverKey() const override { return "RTLSDR"; }
    std::string getHardwareKey() const override { return "R820T"; }
    size_t getNumChannels(int d) const override { return d == SOAPY_SDR_RX ? 1 : 0; }
    std::vector<std::string> listAntennas(int, size_t) const override { return {"RX"}; }
    void setAntenna(int, size_t, const std::string&) override {}
    std::vector<std::string> listGains(int, size_t) const override { return {"TUNER"}; }
    SoapySDR::Range getGainRange(int, size_t, const std::string&) const override { return {0, 49.6}; }
    void setGain(int, size_t, const std::string&, double) override {}
    void setSampleRate(int, size_t, double r) override { rate = r; }
    double getSampleRate(int, size_t) const override { return rate; }
    std::vector<double> listSampleRates(int, size_t) const override { return {2.048e6, 2.4e6}; }
    void setFrequency(int, size_t, double f, const SoapySDR::Kwargs&) override { frequency = f; }
    double getFrequency(int, size_t) const override { return frequency; }
    SoapySDR::RangeList getFrequencyRange(int, size_t) const override { return {{24e6, 1.7e9}}; }
    SoapySDR::Stream* setupStream(int, const std::string&, const std::vector<size_t>&, const SoapySDR::Kwargs&) override {
        if (failSetup) throw std::runtime_error("fixture stream setup failed");
        return reinterpret_cast<SoapySDR::Stream*>(this);
    }
    int activateStream(SoapySDR::Stream*, int, long long, size_t) override { return 0; }
    int deactivateStream(SoapySDR::Stream*, int, long long) override { return 0; }
    void closeStream(SoapySDR::Stream*) override { if (value) ++unsafeCloses; }
    int readStream(SoapySDR::Stream*, void* const*, size_t, int&, long long&, long) override {
        if (failRead) throw std::runtime_error("fixture RX failed");
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); return SOAPY_SDR_TIMEOUT;
    }
};
SoapySDR::Device* makeRtl(const SoapySDR::Kwargs&) { ++makes; return new RtlFixture; }
}

TEST_CASE("RTL bias-T capability probe never writes or guesses hardware support", "[rtl-bias]") {
    RtlFixture driver; RtlBiasT::State state;
    CHECK_FALSE(state.enabled);
    REQUIRE_THROWS(RtlBiasT::apply(driver, state, true));
    RtlBiasT::probe(driver, state);
    CHECK(state.probed); CHECK(state.supported); CHECK(state.reported == false); CHECK(driver.writes == 0);
    state.enabled = true; RtlBiasT::probe(driver, state); CHECK(state.enabled);
    driver.advertised = false; RtlBiasT::probe(driver, state);
    CHECK_FALSE(state.supported); CHECK_FALSE(state.reported.has_value());
    REQUIRE_THROWS(RtlBiasT::apply(driver, state, true));
    driver.advertised = true; driver.type = SoapySDR::ArgInfo::STRING;
    RtlBiasT::probe(driver, state); CHECK_FALSE(state.supported); CHECK(driver.writes == 0);
}

TEST_CASE("RTL bias-T writes exact API values and rejects stale readback", "[rtl-bias]") {
    RtlFixture driver; RtlBiasT::State state; RtlBiasT::probe(driver, state);
    RtlBiasT::apply(driver, state, false); CHECK(driver.writes == 1);
    RtlBiasT::apply(driver, state, true); CHECK(driver.value); CHECK(state.enabled); CHECK(state.reported == true);
    driver.ignored = true;
    REQUIRE_THROWS(RtlBiasT::apply(driver, state, false)); CHECK(state.enabled);
    driver.ignored = false; driver.reject = true;
    REQUIRE_THROWS(RtlBiasT::apply(driver, state, false)); CHECK(state.enabled);
    driver.reject = false; RtlBiasT::apply(driver, state, false); CHECK_FALSE(driver.value);
    CHECK_FALSE(state.enabled); CHECK(state.reported == false);
    driver.badRead = true; REQUIRE_THROWS(RtlBiasT::probe(driver, state)); CHECK_FALSE(state.probed);
    state.probed = true;
    REQUIRE_THROWS(RtlBiasT::apply(driver, state, true));
    CHECK_FALSE(driver.value); CHECK_FALSE(state.reported.has_value());
}

TEST_CASE("RTL bias-T cleanup attempts OFF even with broken readback", "[rtl-bias]") {
    RtlFixture driver; driver.value = true; driver.badRead = true;
    RtlBiasT::powerOff(driver); CHECK_FALSE(driver.value); CHECK(driver.writes == 1);
    driver.advertised = false; RtlBiasT::powerOff(driver); CHECK(driver.writes == 1);
    driver.advertised = true; driver.reject = true;
    CHECK_NOTHROW(RtlBiasT::powerOff(driver));
}

TEST_CASE("RTL bias-T manager persists intent reapplies at open and powers off at close", "[.rtl-bias-live]") {
    int argc = 1; char name[] = "rtl-bias-test"; char* argv[] = {name, nullptr};
    QCoreApplication app(argc, argv);
    app.setOrganizationName("SDRTownTests"); app.setApplicationName("RtlBiasT");
    QStandardPaths::setTestModeEnabled(true);
    const auto settings = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/devices.json";
    QFile::remove(settings); // This test's isolated settings only.
    // Register before any module loads. Soapy 0.8.1 refuses duplicate names;
    // assertions below ensure make() can only target the fixture, never USB.
    REQUIRE(SoapySDR::Registry::listMakeFunctions().count("rtlsdr") == 0);
    SoapySDR::Registry registry("rtlsdr",
        [](const SoapySDR::Kwargs&) -> SoapySDR::KwargsList {
            return {{{"driver", "rtlsdr"}, {"label", "RTL bias-T fixture"}, {"serial", "test-rtl-bias"}}};
        }, makeRtl, SOAPY_SDR_ABI_VERSION);
    auto& mgr = DeviceManager::instance();
    const auto devices = mgr.enumerateDevices(false);
    REQUIRE(SoapySDR::Registry::listMakeFunctions().at("rtlsdr") == makeRtl);
    auto it = std::find_if(devices.begin(), devices.end(), [](const auto& d) { return d.serial == "test-rtl-bias"; });
    REQUIRE(it != devices.end()); const auto index = static_cast<size_t>(it - devices.begin());
    struct Stop { DeviceManager& mgr; size_t i; ~Stop() { mgr.stopStreaming(i); } } stop{mgr, index};
    std::string error;
    CHECK_FALSE(mgr.setRtlBiasT(index, true, &error));
    CHECK_FALSE(mgr.setRtlBiasT(devices.size(), true, &error));
    auto wait = [&](const std::string& expected = "live hardware") {
        for (int n = 0; n < 500; ++n) {
            if (mgr.getRuntimeStateLabel(index) == expected) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return false;
    };
    REQUIRE(mgr.startStreaming(index, true)); REQUIRE(wait());
    CHECK(mgr.getDevices()[index].rtlBiasT.supported);
    CHECK(mgr.getDevices()[index].rtlBiasT.reported == false); CHECK(offs > 0); CHECK(ons == 0);
    REQUIRE(mgr.setRtlBiasT(index, true, &error)); CHECK(ons > 0);
    CHECK(mgr.getDevices()[index].rtlBiasT.reported == true);
    mgr.stopStreaming(index); CHECK(unsafeCloses == 0);
    CHECK_FALSE(mgr.getDevices()[index].rtlBiasT.reported.has_value());
    REQUIRE(mgr.setRtlBiasT(index, false, &error));
    CHECK_FALSE(mgr.getDevices()[index].rtlBiasT.enabled);
    REQUIRE(mgr.setRtlBiasT(index, true, &error));
    mgr.loadSettings(); CHECK(mgr.getDevices()[index].rtlBiasT.enabled);
    const int before = ons;
    REQUIRE(mgr.startStreaming(index, true)); REQUIRE(wait()); CHECK(ons > before);
    REQUIRE(mgr.setRtlBiasT(index, false, &error)); CHECK(mgr.getDevices()[index].rtlBiasT.reported == false);
    mgr.stopStreaming(index); CHECK(unsafeCloses == 0); CHECK(makes >= 2);
    mgr.loadSettings(); CHECK_FALSE(mgr.getDevices()[index].rtlBiasT.enabled);
    REQUIRE(mgr.setRtlBiasT(index, true, &error));
    failSetup = true;
    const int beforeFailureOff = offs;
    REQUIRE(mgr.startStreaming(index, true)); REQUIRE(wait("hardware failed, using stub"));
    mgr.stopStreaming(index); CHECK(offs > beforeFailureOff); CHECK(unsafeCloses == 0);
    CHECK(mgr.getDevices()[index].rtlBiasT.enabled); // cleanup does not erase intent
    failSetup = false;
    REQUIRE(mgr.startStreaming(index, true)); REQUIRE(wait());
    const int beforeRxOff = offs;
    failRead = true; REQUIRE(wait("hardware failed, using stub"));
    mgr.stopStreaming(index); CHECK(offs > beforeRxOff); CHECK(unsafeCloses == 0);
    failRead = false;
    advertise = false;
    REQUIRE(mgr.startStreaming(index, true)); REQUIRE(wait("hardware failed, using stub"));
    CHECK(mgr.getDevices()[index].rtlBiasT.status.find("cannot be applied") != std::string::npos);
    mgr.stopStreaming(index);
    REQUIRE(mgr.setRtlBiasT(index, false, &error));
    REQUIRE(mgr.startStreaming(index, true)); REQUIRE(wait());
    CHECK_FALSE(mgr.getDevices()[index].rtlBiasT.supported);
    CHECK_FALSE(mgr.setRtlBiasT(index, true, &error));
    mgr.stopStreaming(index); CHECK(unsafeCloses == 0);
    advertise = true;
    QFile config(settings); REQUIRE(config.open(QIODevice::ReadOnly));
    auto saved = nlohmann::json::parse(config.readAll().toStdString()); config.close();
    for (auto& d : saved) if (d["serial"] == "test-rtl-bias") {
        d["stableKey"] = "rtlsdr|serial:different-device";
        d["rtlBiasT"] = true;
    }
    REQUIRE(config.open(QIODevice::WriteOnly | QIODevice::Truncate));
    config.write(QByteArray::fromStdString(saved.dump())); config.close();
    mgr.loadSettings(); CHECK_FALSE(mgr.getDevices()[index].rtlBiasT.enabled);
    QFile::remove(settings);
}
#endif
