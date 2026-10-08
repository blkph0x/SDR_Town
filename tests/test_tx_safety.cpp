#include "DeviceManager.h"
#include <catch2/catch_test_macros.hpp>
#ifdef HAVE_SOAPYSDR
#include <SoapySDR/Registry.hpp>
#include <QCoreApplication>
#include <QStandardPaths>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <thread>
#include <limits>

namespace {
struct TxFixture : SoapySDR::Device {
    static inline int failure = 0;
    static inline std::atomic<int> activations{0}, writes{0}, live{0};
    double rate = 2e6, frequency = 100e6, gain = 0;
    TxFixture() { ++live; } ~TxFixture() override { --live; }
    size_t getNumChannels(int direction) const override { return direction == SOAPY_SDR_TX && failure != 1 ? 1 : 0; }
    SoapySDR::RangeList getSampleRateRange(int, size_t) const override { return {{1e6, 4e6}}; }
    SoapySDR::RangeList getFrequencyRange(int, size_t) const override { return {{10e6, 500e6}}; }
    SoapySDR::Range getGainRange(int, size_t) const override { return {0, 40}; }
    void setSampleRate(int, size_t, double v) override { if (failure == 2) throw std::runtime_error("rate failed"); rate = v; }
    double getSampleRate(int, size_t) const override { return rate; }
    void setFrequency(int, size_t, double v, const SoapySDR::Kwargs&) override { if (failure == 3) throw std::runtime_error("tune failed"); if (failure != 5) frequency = v; }
    double getFrequency(int, size_t) const override { return frequency; }
    void setGain(int, size_t, double v) override { if (failure == 4) throw std::runtime_error("gain failed"); gain = v; }
    double getGain(int, size_t) const override { return failure == 6 ? std::numeric_limits<double>::quiet_NaN() : gain; }
    SoapySDR::Stream* setupStream(int, const std::string&, const std::vector<size_t>&, const SoapySDR::Kwargs&) override {
        if (failure == 7) throw 7; // Drivers must not leak handles for non-standard exceptions.
        return reinterpret_cast<SoapySDR::Stream*>(this);
    }
    int activateStream(SoapySDR::Stream*, int, long long, size_t) override { ++activations; return 0; }
    int deactivateStream(SoapySDR::Stream*, int, long long) override { return 0; }
    void closeStream(SoapySDR::Stream*) override {}
    static inline std::atomic<bool> blockWrite{false};
    static inline std::atomic<bool> inWrite{false};
    int writeStream(SoapySDR::Stream*, const void* const*, size_t count, int&, long long, long) override {
        ++writes;
        if (blockWrite.load(std::memory_order_acquire)) {
            inWrite.store(true, std::memory_order_release);
            while (blockWrite.load(std::memory_order_acquire))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            inWrite.store(false, std::memory_order_release);
        }
        return static_cast<int>(count);
    }
};
}

TEST_CASE("Hardware tone TX cannot activate without confirmed authorized settings", "[devicemanager][tx-safety]") {
    int argc = 1; char name[] = "tone-safety"; char* argv[] = {name, nullptr};
    QCoreApplication app(argc, argv); QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName("SDRTownTests"); app.setApplicationName("ToneSafety");
    SoapySDR::Registry registry("tone_safety_fixture",
        [](const SoapySDR::Kwargs&) -> SoapySDR::KwargsList {
            return {{{"driver", "tone_safety_fixture"}, {"serial", "only-a-test"}}};
        }, [](const SoapySDR::Kwargs&) -> SoapySDR::Device* { return new TxFixture; }, SOAPY_SDR_ABI_VERSION);
    auto& manager = DeviceManager::instance();
    const auto devices = manager.enumerateDevices(false);
    const auto it = std::find_if(devices.begin(), devices.end(), [](const auto& d) { return d.driver == "tone_safety_fixture"; });
    REQUIRE(it != devices.end());
    const size_t index = it - devices.begin();
    struct Cleanup { DeviceManager& m; size_t i; ~Cleanup() { m.stopTx(i); } } cleanup{manager, index};
    DeviceManager::TxParams p; p.centerHz = 450e6; p.attemptHardware = true;
    TxFixture::activations = 0; TxFixture::writes = 0;
    REQUIRE_FALSE(manager.startToneTx(index, p));
    CHECK(TxFixture::live == 0); CHECK(TxFixture::activations == 0);
    p.hardwareAuthorized = true;
    for (int failure = 1; failure <= 7; ++failure) {
        CAPTURE(failure); TxFixture::failure = failure;
        CHECK_FALSE(manager.startToneTx(index, p));
        CHECK(TxFixture::activations == 0); CHECK(TxFixture::writes == 0); CHECK(TxFixture::live == 0);
    }
    TxFixture::failure = 0;
    auto invalid = p; invalid.centerHz = 2e9;
    CHECK_FALSE(manager.startToneTx(index, invalid));
    invalid = p; invalid.gainDb = 50;
    CHECK_FALSE(manager.startToneTx(index, invalid));
    CHECK(TxFixture::activations == 0); CHECK(TxFixture::writes == 0);
    REQUIRE(manager.startToneTx(index, p));
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    manager.stopTx(index);
    CHECK(TxFixture::activations == 1); CHECK(TxFixture::writes > 0); CHECK(TxFixture::live == 0);

    TxFixture::blockWrite = true; TxFixture::inWrite = false;
    REQUIRE(manager.startToneTx(index, p));
    const auto entered = std::chrono::steady_clock::now();
    while (!TxFixture::inWrite.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() - entered < std::chrono::seconds(2))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(TxFixture::inWrite.load(std::memory_order_acquire));
    const auto stopStart = std::chrono::steady_clock::now();
    manager.stopTx(index);
    const auto stopMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - stopStart).count();
    CHECK(stopMs < 2000);
    CHECK(TxFixture::live == 1);
    CHECK(manager.getTxRuntimeState(index) == "io-leaked");
    TxFixture::blockWrite = false;
    const auto released = std::chrono::steady_clock::now();
    while (TxFixture::inWrite.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() - released < std::chrono::seconds(2))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    manager.stopTx(index);
    CHECK(TxFixture::live == 0);
    CHECK(manager.getTxRuntimeState(index) == "idle");
}
#endif
