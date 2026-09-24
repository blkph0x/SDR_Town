#include "InmarsatIqFile.h"
#include "InmarsatReplay.h"
#include "InmarsatPipeline.h"
#include "InmarsatDiagnostics.h"
#include "InmarsatVoice.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <QTemporaryDir>
#include <QtEndian>
#include <cstring>
#include <limits>

namespace {
void write(const QString& path, const QByteArray& bytes) {
    QFile f(path); REQUIRE(f.open(QIODevice::WriteOnly)); REQUIRE(f.write(bytes) == bytes.size());
}
QByteArray word(quint64 value, size_t size, bool big = false) {
    QByteArray data(static_cast<qsizetype>(size), 0);
    for (size_t i = 0; i < size; ++i) data[static_cast<qsizetype>(big ? size - i - 1 : i)] = char(value >> (8 * i));
    return data;
}
QByteArray floats(float i, float q, bool big = false) {
    quint32 a, b; std::memcpy(&a, &i, 4); std::memcpy(&b, &q, 4);
    return word(a, 4, big) + word(b, 4, big);
}
nlohmann::json metadata(const char* type = "cf32_le") {
    return {{"global", {{"core:datatype", type}, {"core:sample_rate", 48000}}},
            {"captures", {{{"core:sample_start", 0}, {"core:frequency", 1542935000}}}},
            {"annotations", nlohmann::json::array()}};
}
void meta(const QString& base, const nlohmann::json& json) { write(base + ".sigmf-meta", QByteArray::fromStdString(json.dump())); }
InmarsatReplaySnapshot waitFor(InmarsatReplay& replay, const std::function<bool(const InmarsatReplaySnapshot&)>& predicate) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < end) {
        const auto s = replay.snapshot();
        if (predicate(s)) return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    FAIL("Replay timed out"); return {};
}
}

TEST_CASE("Inmarsat IQ formats preserve component order endian and scaling", "[inmarsat][replay]") {
    QTemporaryDir dir; REQUIRE(dir.isValid());
    for (const auto& format : InmarsatIqFile::supportedFormats()) {
        INFO(format.toStdString());
        const bool big = format.endsWith("_be");
        QByteArray pair;
        float expectedI = -0.5f, expectedQ = 0.5f;
        if (format.startsWith("cf32")) pair = floats(-0.5f, 0.5f, big);
        else if (format.startsWith("cf64")) pair = word(0xbfe0000000000000ULL, 8, big) + word(0x3fe0000000000000ULL, 8, big);
        else if (format.startsWith("ci16")) pair = word(0xc000, 2, big) + word(0x4000, 2, big);
        else if (format.startsWith("ci32")) pair = word(0xc0000000, 4, big) + word(0x40000000, 4, big);
        else if (format.startsWith("cu16")) { pair = word(0, 2, big) + word(65535, 2, big); expectedI = -1; expectedQ = 1; }
        else if (format.startsWith("cu32")) { pair = word(0, 4, big) + word(0xffffffff, 4, big); expectedI = -1; expectedQ = 1; }
        else if (format == "ci8") pair = QByteArray::fromHex("c040");
        else { pair = QByteArray::fromHex("00ff"); expectedI = -1; expectedQ = 1; }
        const auto path = dir.filePath("capture.iq"); write(path, pair + pair);
        InmarsatIqFile reader;
        reader.open(path, {format, 48000, 1542935000});
        REQUIRE(reader.info().sampleCount == 2);
        const auto block = reader.read(1);
        REQUIRE(block.samples.size() == 1);
        CHECK(block.samples[0].real() == expectedI);
        CHECK(block.samples[0].imag() == expectedQ);
        CHECK(block.discontinuity);
        CHECK_FALSE(reader.read().discontinuity);
        CHECK(reader.read().samples.empty());
        reader.seek(0); CHECK(reader.read().discontinuity);
    }
}
TEST_CASE("Inmarsat SigMF boundaries never combine retuned samples", "[inmarsat][replay]") {
    QTemporaryDir dir;
    const auto base = dir.filePath("test");
    write(base + ".sigmf-data", floats(0.3f, -0.4f).repeated(10));
    auto m = metadata(); m["captures"].push_back({{"core:sample_start", 3}, {"core:frequency", 1542940000}}); meta(base, m);
    InmarsatIqFile reader; reader.open(base + ".sigmf-meta");
    const auto first = reader.read(); REQUIRE(first.samples.size() == 3);
    const auto second = reader.read(); REQUIRE(second.samples.size() == 7);
    CHECK(second.startSample == 3); CHECK(second.discontinuity); CHECK(second.centerHz == 1542940000);
    reader.seek(2); CHECK(reader.read().samples.size() == 1);
    reader.seek(10); CHECK(reader.read().samples.empty());
    CHECK_THROWS(reader.seek(11));
}
TEST_CASE("Inmarsat malformed metadata and samples fail closed", "[inmarsat][replay]") {
    QTemporaryDir dir; const auto base = dir.filePath("bad");
    write(base + ".sigmf-data", floats(0, 0).repeated(10));
    InmarsatIqFile reader;
    for (int variant = 0; variant < 8; ++variant) {
        auto m = metadata();
        if (variant == 0) m["global"]["core:num_channels"] = 2;
        if (variant == 1) m["global"]["core:dataset"] = "../other";
        if (variant == 2) m["global"]["core:sample_rate"] = 0;
        if (variant == 3) m["captures"][0]["core:sample_start"] = -1;
        if (variant == 4) m["captures"][0]["core:frequency"] = nullptr;
        if (variant == 5) m["captures"][0]["core:header_bytes"] = 1;
        if (variant == 6) m["captures"].push_back(m["captures"][0]);
        if (variant == 7) m["global"]["core:extensions"] = {{{"name", "required"}, {"optional", false}}};
        meta(base, m); CHECK_THROWS(reader.open(base + ".sigmf-meta"));
    }
    meta(base, metadata());
    write(base + ".sigmf-data", floats(std::numeric_limits<float>::quiet_NaN(), 0));
    reader.open(base + ".sigmf-meta"); CHECK_THROWS(reader.read());
    write(base + ".raw", QByteArray("abc"));
    CHECK_THROWS(reader.open(base + ".raw"));
    CHECK_THROWS(reader.open(base + ".raw", {"ci16_le", 48000, 1542935000}));
}
TEST_CASE("Inmarsat WAV validates container and requires explicit RF center", "[inmarsat][replay]") {
    QTemporaryDir dir;
    const auto path = dir.filePath("iq.wav");
    const QByteArray fmt = word(1, 2) + word(2, 2) + word(48000, 4) + word(192000, 4) + word(4, 2) + word(16, 2);
    const auto body = QByteArray("WAVEfmt ") + word(16, 4) + fmt + "JUNK" + word(1, 4) + QByteArray("x\0", 2) +
        "data" + word(4, 4) + word(16384, 2) + word(49152, 2);
    write(path, "RIFF" + word(body.size(), 4) + body);
    InmarsatIqFile reader;
    CHECK_THROWS(reader.open(path));
    reader.open(path, {"auto", 0, 1542935000});
    CHECK(reader.info().sampleRateHz == 48000);
    const auto block = reader.read(); REQUIRE(block.samples.size() == 1);
    CHECK(block.samples[0] == std::complex<float>(0.5f, -0.5f));
    write(path, "RIFF" + word(body.size() + 20, 4) + body);
    CHECK_THROWS(reader.open(path, {"auto", 0, 1542935000}));
}
TEST_CASE("Inmarsat shared pipeline is chunk invariant and counts discontinuities", "[inmarsat][replay]") {
    std::vector<std::complex<float>> iq(150000);
    for (size_t i = 0; i < iq.size(); ++i) iq[i] = {((i / 40) & 1) ? -0.4f : 0.4f, 0.0f};
    InmarsatPipeline live, file;
    live.process(iq.data(), iq.size(), 0, 48000, 1542935000, 1542935000, InmarsatDemodMode::EgcBpsk1200, true);
    for (size_t i = 0; i < iq.size(); i += 311) file.process(iq.data() + i, std::min<size_t>(311, iq.size() - i),
        i, 48000, 1542935000, 1542935000, InmarsatDemodMode::EgcBpsk1200, i == 0);
    for (const char* key : {"samples", "symbols", "rawBlocks", "quality", "resets", "discontinuities"})
        CHECK(live.report()[key] == file.report()[key]);
    CHECK_FALSE(file.report()["protocolLock"].get<bool>());
    CHECK(file.report()["pcmSamples"] == 0);
    file.process(iq.data(), 10, 3, 48000, 1542935000, 1542935000, InmarsatDemodMode::EgcBpsk1200, true);
    CHECK(file.report()["discontinuities"] == 1);
    CHECK(file.report()["resets"] == 2);
    CHECK_THROWS(file.process(iq.data(), 1, 0, 48000, 1542935000, 1543935000, InmarsatDemodMode::EgcBpsk1200, false));
}
TEST_CASE("Inmarsat replay paced and fast results agree with bounded reports", "[inmarsat][replay]") {
    QTemporaryDir dir; const auto base = dir.filePath("reference");
    write(base + ".sigmf-data", floats(.3f, -.2f).repeated(24000)); meta(base, metadata());
    InmarsatReplay replay; InmarsatReplayOptions options;
    options.path = base + ".sigmf-data"; options.logDirectory = dir.path();
    replay.start(options);
    const auto paced = waitFor(replay, [](const auto& s) { return !s.running; });
    REQUIRE(paced.state == "complete"); CHECK(paced.position == 24000);
    options.realTime = false; replay.start(options);
    const auto fast = waitFor(replay, [](const auto& s) { return !s.running; });
    REQUIRE(fast.state == "complete");
    for (const char* key : {"samples", "symbols", "rawBlocks", "quality", "pcmSamples"}) CHECK(paced.pipeline[key] == fast.pipeline[key]);
    QFile log(fast.logPath); REQUIRE(log.open(QIODevice::ReadOnly));
    const auto lines = log.readAll(); CHECK(lines.contains("summary")); CHECK(lines.contains("classic_aero_experimental"));
    CHECK(lines.size() < 8192);
    InmarsatVoice voice; CHECK_FALSE(voice.backendAvailable());
    int emits = 0; voice.setPcmSink([&](const int16_t*, int) { ++emits; });
    uint8_t bytes[120]{}; voice.feedBytes(bytes, 120); CHECK(emits == 0); CHECK(voice.framesDecoded() == 0);
}
TEST_CASE("Inmarsat replay pause seek stop and error reports are deterministic", "[inmarsat][replay]") {
    QTemporaryDir dir; const auto path = dir.filePath("long.iq");
    write(path, floats(.1f, .1f).repeated(480000));
    InmarsatReplayOptions options; options.path = path; options.input = {"cf32_le", 48000, 1542935000};
    options.logDirectory = dir.path();
    InmarsatReplay replay; replay.start(options);
    waitFor(replay, [](const auto& s) { return s.position > 0; });
    replay.pause(true); waitFor(replay, [](const auto& s) { return s.state == "paused"; });
    REQUIRE(replay.seek(2));
    waitFor(replay, [](const auto& s) { return s.position == 96000; });
    CHECK_FALSE(replay.seek(-1)); CHECK_FALSE(replay.seek(11));
    replay.pause(false);
    const auto resumed = waitFor(replay, [](const auto& s) { return s.position > 96000; });
    CHECK(resumed.pipeline["discontinuities"] == 1);
    const auto start = std::chrono::steady_clock::now(); replay.stop();
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    CHECK(replay.snapshot().state == "stopped");
    options.path = dir.filePath("missing.iq"); replay.start(options);
    const auto failure = waitFor(replay, [](const auto& s) { return !s.running; });
    CHECK(failure.state == "error"); CHECK_FALSE(failure.error.isEmpty()); CHECK(QFile::exists(failure.logPath));
}
TEST_CASE("Inmarsat remote summaries exclude recording and aircraft data", "[inmarsat][replay]") {
    auto j = InmarsatDiagnostics::remotePayload({{"samples", uint64_t{123}}, {"rateHz", 48000},
        {"inputPath", "private"}, {"error", "private path"}, {"aesId", 123}, {"centerHz", 1542935000},
        {"position", {1, 2}}, {"iq", {1, 2}}, {"audio", {1, 2}}, {"state", "complete"}});
    CHECK(j.size() == 3); CHECK(j["processedSampleCount"] == 123); CHECK_FALSE(j.contains("inputPath"));
    const auto audio = InmarsatDiagnostics::remotePayload({{"audio", {
        {"pcmReceived", 160}, {"pcmNonzero", 12}, {"pcmPeak", 100}, {"pcmRms", 2.5},
        {"speakerRequested", true}, {"speakerRunning", false}, {"speakerFailed", true},
        {"speakerConsumed", 0}, {"speakerError", "private device"}, {"wavPath", "private"},
        {"pcm", {1,2,3}}}}});
    CHECK(audio.size() == 8);
    CHECK(audio["pcmReceived"] == 160);
    CHECK(audio["speakerFailed"] == true);
    CHECK_FALSE(audio.contains("pcm"));
    CHECK_FALSE(audio.contains("wavPath"));
    CHECK_FALSE(audio.contains("speakerError"));
}

TEST_CASE("Inmarsat diagnostics enforce per-session consent and bounded remote cadence", "[inmarsat][replay]") {
    QTemporaryDir dir;
    std::vector<nlohmann::json> received;
    setInmarsatDiagnosticSink([&](const auto& j) { received.push_back(j); });
    struct Clear { ~Clear() { setInmarsatDiagnosticSink({}); } } clear;
    InmarsatDiagnostics privateLog;
    privateLog.open(dir.path(), "replay", false);
    privateLog.write("summary", {{"samples", uint64_t{32}}, {"state", "complete"}}, true);
    CHECK(received.empty());
    InmarsatDiagnostics optedIn;
    optedIn.open(dir.path(), "replay", true);
    optedIn.write("open", {});
    CHECK(received.empty());
    for (int i = 0; i < 20; ++i) optedIn.write("progress", {{"samples", uint64_t{32}}});
    REQUIRE(received.size() == 1);
    optedIn.write("summary", {{"samples", uint64_t{64}}}, true);
    REQUIRE(received.size() == 2);
    CHECK(received.back()["processedSampleCount"] == 64);
    CHECK(received.back()["event"] == "summary");
}
