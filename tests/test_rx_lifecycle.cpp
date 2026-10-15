#include "DeviceManager.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#ifdef HAVE_SOAPYSDR
#include "SdrplayControlFixture.h"
#include <SoapySDR/Registry.hpp>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QVariant>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <array>
#include <condition_variable>
#include <functional>
#include <mutex>

namespace {
struct LifecycleFixture : SdrplayControlFixture {
    static inline std::atomic<int> activation{0}, closed{0}, created{0}, destroyed{0};
    static inline std::mutex mutex;
    static inline std::condition_variable condition;
    static inline bool holdRead = false, holdTune = false, holdActivation = false, entered = false;
    static inline int lateResult = 1024;
    static inline std::vector<std::string> serials{"no-hardware"};
    std::string serial;
    explicit LifecycleFixture(std::string identity) : serial(std::move(identity)) { ++created; }
    ~LifecycleFixture() override { ++destroyed; }
    std::string getHardwareKey() const override { return "RX lifecycle fixture"; }
    int activateStream(SoapySDR::Stream*, int, long long, size_t) override {
        std::unique_lock lock(mutex);
        if (holdActivation && serial == "no-hardware") {
            entered = true; condition.notify_all();
            condition.wait(lock, [] { return !holdActivation; });
        }
        return activation;
    }
    void closeStream(SoapySDR::Stream*) override { ++closed; }
    void setFrequency(int, size_t, double value, const SoapySDR::Kwargs&) override {
        std::unique_lock lock(mutex);
        if (holdTune) {
            entered = true; condition.notify_all();
            condition.wait(lock, [] { return !holdTune; });
        }
        frequency = value;
    }
    int readStream(SoapySDR::Stream*, void* const* buffers, size_t elements,
                   int&, long long&, long) override {
        std::unique_lock lock(mutex);
        if (holdRead && serial == "no-hardware") {
            entered = true; condition.notify_all();
            condition.wait(lock, [] { return !holdRead; });
            if (lateResult == -99) throw std::runtime_error("cancelled reader exception");
            if (lateResult <= 0) return lateResult;
            const auto count = std::min<size_t>(elements, lateResult);
            std::fill_n(static_cast<std::complex<float>*>(buffers[0]), count,
                        std::complex<float>(0.375f, -0.125f));
            return static_cast<int>(count);
        }
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return SOAPY_SDR_TIMEOUT;
    }
    static void release() {
        { std::lock_guard lock(mutex); holdRead = holdTune = holdActivation = false; }
        condition.notify_all();
    }
};
bool awaitLifecycle(const std::function<bool()>& check) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!check() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return check();
}
struct LifecycleEnvironment {
    int argc = 1;
    char name[20] = "rx-lifecycle";
    char* argv[2] = {name, nullptr};
    QCoreApplication app{argc, argv};
    SoapySDR::Registry registry{"rx_lifecycle_fixture",
        [](const SoapySDR::Kwargs& args) -> SoapySDR::KwargsList {
            std::lock_guard lock(LifecycleFixture::mutex);
            SoapySDR::KwargsList result;
            for (const auto& serial : LifecycleFixture::serials) {
                if (!args.count("serial") || args.at("serial") == serial)
                    result.push_back({{"driver", "rx_lifecycle_fixture"}, {"serial", serial}});
            }
            return result;
        }, [](const SoapySDR::Kwargs& args) -> SoapySDR::Device* {
            return new LifecycleFixture(args.at("serial"));
        }, SOAPY_SDR_ABI_VERSION};
    DeviceManager& manager = DeviceManager::instance();
    size_t index;
    LifecycleEnvironment() {
        QStandardPaths::setTestModeEnabled(true);
        app.setOrganizationName("SDRTownTests"); app.setApplicationName("RxLifecycle");
        LifecycleFixture::release();
        LifecycleFixture::activation = LifecycleFixture::closed = LifecycleFixture::created = LifecycleFixture::destroyed = 0;
        LifecycleFixture::entered = false;
        LifecycleFixture::serials = {"no-hardware"};
        const auto devices = manager.enumerateDevices(false);
        const auto it = std::find_if(devices.begin(), devices.end(), [](const auto& d) {
            return d.driver == "rx_lifecycle_fixture";
        });
        REQUIRE(it != devices.end());
        REQUIRE_FALSE(it->isSdrplay);
        index = size_t(it - devices.begin());
    }
    ~LifecycleEnvironment() {
        LifecycleFixture::release();
        const auto devices = manager.getDevices();
        for (size_t i = 0; i < devices.size(); ++i)
            if (devices[i].driver == "rx_lifecycle_fixture") manager.stopStreaming(i);
        // Keep the fixture registry alive until detached native work has returned.
        awaitLifecycle([] { return LifecycleFixture::destroyed == LifecycleFixture::created; });
    }
    void live() {
        REQUIRE(awaitLifecycle([&] { return manager.startStreaming(index, true); }));
        REQUIRE(awaitLifecycle([&] { return manager.getRuntimeStateLabel(index) == "live hardware"; }));
    }
    void block(bool tune) {
        std::lock_guard lock(LifecycleFixture::mutex);
        LifecycleFixture::entered = false;
        LifecycleFixture::holdRead = !tune;
        LifecycleFixture::holdTune = tune;
    }
    void entered() {
        std::unique_lock lock(LifecycleFixture::mutex);
        REQUIRE(LifecycleFixture::condition.wait_for(lock, std::chrono::seconds(5), [] { return LifecycleFixture::entered; }));
    }
};

struct SharedLifecycleFixture : SdrplayControlFixture {
    struct Channel { size_t index; } channels[2]{{0}, {1}};
    static inline std::atomic<int> created{0}, destroyed{0};
    static inline std::array<std::atomic<int>, 2> closed{};
    static inline std::mutex mutex;
    static inline std::condition_variable condition;
    static inline std::array<bool, 2> holdRead{}, entered{};
    static inline std::array<bool, 2> holdClose{}, closeEntered{}, holdActivation{}, activationEntered{}, throwRead{};
    static inline std::array<int, 2> activationResult{};
    static inline bool failClose = false;
    static inline std::string serial;
    SharedLifecycleFixture() { ++created; }
    ~SharedLifecycleFixture() override { ++destroyed; }
    std::string getHardwareKey() const override { return "RSPduo"; }
    size_t getNumChannels(int direction) const override { return direction == SOAPY_SDR_RX ? 2 : 0; }
    SoapySDR::Stream* setupStream(int, const std::string&, const std::vector<size_t>& selected,
                                 const SoapySDR::Kwargs&) override {
        return reinterpret_cast<SoapySDR::Stream*>(&channels[selected.at(0)]);
    }
    int activateStream(SoapySDR::Stream* stream, int, long long, size_t) override {
        const auto channel = reinterpret_cast<Channel*>(stream)->index;
        std::unique_lock lock(mutex);
        activationEntered[channel] = true;
        condition.notify_all();
        condition.wait(lock, [&] { return !holdActivation[channel]; });
        return activationResult[channel];
    }
    void closeStream(SoapySDR::Stream* stream) override {
        const auto channel = reinterpret_cast<Channel*>(stream)->index;
        std::unique_lock lock(mutex);
        closeEntered[channel] = true;
        condition.notify_all();
        condition.wait(lock, [&] { return !holdClose[channel]; });
        if (channel == 0 && failClose) throw std::runtime_error("shared stream close rejected");
        ++closed[channel];
    }
    int readStream(SoapySDR::Stream* stream, void* const*, size_t, int&, long long&, long) override {
        std::unique_lock lock(mutex);
        const auto channel = reinterpret_cast<Channel*>(stream)->index;
        if (throwRead[channel]) throw std::runtime_error("live shared reader fault");
        if (holdRead[channel]) {
            entered[channel] = true;
            condition.notify_all();
            condition.wait(lock, [&] { return !holdRead[channel]; });
        }
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return SOAPY_SDR_TIMEOUT;
    }
    static void release(size_t channel = 2) {
        {
            std::lock_guard lock(mutex);
            if (channel < 2) holdRead[channel] = false;
            else {
                holdRead.fill(false);
                holdClose.fill(false);
                holdActivation.fill(false);
                throwRead.fill(false);
            }
        }
        condition.notify_all();
    }
};

// The existing failure log is emitted after retirement catches the close error.
// Observe that boundary so assertions do not race unfinished native cleanup.
struct RetirementFailureSink : spdlog::sinks::base_sink<std::mutex> {
    std::atomic<bool> observed{false};
    void sink_it_(const spdlog::details::log_msg& message) override {
        const std::string text(message.payload.data(), message.payload.size());
        if (text.find("RX cleanup failed for usb:sdrplay_rx_lifecycle_fixture|") != std::string::npos)
            observed.store(true, std::memory_order_release);
    }
    void flush_() override {}
};

struct SharedLifecycleEnvironment : LifecycleEnvironment {
    SoapySDR::Registry sharedRegistry{"sdrplay_rx_lifecycle_fixture",
        [](const SoapySDR::Kwargs&) -> SoapySDR::KwargsList {
            return {{{"driver", "sdrplay_rx_lifecycle_fixture"}, {"hardware", "RSPduo"},
                     {"serial", SharedLifecycleFixture::serial}, {"mode", "DT"}}};
        }, [](const SoapySDR::Kwargs&) -> SoapySDR::Device* { return new SharedLifecycleFixture; }, SOAPY_SDR_ABI_VERSION};
    std::shared_ptr<spdlog::logger> previousLogger = spdlog::default_logger();
    std::shared_ptr<RetirementFailureSink> failure = std::make_shared<RetirementFailureSink>();
    std::vector<size_t> indices;
    SharedLifecycleEnvironment(bool failClose, std::string serial) {
        SharedLifecycleFixture::created = SharedLifecycleFixture::destroyed = 0;
        for (auto& count : SharedLifecycleFixture::closed) count = 0;
        SharedLifecycleFixture::failClose = failClose;
        SharedLifecycleFixture::serial = std::move(serial);
        SharedLifecycleFixture::entered.fill(false);
        SharedLifecycleFixture::closeEntered.fill(false);
        SharedLifecycleFixture::activationEntered.fill(false);
        SharedLifecycleFixture::activationResult.fill(0);
        SharedLifecycleFixture::release();
        const auto devices = manager.enumerateDevices(false);
        for (size_t i = 0; i < devices.size(); ++i) {
            if (devices[i].driver == "sdrplay_rx_lifecycle_fixture") indices.push_back(i);
            if (devices[i].driver == "rx_lifecycle_fixture") index = i;
        }
        REQUIRE(indices.size() == 2);
        REQUIRE(devices[indices[0]].rxChannel == 0);
        REQUIRE(devices[indices[1]].rxChannel == 1);
        auto logger = previousLogger->clone("rx-lifecycle-retirement");
        logger->sinks().push_back(failure);
        logger->set_level(spdlog::level::err);
        spdlog::set_default_logger(logger);
    }
    ~SharedLifecycleEnvironment() {
        SharedLifecycleFixture::release();
        for (auto i : indices) manager.stopStreaming(i);
        manager.stopStreaming(index);
        spdlog::set_default_logger(previousLogger);
    }
    void startBoth() {
        for (auto i : indices) {
            REQUIRE(manager.startStreaming(i, true));
            REQUIRE(awaitLifecycle([&] { return manager.getRuntimeStateLabel(i) == "live hardware"; }));
        }
        REQUIRE(SharedLifecycleFixture::created == 1);
    }
    void blockReads(bool both) {
        std::unique_lock lock(SharedLifecycleFixture::mutex);
        SharedLifecycleFixture::holdRead = {true, both};
        REQUIRE(SharedLifecycleFixture::condition.wait_for(lock, std::chrono::seconds(5), [&] {
            return SharedLifecycleFixture::entered[0] && (!both || SharedLifecycleFixture::entered[1]);
        }));
    }
};
}

TEST_CASE("Every negative RX activation result fails closed for non-SDRplay", "[devicemanager][rx-lifecycle]") {
    const int result = GENERATE(SOAPY_SDR_STREAM_ERROR, SOAPY_SDR_NOT_SUPPORTED, SOAPY_SDR_TIMEOUT);
    LifecycleEnvironment env;
    LifecycleFixture::activation = result;
    const auto reads = env.manager.getRxHealth(env.index).reads;
    REQUIRE(env.manager.startStreaming(env.index, true));
    REQUIRE(awaitLifecycle([&] { return env.manager.getRuntimeStateLabel(env.index).find("opening") == std::string::npos; }));
    CHECK(env.manager.getRuntimeStateLabel(env.index) == "hardware failed, using stub");
    CHECK(env.manager.getRxHealth(env.index).reads == reads);
    CHECK(LifecycleFixture::closed == 1);
    CHECK(LifecycleFixture::destroyed == 1);
}

TEST_CASE("Cancelled native reads never mutate a replacement RX session", "[devicemanager][rx-lifecycle]") {
    const int result = GENERATE(1024, SOAPY_SDR_OVERFLOW, SOAPY_SDR_TIMEOUT, SOAPY_SDR_STREAM_ERROR, -99);
    LifecycleEnvironment env; env.live();
    LifecycleFixture::lateResult = result;
    env.block(false); env.entered();
    const auto start = std::chrono::steady_clock::now();
    env.manager.stopStreaming(env.index);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
    CHECK_FALSE(env.manager.startStreaming(env.index, true));
    CHECK(LifecycleFixture::closed == 0);
    REQUIRE(env.manager.startStreaming(env.index, false));
    const auto before = env.manager.getRecentIQWindowWithCursor(env.index, 1);
    const auto health = env.manager.getRxHealth(env.index);
    LifecycleFixture::release();
    REQUIRE(awaitLifecycle([] { return LifecycleFixture::destroyed == 1; }));
    const auto after = env.manager.getRecentIQWindowWithCursor(env.index, 1048576);
    CHECK(std::count(after.samples.begin(), after.samples.end(), std::complex<float>(0.375f, -0.125f)) == 0);
    CHECK(after.streamEpoch == before.streamEpoch);
    CHECK(env.manager.getRxHealth(env.index).reads == health.reads);
    CHECK(env.manager.getRxHealth(env.index).overflows == health.overflows);
    CHECK(env.manager.getRuntimeStateLabel(env.index) == "simulated/stub");
    CHECK(LifecycleFixture::closed == 1);
    env.manager.stopStreaming(env.index);
    CHECK(env.manager.getRecentIQWindow(env.index, 1024).empty());
    env.live();
}

TEST_CASE("Cancelled queued tune cannot acknowledge a replacement session", "[devicemanager][rx-lifecycle]") {
    LifecycleEnvironment env; env.live();
    env.block(true);
    const auto tune = env.manager.setCenterFreq(env.index, 145800000);
    REQUIRE(tune != 0); env.entered();
    env.manager.stopStreaming(env.index);
    REQUIRE(env.manager.startStreaming(env.index, false));
    const auto applied = env.manager.getCenterTuneAppliedSeq(env.index);
    const auto epoch = env.manager.getRecentIQWindowWithCursor(env.index, 1).streamEpoch;
    LifecycleFixture::release();
    REQUIRE(awaitLifecycle([] { return LifecycleFixture::destroyed == 1; }));
    CHECK(env.manager.getCenterTuneAppliedSeq(env.index) == applied);
    CHECK(env.manager.getRecentIQWindowWithCursor(env.index, 1).streamEpoch == epoch);
}

TEST_CASE("Cancelled hardware activation cannot replace a new simulated session", "[devicemanager][rx-lifecycle]") {
    LifecycleEnvironment env;
    { std::lock_guard lock(LifecycleFixture::mutex); LifecycleFixture::holdActivation = true; }
    REQUIRE(env.manager.startStreaming(env.index, true)); env.entered();
    env.manager.stopStreaming(env.index);
    CHECK_FALSE(env.manager.startStreaming(env.index, true));
    REQUIRE(env.manager.startStreaming(env.index, false));
    const auto applied = env.manager.getCenterTuneAppliedSeq(env.index);
    const auto epoch = env.manager.getRecentIQWindowWithCursor(env.index, 1).streamEpoch;
    LifecycleFixture::release();
    REQUIRE(awaitLifecycle([] { return LifecycleFixture::destroyed == 1; }));
    CHECK(env.manager.getRuntimeStateLabel(env.index) == "simulated/stub");
    CHECK(env.manager.getCenterTuneAppliedSeq(env.index) == applied);
    CHECK(env.manager.getRecentIQWindowWithCursor(env.index, 1).streamEpoch == epoch);
    CHECK(LifecycleFixture::closed == 1);
}

TEST_CASE("Actual GUI dry-run rejects hardware starts at the common boundary", "[devicemanager][rx-lifecycle]") {
    LifecycleEnvironment env;
    env.app.setProperty("sdrtown.guiDryRun", true);
    CHECK_FALSE(env.manager.startStreaming(env.index, true));
    CHECK_FALSE(env.manager.isStreaming(env.index));
    CHECK(LifecycleFixture::destroyed == 0);
    env.app.setProperty("sdrtown.guiDryRun", false);
    env.live();
}

TEST_CASE("Cancelled native open follows device identity across a rescan", "[devicemanager][rx-lifecycle][rx-identity]") {
    const bool read = GENERATE(false, true);
    LifecycleEnvironment env;
    if (read) {
        env.live();
        env.block(false);
    } else {
        { std::lock_guard lock(LifecycleFixture::mutex); LifecycleFixture::holdActivation = true; }
        REQUIRE(env.manager.startStreaming(env.index, true));
    }
    env.entered();
    env.manager.stopStreaming(env.index);
    {
        std::lock_guard lock(LifecycleFixture::mutex);
        LifecycleFixture::serials = {"replacement", "no-hardware"};
    }
    const auto devices = env.manager.enumerateDevices(false);
    const auto find = [&](const std::string& serial) {
        const auto it = std::find_if(devices.begin(), devices.end(), [&](const auto& device) {
            return device.driver == "rx_lifecycle_fixture" && device.serial == serial;
        });
        REQUIRE(it != devices.end());
        return size_t(it - devices.begin());
    };
    const auto replacement = find("replacement");
    const auto moved = find("no-hardware");
    REQUIRE(replacement == env.index);
    REQUIRE(moved != env.index);
    CHECK_FALSE(env.manager.startStreaming(moved, true));
    REQUIRE(env.manager.startStreaming(replacement, true));
    REQUIRE(awaitLifecycle([&] { return env.manager.getRuntimeStateLabel(replacement) == "live hardware"; }));
    CHECK(LifecycleFixture::created == 2);
    LifecycleFixture::release();
    REQUIRE(awaitLifecycle([] { return LifecycleFixture::destroyed == 1; }));
    CHECK(env.manager.getRuntimeStateLabel(replacement) == "live hardware");
    REQUIRE(awaitLifecycle([&] { return env.manager.startStreaming(moved, true); }));
    REQUIRE(awaitLifecycle([&] { return env.manager.getRuntimeStateLabel(moved) == "live hardware"; }));
    CHECK(LifecycleFixture::created == 3);
}

TEST_CASE("Shared RX retirement releases ownership only after confirmed close", "[devicemanager][rx-lifecycle][rx-shared-close]") {
    const bool failClose = GENERATE(false, true);
    SharedLifecycleEnvironment env(failClose, failClose ? "close-failure" : "close-success");
    const auto first = env.indices[0], second = env.indices[1];
    env.startBoth();
    env.blockReads(false);
    env.manager.stopStreaming(first);
    CHECK_FALSE(env.manager.startStreaming(first, true));
    SharedLifecycleFixture::release();
    if (failClose) {
        REQUIRE(awaitLifecycle([&] { return env.failure->observed.load(std::memory_order_acquire); }));
        CHECK_FALSE(env.manager.startStreaming(first, true));
        CHECK(SharedLifecycleFixture::closed[0] == 0);
    } else {
        REQUIRE(awaitLifecycle([] { return SharedLifecycleFixture::closed[0] == 1; }));
        REQUIRE(awaitLifecycle([&] { return env.manager.startStreaming(first, true); }));
        REQUIRE(awaitLifecycle([&] { return env.manager.getRuntimeStateLabel(first) == "live hardware"; }));
        env.manager.stopStreaming(first);
        CHECK(SharedLifecycleFixture::closed[0] == 2);
    }
    CHECK(env.manager.getRuntimeStateLabel(second) == "live hardware");
    CHECK(SharedLifecycleFixture::destroyed == 0);
    env.manager.stopStreaming(second);
    CHECK(SharedLifecycleFixture::closed[1] == 1);
    CHECK(SharedLifecycleFixture::destroyed == (failClose ? 0 : 1));
    if (failClose) {
        CHECK_FALSE(env.manager.startStreaming(first, true));
        REQUIRE(env.manager.startStreaming(first, false));
        env.manager.stopStreaming(first);
    }
    // A quarantined shared receiver must not prevent an unrelated device opening.
    env.live();
}

TEST_CASE("Shared RX quarantine retains every retirement after a sibling close succeeds", "[devicemanager][rx-lifecycle][rx-shared-close][rx-mixed-close]") {
    const bool reverseRetirement = GENERATE(false, true);
    SharedLifecycleEnvironment env(true, reverseRetirement ? "mixed-close-reverse" : "mixed-close-forward");
    env.startBoth();
    env.blockReads(true);
    env.manager.stopStreaming(env.indices[reverseRetirement ? 1 : 0]);
    env.manager.stopStreaming(env.indices[reverseRetirement ? 0 : 1]);
    for (auto index : env.indices) CHECK_FALSE(env.manager.startStreaming(index, true));

    SharedLifecycleFixture::release(0);
    REQUIRE(awaitLifecycle([&] { return env.failure->observed.load(std::memory_order_acquire); }));
    SharedLifecycleFixture::release(1);
    REQUIRE(awaitLifecycle([] { return SharedLifecycleFixture::closed[1] == 1; }));
    // Poll past asynchronous retirement completion: neither a successful sibling
    // nor its completed record may admit a new open over the failed close.
    CHECK_FALSE(awaitLifecycle([&] { return env.manager.startStreaming(env.indices[1], true); }));
    CHECK_FALSE(env.manager.startStreaming(env.indices[0], true));
    CHECK(SharedLifecycleFixture::closed[0] == 0);
    CHECK(SharedLifecycleFixture::destroyed == 0);
    env.live();
}

TEST_CASE("Every RX cleanup path retains shared ownership on close failure", "[devicemanager][rx-lifecycle][rx-shared-close][rx-cleanup-paths]") {
    enum class Path { Stop, ReadFault, CancelledOpen, FailedOpen };
    const auto path = GENERATE(Path::Stop, Path::ReadFault, Path::CancelledOpen, Path::FailedOpen);
    const bool failClose = GENERATE(false, true);
    CAPTURE(static_cast<int>(path), failClose);
    SharedLifecycleEnvironment env(failClose, "cleanup-" + std::to_string(static_cast<int>(path))
        + (failClose ? "-failure" : "-success"));
    const auto first = env.indices[0], second = env.indices[1];
    const auto startLive = [&](size_t index) {
        REQUIRE(awaitLifecycle([&] { return env.manager.startStreaming(index, true); }));
        REQUIRE(awaitLifecycle([&] { return env.manager.getRuntimeStateLabel(index) == "live hardware"; }));
    };
    startLive(second);
    if (path == Path::CancelledOpen || path == Path::FailedOpen) {
        {
            std::lock_guard lock(SharedLifecycleFixture::mutex);
            SharedLifecycleFixture::holdActivation[0] = path == Path::CancelledOpen;
            SharedLifecycleFixture::activationResult[0] = path == Path::FailedOpen ? SOAPY_SDR_STREAM_ERROR : 0;
        }
        REQUIRE(env.manager.startStreaming(first, true));
        if (path == Path::CancelledOpen) {
            {
                std::unique_lock lock(SharedLifecycleFixture::mutex);
                REQUIRE(SharedLifecycleFixture::condition.wait_for(lock, std::chrono::seconds(5), [] {
                    return SharedLifecycleFixture::activationEntered[0];
                }));
            }
            env.manager.stopStreaming(first);
            SharedLifecycleFixture::release();
        }
    } else {
        startLive(first);
        if (path == Path::Stop) {
            env.manager.stopStreaming(first);
        } else {
            {
                std::unique_lock lock(SharedLifecycleFixture::mutex);
                SharedLifecycleFixture::holdClose[0] = SharedLifecycleFixture::throwRead[0] = true;
                REQUIRE(SharedLifecycleFixture::condition.wait_for(lock, std::chrono::seconds(5), [] {
                    return SharedLifecycleFixture::closeEntered[0];
                }));
            }
            // Cleanup is inside the native call, but fallback is already visible.
            // Its quarantine must already exist and state/publication locks be free.
            CHECK(env.manager.getRuntimeStateLabel(first) == "hardware failed, using stub");
            const auto start = std::chrono::steady_clock::now();
            CHECK_FALSE(env.manager.startStreaming(first, true));
            CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
            SharedLifecycleFixture::release();
        }
    }
    if (failClose) {
        REQUIRE(awaitLifecycle([&] { return env.failure->observed.load(std::memory_order_acquire); }));
        CHECK(SharedLifecycleFixture::closed[0] == 0);
        CHECK_FALSE(env.manager.startStreaming(first, true));
        env.manager.stopStreaming(second);
        CHECK(SharedLifecycleFixture::closed[1] == 1);
        CHECK(SharedLifecycleFixture::destroyed == 0);
        CHECK_FALSE(env.manager.startStreaming(first, true));
        CHECK_FALSE(env.manager.startStreaming(second, true));
    } else {
        REQUIRE(awaitLifecycle([] { return SharedLifecycleFixture::closed[0] == 1; }));
        env.manager.stopStreaming(second);
        REQUIRE(awaitLifecycle([] { return SharedLifecycleFixture::destroyed == 1; }));
        {
            std::lock_guard lock(SharedLifecycleFixture::mutex);
            SharedLifecycleFixture::activationResult[0] = 0;
        }
        startLive(first);
        env.manager.stopStreaming(first);
        CHECK(SharedLifecycleFixture::destroyed == 2);
    }
    env.live();
}
#endif
