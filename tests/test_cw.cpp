#include "CwDecoder.h"
#include "Demod.h"
#include "CwRfSession.h"
#include "Receiver.h"
#include "CwSession.h"
#include "miniaudio.h"
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <vector>

namespace {
// Independent ITU-R M.1677 timing fixture, not GGMorse's encoder.
std::vector<float> morse(double rate, double speed, double pitch) {
    std::vector<float> audio(size_t(rate), 0);
    auto segment = [&](int units, bool on) {
        const auto count = size_t(std::llround(rate * 1.2 / speed * units));
        for (size_t n = 0; n < count; ++n)
            audio.push_back(on ? float(.3 * std::sin(6.283185307179586 * pitch * audio.size() / rate)) : 0);
    };
    // Repeat for the adaptive estimator's acquisition; includes letters/numbers.
    for (int repeat = 0; repeat < 4; ++repeat) {
        for (const auto* letter : {"-.-.", "--.-", "", "-..", ".", "", "...-", "-.-", "..---", ".-", "-...", "-.-."}) {
            if (!*letter) { segment(4, false); continue; }
            for (auto p = letter; *p; ++p) { segment(*p == '-' ? 3 : 1, true); segment(1, false); }
            segment(2, false);
        }
        segment(4, false);
    }
    audio.resize(audio.size() + size_t(rate * 4), 0);
    return audio;
}
std::string decode(const std::vector<float>& audio, double rate, size_t chunk, CwOptions options = {}) {
    CwDecoder decoder(options);
    for (size_t start = 0; start < audio.size(); start += chunk)
        decoder.process(std::span(audio).subspan(start, std::min(chunk, audio.size() - start)), rate, 1, start);
    return decoder.snapshot().text;
}
}

TEST_CASE("CW independent timing fixture decodes with arbitrary block boundaries", "[cw]") {
    const auto audio = morse(48000, 20, 700);
    const auto regular = decode(audio, 48000, 4096);
    INFO(regular);
    REQUIRE(regular.find("CQ DE VK2ABC") != std::string::npos);
    REQUIRE(decode(audio, 48000, 997) == regular);
}

TEST_CASE("CW rejects invalid audio and separates discontinuous streams", "[cw]") {
    CwDecoder decoder;
    std::vector<float> silence(4800, 0);
    for (int n = 0; n < 60; ++n) decoder.process(silence, 48000, 1, n * 4800);
    REQUIRE(decoder.snapshot().text.empty());
    REQUIRE(decoder.snapshot().resets == 1);
    decoder.process(silence, 48000, 2, 0);
    REQUIRE(decoder.snapshot().resets == 2);
    decoder.process(silence, 48000, 2, 9600);
    REQUIRE(decoder.snapshot().resets == 3);
    silence[0] = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_THROWS(decoder.process(silence, 48000, 2, 14400));
    REQUIRE(decoder.snapshot().rejected == 1);
    REQUIRE_THROWS(CwDecoder(CwOptions{150, 20}));
}

TEST_CASE("Morse decoder recognizes keyed audio across analog RF demods", "[cw][rf]") {
    for (auto mode : {DemodMode::NFM, DemodMode::WFM, DemodMode::AM, DemodMode::USB, DemodMode::LSB, DemodMode::CW}) {
        INFO(int(mode));
        const double rate = mode == DemodMode::WFM ? 192000 : 48000;
        const auto audio = morse(rate, 20, 700);
        Demodulator demod;
        CwDecoder decoder;
        uint64_t first = 0;
        double phase = 0;
        for (size_t start = 0; start < audio.size(); start += 4800) {
            const auto count = std::min<size_t>(4800, audio.size() - start);
            std::vector<std::complex<float>> iq(count);
            for (size_t i = 0; i < count; ++i) {
                const double time = double(start + i) / rate;
                const bool on = audio[start + i] != 0;
                if (mode == DemodMode::NFM || mode == DemodMode::WFM) {
                    phase += 6.283185307179586 * (mode == DemodMode::WFM ? 75000 : 2000) * audio[start + i] / rate;
                    iq[i] = std::polar(.4f, float(phase));
                } else if (mode == DemodMode::AM) iq[i] = {.4f * (1 + audio[start + i]), 0};
                else if (mode == DemodMode::CW) iq[i] = {on ? .2f : 0, 0};
                else iq[i] = std::polar(on ? .2f : 0, float(6.283185307179586 * 700 * time * (mode == DemodMode::LSB ? -1 : 1)));
            }
            double level;
            const auto pcm = demod.demodulateToAudio(iq, rate, 7e6, 7e6, mode, level, 0, -140);
            decoder.process(pcm, 48000, 1, first); first += pcm.size();
        }
        INFO(decoder.snapshot().text);
        REQUIRE(decoder.snapshot().text.find("CQ DE VK2ABC") != std::string::npos);
        REQUIRE(decoder.snapshot().resets == 1);
    }
}

TEST_CASE("Morse recording reader produces text and cancellation is bounded", "[cw][file]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = directory.filePath("morse-test.wav");
    const auto samples = morse(44100, 25, 850);
    ma_encoder writer{};
    auto config = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 1, 44100);
    REQUIRE(ma_encoder_init_file(path.toStdString().c_str(), &config, &writer) == MA_SUCCESS);
    ma_uint64 written = 0;
    REQUIRE(ma_encoder_write_pcm_frames(&writer, samples.data(), samples.size(), &written) == MA_SUCCESS);
    ma_encoder_uninit(&writer);
    REQUIRE(written == samples.size());
    CwProgress result;
    decodeCwFile(path, {}, [] {return false;}, [&](const auto& p) {result = p;});
    INFO(result.decoder.text);
    REQUIRE(result.decoder.text.find("CQ DE VK2ABC") != std::string::npos);
    REQUIRE(result.status == "Recording complete");
    decodeCwFile(path, {}, [] {return true;}, [&](const auto& p) {result = p;});
    REQUIRE(result.status == "Stopped");
    REQUIRE(result.decoder.samples == 0);
}

TEST_CASE("Morse live observer rejects inactive and unassigned sources", "[cw][rf]") {
    REQUIRE_THROWS(cwReceiverSource({}));
    auto receiver = std::make_shared<Receiver>();
    receiver->deviceIndex = size_t(-1);
    auto run = cwReceiverSource(receiver);
    REQUIRE_THROWS(run({}, [] {return false;}, [](const auto&) {}));
}
