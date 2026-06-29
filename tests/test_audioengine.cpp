#include <catch2/catch_test_macros.hpp>
#include "AudioEngine.h"
#include <stdexcept>
#include <thread>
#include <chrono>
#include <vector>
#include <cstdint>
#include <new>

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
        // Should find at least one on Windows (even if virtual)
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
