#include <catch2/catch_test_macros.hpp>
#include "AudioCapture.h"
#include "AudioEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <cmath>
#include <thread>
#include <chrono>
#include <vector>

TEST_CASE("AudioCapture ring pull and meters work with silence path", "[audio][capture]")
{
    int argc = 0;
    char* argv[] = {nullptr};
    QCoreApplication app(argc, argv);
    app.setApplicationName("SDR Town Test");
    app.setOrganizationName("SDR_Town");

    AudioCapture cap;
    auto devs = cap.enumerateCaptureDevices();
    // CI/sandbox may have zero capture devices — treat as skip, not fail.
    if (devs.empty()) {
        WARN("No capture devices; skipping live mic test");
        return;
    }

    REQUIRE(cap.start(0, 48000.0));
    REQUIRE(cap.isCapturing());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::vector<float> buf(4096);
    size_t got = 0;
    for (int i = 0; i < 20 && got == 0; ++i) {
        got = cap.pull(buf.data(), buf.size());
        if (got == 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // Some environments grant a device but feed silence only — still expect frames.
    REQUIRE(cap.framesCaptured() > 0);

    float meter = cap.levelMeter();
    REQUIRE(std::isfinite(meter));
    REQUIRE(meter >= 0.0f);
    REQUIRE(meter <= 1.0f);

    // 8 kHz pull should not crash
    size_t got8k = cap.pullAtRate(buf.data(), 160, 8000.0);
    REQUIRE(got8k <= 160);

    cap.stop();
    REQUIRE_FALSE(cap.isCapturing());
}

TEST_CASE("AudioEngine output mute gate", "[audio][capture]")
{
    int argc = 0;
    char* argv[] = {nullptr};
    QCoreApplication app(argc, argv);
    app.setApplicationName("SDR Town Test");
    app.setOrganizationName("SDR_Town");

    AudioEngine eng;
    REQUIRE_FALSE(eng.isOutputMuted());
    eng.setOutputMuted(true);
    REQUIRE(eng.isOutputMuted());
    eng.setOutputMuted(false);
    REQUIRE_FALSE(eng.isOutputMuted());
}
