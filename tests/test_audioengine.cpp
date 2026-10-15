#include <catch2/catch_test_macros.hpp>
#include "AudioEngine.h"
#include <stdexcept>
#include <thread>
#include <chrono>
#include <vector>
#include <cstdint>
#include <new>
#include <future>
#include <algorithm>
#include <functional>

struct AudioEngineTestAccess {
    static std::vector<float> callback(AudioEngine::ActiveOutput& output,
                                      ma_uint32 frames, ma_uint32 channels = 2) {
        ma_device device{};
        device.pUserData = &output;
        device.playback.channels = channels;
        std::vector<float> pcm(static_cast<size_t>(frames) * channels, 99.0f);
        AudioEngine::dataCallback(&device, pcm.data(), nullptr, frames);
        return pcm;
    }
};

namespace {
std::shared_ptr<AudioEngine::ActiveOutput> addTestOutput(AudioEngine& engine,
                                                       size_t capacity = 16) {
    auto output = std::make_shared<AudioEngine::ActiveOutput>();
    output->owningEngine = &engine;
    output->ring.init(capacity);
    output->valid.store(true);
    engine.m_active.push_back(output);
    engine.setMasterVolume(1.0f);
    return output; // No device opened or playback started.
}

void checkFrames(const std::vector<float>& pcm, const std::vector<float>& expected,
                 size_t channels = 2) {
    REQUIRE(pcm.size() == expected.size() * channels);
    for (size_t i = 0; i < expected.size(); ++i)
        for (size_t c = 0; c < channels; ++c)
            REQUIRE(pcm[i * channels + c] == expected[i]);
}
}

#ifdef SDR_TOWN_AUDIO_TEST_HOOKS
namespace audioEngineTestHooks {
thread_local std::function<void()> onWait, onCursor, onTrim;
void once(std::function<void()>& hook) {
    auto call = std::move(hook);
    hook = {};
    if (call) call();
}
void consumerWaiting() { once(onWait); }
void callbackCursorLoaded() { once(onCursor); }
void trimLeaseAcquired() { once(onTrim); }
}

TEST_CASE("Production trim waits for callback cursor and prevents sample reuse", "[audioengine][overlap]") {
    for (bool selectedOnly : {false, true}) {
        AudioEngine engine;
        auto first = addTestOutput(engine), second = addTestOutput(engine);
        for (const auto& output : {first, second}) {
            output->ring.readPos.store(12);
            output->ring.writePos.store(12);
        }
        const std::vector<float> old{1,2,3,4,5,6,7,8,9,10,11,12};
        const std::vector<float> next{101,102,103,104,105,106,107,108};
        engine.pushAudio(old.data(), old.size());
        std::promise<void> cursor, release, waiting, pushing;
        auto cursorReady = cursor.get_future(), releaseReady = release.get_future();
        auto waitingReady = waiting.get_future(), pushingReady = pushing.get_future();
        std::vector<float> pcm;
        std::thread callback([&] {
            audioEngineTestHooks::onCursor = [&] { cursor.set_value(); releaseReady.wait(); };
            pcm = AudioEngineTestAccess::callback(*first, 4);
        });
        cursorReady.wait(); // Real callback now owns cursor 12 and samples 1..4.
        size_t dropped = 0;
        std::thread trim([&] {
            audioEngineTestHooks::onWait = [&] { waiting.set_value(); };
            dropped = engine.trimQueuedAudio(4, selectedOnly ? std::vector<size_t>{0,0,99}
                                                           : std::vector<size_t>{});
        });
        const bool excluded = waitingReady.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        std::thread producer([&] {
            pushing.set_value();
            engine.pushAudioToActiveOutputs(next.data(), next.size(), {0});
        });
        pushingReady.wait();
        const size_t heldCursor = first->ring.readPos.load();
        release.set_value();
        callback.join();
        trim.join();
        producer.join();
        REQUIRE(excluded);
        REQUIRE(heldCursor == 12);
        REQUIRE(dropped == (selectedOnly ? 4 : 12));
        REQUIRE(engine.producerDroppedFrames.load() == dropped);
        checkFrames(pcm, {1,2,3,4});
        checkFrames(AudioEngineTestAccess::callback(*first, 12),
                    {9,10,11,12,101,102,103,104,105,106,107,108});
        REQUIRE(first->ring.readPos.load() == first->ring.writePos.load());
        checkFrames(AudioEngineTestAccess::callback(*first, 2), {0,0});
        checkFrames(AudioEngineTestAccess::callback(*second, selectedOnly ? 12 : 4),
                    selectedOnly ? old : std::vector<float>{9,10,11,12});
    }
}

TEST_CASE("Production callback returns silence without waiting for trim lease", "[audioengine][overlap]") {
    AudioEngine engine;
    auto output = addTestOutput(engine);
    const std::vector<float> marked{1,2,3,4,5,6,7,8};
    engine.pushAudio(marked.data(), marked.size());
    std::promise<void> leased, release;
    auto leasedReady = leased.get_future(), releaseReady = release.get_future();
    size_t dropped = 0;
    std::thread trim([&] {
        audioEngineTestHooks::onTrim = [&] { leased.set_value(); releaseReady.wait(); };
        dropped = engine.trimQueuedAudio(4);
    });
    leasedReady.wait();
    auto callback = std::async(std::launch::async, [&] {
        auto first = AudioEngineTestAccess::callback(*output, 3, 8);
        auto second = AudioEngineTestAccess::callback(*output, 3, 8);
        first.insert(first.end(), second.begin(), second.end());
        return first;
    });
    const bool nonblocking = callback.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    const size_t cursor = output->ring.readPos.load();
    release.set_value();
    trim.join();
    const auto pcm = callback.get();
    REQUIRE(nonblocking);
    REQUIRE(cursor == 0);
    checkFrames(pcm, std::vector<float>(6, 0), 8);
    REQUIRE(engine.controlSilenceFrames.load() == 6);
    REQUIRE(engine.consumedFrames.load() == 0);
    REQUIRE(engine.underrunCount.load() == 0);
    REQUIRE(dropped == 4);
    checkFrames(AudioEngineTestAccess::callback(*output, 4), {5,6,7,8});
}
#endif

TEST_CASE("Audio ring clear cannot resurrect an in-flight consumer cursor", "[audioengine][cursor]") {
    AudioEngine::RingBuffer rb;
    rb.init(1024);
    rb.readPos.store(100);
    rb.writePos.store(500);
    std::promise<void> attempting;
    auto ready = attempting.get_future();
    std::thread clear;
    {
        AudioEngine::RingBuffer::ConsumerLease callback(rb, false);
        REQUIRE(callback.acquired);
        const size_t oldRead = rb.readPos.load();
        clear = std::thread([&] {
            attempting.set_value();
            rb.discardAll();
        });
        ready.wait();
        // The old implementation allowed clear to store 500 before this
        // callback stored 148, resurrecting 352 discarded samples.
        rb.readPos.store(oldRead + 48, std::memory_order_release);
    }
    clear.join();
    REQUIRE(rb.readPos.load() == 500);
    AudioEngine::RingBuffer::ConsumerLease nextCallback(rb, false);
    REQUIRE(nextCallback.acquired);
}

TEST_CASE("Audio callback never waits for a control cursor mutation", "[audioengine][cursor]") {
    AudioEngine::RingBuffer rb;
    rb.init(1024);
    AudioEngine::RingBuffer::ConsumerLease control(rb, true);
    {
        AudioEngine::RingBuffer::ConsumerLease callback(rb, false);
        REQUIRE_FALSE(callback.acquired);
    }
    // A failed attempt must not release another owner's lease.
    AudioEngine::RingBuffer::ConsumerLease otherCallback(rb, false);
    REQUIRE_FALSE(otherCallback.acquired);
}

TEST_CASE("AudioEngine ring buffer requires power-of-two capacity", "[audioengine]") {
    AudioEngine::RingBuffer rb;
    rb.init(1u << 18);
    REQUIRE(rb.capacity == (1u << 18));
    REQUIRE_FALSE(rb.data.empty());

    AudioEngine::RingBuffer bad;
    REQUIRE_THROWS_AS(bad.init(48000 * 4), std::invalid_argument);
}

TEST_CASE("AudioEngine ring cursors are cache-line separated", "[audioengine]") {
    AudioEngine::RingBuffer rb;
    rb.init(1u << 18);
    const auto writeAddr = reinterpret_cast<std::uintptr_t>(&rb.writePos);
    const auto readAddr = reinterpret_cast<std::uintptr_t>(&rb.readPos);
    constexpr std::size_t line = std::hardware_destructive_interference_size;
    REQUIRE(writeAddr % line == 0);
    REQUIRE(readAddr % line == 0);
    REQUIRE(writeAddr / line != readAddr / line);
}

TEST_CASE("Playback callback covers every WASAPI channel from mono", "[audioengine][channels]") {
    // Collector 2026-10-09: HyperX Virtual Surround / VB-CABLE are 2+ channels.
    // Writing frameCount floats into a stereo buffer leaves the other channel
    // uninitialized and sounds like jitter on every device mix format.
    for (ma_uint32 channels : {1u, 2u, 8u}) {
        AudioEngine engine;
        auto output = addTestOutput(engine);
        const std::vector<float> mono{0.1f, 0.2f, 0.3f, 0.4f};
        engine.pushAudio(mono.data(), mono.size());
        checkFrames(AudioEngineTestAccess::callback(*output, 6, channels),
                    {0.1f,0.2f,0.3f,0.4f,0,0}, channels);
        REQUIRE(engine.consumedFrames.load() == 4);
        REQUIRE(engine.zeroFillFrames.load() == 2);
        REQUIRE(engine.partialCallbacks.load() == 1);
    }
}

TEST_CASE("AudioEngine multi-device and push", "[audioengine]") {
    AudioEngine* eng = nullptr;
    try {
        eng = new AudioEngine();
    } catch (...) {
        WARN("AudioEngine construction failed (possibly no audio hardware in test env) - skipping full test");
        return;
    }

    SECTION("Enumerate playback devices") {
        auto devs = eng->enumeratePlaybackDevices();
        // CI/headless runners often have zero playback devices — skip, don't fail.
        if (devs.empty()) {
            WARN("No playback devices; skipping enumerate assertion");
            return;
        }
        REQUIRE(devs.size() >= 1);
    }

    SECTION("Activate multiple outputs and volumes (if devices available)") {
        auto devs = eng->enumeratePlaybackDevices();
        if (devs.size() >= 2) {
            eng->setActiveOutputs({0, 1});
            REQUIRE(eng->activeOutputCount() == 2);

            eng->setMasterVolume(0.8f);
            eng->setOutputVolume(0, 0.9f);
            eng->setOutputVolume(1, 0.7f);

            std::vector<float> samples(480, 0.1f);
            eng->pushAudio(samples.data(), samples.size());

            eng->playTestTone(0, 1000.0f, 0.1f);
            eng->playTestTone(1, 440.0f, 0.1f);

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    SECTION("No crash on bad indices or empty") {
        eng->setActiveOutputs({999});
        eng->pushAudio(nullptr, 0);
        eng->playTestTone(999);
    }

    delete eng;
}
