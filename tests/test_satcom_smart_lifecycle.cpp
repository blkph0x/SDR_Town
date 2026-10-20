#include <catch2/catch_test_macros.hpp>
#ifdef HAVE_SOAPYSDR
#include "SdrplayControlFixture.h"
#include "DeviceManager.h"
#include "SatcomScannerEngine.h"
#include <QApplication>
#include <QStandardPaths>
#include <SoapySDR/Registry.hpp>
#include <cmath>
#include <numbers>

namespace {
// DEC-0226: deterministic two-transmitter fixture through the real Soapy,
// ring, cursor and worker. No connected radio or speaker is used.
class SmartScanRadio : public SdrplayControlFixture {
public:
    static inline std::atomic<int> carriers{3};
    static inline std::atomic<bool> stalled{false};
    uint64_t sample = 0;
    uint32_t noise = 1234567;
    int readStream(SoapySDR::Stream*, void* const* buffers, size_t elements,
                   int&, long long&, long) override {
        if (stalled.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return SOAPY_SDR_TIMEOUT;
        }
        auto* iq = static_cast<std::complex<float>*>(buffers[0]);
        const int mask = carriers.load();
        for (size_t i = 0; i < elements; ++i, ++sample) {
            noise = 1664525u * noise + 1013904223u;
            iq[i] = {.1f + float(noise >> 16) * (1e-4f / 65536),
                      .05f + float(noise & 65535) * (1e-4f / 65536)};
            const double t = sample / rate;
            for (int channel = 0; channel < 2; ++channel) {
                if (!(mask & (1 << channel))) continue;
                const double voice = channel ? 1700 : 1000;
                const double offset = (channel ? 245.075e6 : 245e6) - frequency;
                const double phase = 2 * std::numbers::pi * offset * t -
                    2500 / voice * std::cos(2 * std::numbers::pi * voice * t);
                iq[i] += .02f * std::complex<float>(std::cos(phase), std::sin(phase));
            }
        }
        std::this_thread::sleep_for(std::chrono::duration<double>(elements / rate));
        return static_cast<int>(elements);
    }
};
}

TEST_CASE("Smart receiver follows priority, resumes on quiet and stops on stale IQ",
          "[.ownership-live][smart-worker]") {
    int argc = 1;
    char name[] = "satcom-smart-tests";
    char* argv[] = {name, nullptr};
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName("SDRTownTests");
    app.setApplicationName("SatcomSmartWorker");
    SoapySDR::Registry registry("satcom_smart_fixture",
        [](const SoapySDR::Kwargs&) -> SoapySDR::KwargsList {
            return {{{"driver", "satcom_smart_fixture"}, {"serial", "smart-scan"},
                     {"label", "Smart scan synthetic radio"}}};
        }, [](const SoapySDR::Kwargs&) -> SoapySDR::Device* { return new SmartScanRadio; },
        SOAPY_SDR_ABI_VERSION);
    auto& manager = DeviceManager::instance();
    const auto devices = manager.enumerateDevices(false);
    const auto selected = std::find_if(devices.begin(), devices.end(),
        [](const auto& d) { return d.driver == "satcom_smart_fixture"; });
    REQUIRE(selected != devices.end());
    const size_t index = static_cast<size_t>(selected - devices.begin());
    std::string error;
    REQUIRE(manager.setWorkflowAssignments({}, &error));
    SmartScanRadio::carriers.store(3);
    SmartScanRadio::stalled.store(false);
    SatcomScannerEngine engine("smart-synthetic");
    struct Stop { SatcomScannerEngine& engine; ~Stop() { engine.stop(); } } stop{engine};
    auto config = SatcomScannerConfig::defaults();
    config.deviceIndex = index;
    config.deviceStableKey = selected->stableKey;
    config.lowHz = 245e6;
    config.highHz = 246e6;
    config.stepHz = config.bandwidthHz = 25000;
    config.dwellMs = 80;
    config.quietHoldMs = 200;
    config.monitorAudio = config.autoCapture = false;
    config.smartScan = config.autoSquelch = true;
    config.watchChannels = {{245e6, "A", 2, true}, {245.075e6, "B", 1, true}};
    engine.setConfig(config);
    REQUIRE(engine.start());
    const auto until = [&](auto predicate, int milliseconds) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        do {
            QApplication::processEvents();
            if (predicate()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    };
    REQUIRE(until([&] {
        const auto s = engine.snapshot();
        return s.lockHz == 245e6 && std::count_if(s.watchStatus.begin(), s.watchStatus.end(),
            [](const auto& w) { return w.active; }) >= 2;
    }, 6000));
    CHECK(engine.snapshot().scanInputSamples > 0);
    CHECK(engine.snapshot().scanProcessingUs > 0);
    CHECK(engine.setWatchChannel({245e6, "A", 0, true}, &error));
    CHECK(engine.setWatchChannel({245.075e6, "B", 2, true}, &error));
    REQUIRE(until([&] { return engine.snapshot().lockHz == 245.075e6; }, 3000));
    CHECK(engine.snapshot().foregroundSwitches >= 2);
    SmartScanRadio::carriers.store(0);
    const bool quiet = until([&] { const auto s = engine.snapshot(); return s.lockHz == 0 && s.scanTiles >= 2; }, 5000);
    const auto quietSnapshot = engine.snapshot();
    INFO("quiet state=" << int(quietSnapshot.state) << " lock=" << quietSnapshot.lockHz
         << " tiles=" << quietSnapshot.scanTiles << " status=" << quietSnapshot.lastStatus
         << " gaps=" << quietSnapshot.iqDiscontinuities);
    for (const auto& w : quietSnapshot.watchStatus)
        UNSCOPED_INFO("channel=" << w.channel.frequencyHz << " active=" << w.active
                     << " signal=" << w.signalDb << " noise=" << w.noiseDb);
    REQUIRE(quiet);
    SmartScanRadio::stalled.store(true);
    REQUIRE(until([&] { return engine.snapshot().state == SatcomScannerState::Idle; }, 5000));
    CHECK(engine.snapshot().lastStatus.find("IQ") != std::string::npos);
    CHECK_FALSE(manager.isStreaming(index));
}
#endif
