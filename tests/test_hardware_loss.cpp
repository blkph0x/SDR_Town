#include "DeviceManager.h"
#include "Receiver.h"
#include <catch2/catch_test_macros.hpp>
#ifdef HAVE_SOAPYSDR
#include "SdrplayControlFixture.h"
#include <SoapySDR/Registry.hpp>
#include <QCoreApplication>
#include <QStandardPaths>

namespace {
struct LossFixture : SdrplayControlFixture {
    static inline std::atomic<int> command{0}, completed{0};
    int readStream(SoapySDR::Stream*, void* const* buffers, size_t elements, int&, long long&, long) override {
        const int op = command.exchange(0);
        if (!op) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); return SOAPY_SDR_TIMEOUT; }
        completed.store(op);
        if (op < 0) return op;
        const size_t n = std::min<size_t>(1024, elements);
        std::fill_n(static_cast<std::complex<float>*>(buffers[0]), n, std::complex<float>(float(op), 0));
        return static_cast<int>(n);
    }
};
bool awaitLossFixture(const std::function<bool()>& check) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!check() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return check();
}
}

TEST_CASE("Hardware overflow creates an epoch boundary without mixing pre-loss IQ", "[devicemanager][hardware-loss]") {
    int argc = 1; char name[] = "loss-test"; char* argv[] = {name, nullptr};
    QCoreApplication app(argc, argv); QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName("SDRTownTests"); app.setApplicationName("HardwareLoss");
    SoapySDR::Registry registry("loss_fixture",
        [](const SoapySDR::Kwargs&) -> SoapySDR::KwargsList { return {{{"driver", "loss_fixture"}, {"serial", "test-only"}}}; },
        [](const SoapySDR::Kwargs&) -> SoapySDR::Device* { return new LossFixture; }, SOAPY_SDR_ABI_VERSION);
    auto& manager = DeviceManager::instance(); const auto devices = manager.enumerateDevices(false);
    const auto it = std::find_if(devices.begin(), devices.end(), [](const auto& d) { return d.driver == "loss_fixture"; });
    REQUIRE(it != devices.end()); const size_t index = it - devices.begin();
    struct Cleanup { DeviceManager& m; size_t i; ~Cleanup() { m.stopStreaming(i); } } cleanup{manager, index};
    LossFixture::command = 0; LossFixture::completed = 0;
    REQUIRE(manager.startStreaming(index, true));
    REQUIRE(awaitLossFixture([&] { return manager.getRuntimeStateLabel(index) == "live hardware"; }));
    CHECK(manager.getRecentIQWindowWithCursor(index, 1, true).appliedCenterHz > 0);
    // Persisted fixture settings must not turn the final retune into a no-op.
    const auto initialTune = manager.setCenterFreq(index, 100000000);
    REQUIRE(initialTune != 0);
    REQUIRE(manager.waitForCenterTuneApplied(index, initialTune, 5000));
    LossFixture::command = 1;
    REQUIRE(awaitLossFixture([&] { return !manager.getRecentIQWindow(index, 1024).empty(); }));
    const auto before = manager.getRecentIQWindowWithCursor(index, 4096);
    REQUIRE(before.samples.size() == 1024);
    Receiver receiver; manager.setReceiverCursorToLiveEdge(index, receiver);
    Receiver secondReceiver; manager.setReceiverCursorToLiveEdge(index, secondReceiver);
    LossFixture::command = SOAPY_SDR_OVERFLOW;
    REQUIRE(awaitLossFixture([&] { return LossFixture::completed == SOAPY_SDR_OVERFLOW; }));
    CHECK(awaitLossFixture([&] { return manager.getRecentIQWindowWithCursor(index, 4096).streamEpoch > before.streamEpoch; }));
    const auto gap = manager.getRecentIQWindowWithCursor(index, 4096);
    CHECK(gap.samples.empty());
    const auto emptyPoll = manager.getNewIQWindowForReceiver(index, receiver, 4096);
    REQUIRE(emptyPoll.samples.empty());
    CHECK(emptyPoll.streamEpoch > before.streamEpoch);
    // HF compares the receiver's acknowledged epoch before each read and
    // processes resets only with samples. An empty poll cannot consume it.
    CHECK(receiver.lastSeenStreamEpoch.load() == before.streamEpoch);
    LossFixture::command = 2;
    REQUIRE(awaitLossFixture([&] { return manager.getRecentIQWindowWithCursor(index, 4096).endAbsolute > before.endAbsolute; }));
    const auto after = manager.getRecentIQWindowWithCursor(index, 4096);
    CHECK(after.streamEpoch > before.streamEpoch);
    CHECK(after.startAbsolute == before.endAbsolute);
    REQUIRE(after.samples.size() == 1024);
    CHECK(std::all_of(after.samples.begin(), after.samples.end(), [](auto iq) { return iq == std::complex<float>(2, 0); }));
    const auto chronological = manager.getNewIQWindowForReceiver(index, receiver, 4096);
    CHECK(chronological.streamEpoch == after.streamEpoch);
    CHECK(chronological.samples == after.samples);
    CHECK(receiver.lastSeenStreamEpoch.load() == after.streamEpoch);
    const auto second = manager.getNewIQWindowForReceiver(index, secondReceiver, 4096);
    CHECK(second.streamEpoch == after.streamEpoch);
    CHECK(second.samples == after.samples);
    const auto health = manager.getRxHealth(index);
    CHECK(health.overflows >= 1);
    CHECK(health.lastLossAbsolute == before.endAbsolute);
    CHECK(health.reads >= 3);
    const auto stableEpoch = after.streamEpoch;
    std::this_thread::sleep_for(std::chrono::milliseconds(20)); // ordinary timeouts are not proven loss
    CHECK(manager.getRecentIQWindowWithCursor(index, 4096).streamEpoch == stableEpoch);
    LossFixture::command = SOAPY_SDR_OVERFLOW;
    REQUIRE(awaitLossFixture([&] { return manager.getRxHealth(index).overflows > health.overflows; }));
    const auto repeatedLoss = manager.getRecentIQWindowWithCursor(index, 4096);
    CHECK(repeatedLoss.streamEpoch > stableEpoch);
    CHECK(repeatedLoss.samples.empty());
    CHECK(repeatedLoss.endAbsolute == after.endAbsolute);
    LossFixture::command = 3;
    REQUIRE(awaitLossFixture([&] { return manager.getRecentIQWindowWithCursor(index, 4096).endAbsolute > after.endAbsolute; }));
    const auto final = manager.getNewIQWindowForReceiver(index, receiver, 4096);
    CHECK(final.streamEpoch == repeatedLoss.streamEpoch);
    CHECK(final.startAbsolute == after.endAbsolute);
    REQUIRE(final.samples.size() == 1024);
    CHECK(std::all_of(final.samples.begin(), final.samples.end(), [](auto iq) { return iq == std::complex<float>(3, 0); }));
    const auto tune = manager.setCenterFreq(index, 420350000);
    REQUIRE(tune != 0);
    REQUIRE(manager.waitForCenterTuneApplied(index, tune, 5000));
    // DEC-0194: demonstrate that an epoch alone does not qualify the old tail.
    const auto afterRetune = manager.getRecentIQWindowWithCursor(index, 4096, true);
    CHECK(afterRetune.samples.empty());
    CHECK(afterRetune.appliedCenterHz == 420350000);
    CHECK(afterRetune.retuneStartAbsolute == final.endAbsolute);
    CHECK(afterRetune.streamEpoch > final.streamEpoch);
    // The recording view is intentionally unchanged and still includes old RF.
    CHECK(manager.getRecentIQWindowWithCursor(index, 4096).samples == final.samples);
    LossFixture::command = 4;
    REQUIRE(awaitLossFixture([&] { return manager.getRecentIQWindowWithCursor(index, 4096).endAbsolute > final.endAbsolute; }));
    const auto newRf = manager.getRecentIQWindowWithCursor(index, 4096, true);
    CHECK(newRf.startAbsolute == afterRetune.retuneStartAbsolute);
    REQUIRE(newRf.samples.size() == 1024);
    CHECK(std::all_of(newRf.samples.begin(), newRf.samples.end(), [](auto iq) { return iq == std::complex<float>(4, 0); }));
}
#endif
