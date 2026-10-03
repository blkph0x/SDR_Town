#include "CwSession.h"
#include "miniaudio.h"
#include <QFileInfo>
#include <array>
#include <stdexcept>
#include <spdlog/spdlog.h>

void decodeCwFile(const QString& path, CwOptions options, const CwCancel& cancel, const CwPublish& publish) {
    const QFileInfo info(path);
    // DEC-0167: bounded local diagnostic file; never loaded whole or uploaded.
    if (!info.isFile() || info.size() > 256ll * 1024 * 1024)
        throw std::runtime_error("Choose an audio recording no larger than 256 MiB");
    ma_decoder file{};
    auto config = ma_decoder_config_init(ma_format_f32, 1, 48000);
#ifdef _WIN32
    const auto opened = ma_decoder_init_file_w(path.toStdWString().c_str(), &config, &file);
#else
    const auto opened = ma_decoder_init_file(path.toUtf8().constData(), &config, &file);
#endif
    if (opened != MA_SUCCESS) throw std::runtime_error("Cannot decode this audio recording");
    struct Guard { ma_decoder& file; ~Guard() {ma_decoder_uninit(&file);} } guard{file};
    constexpr uint64_t sampleLimit = 48000ull * 30 * 60;
    ma_uint64 total = 0;
    if (ma_decoder_get_length_in_pcm_frames(&file, &total) == MA_SUCCESS && total > sampleLimit)
        throw std::runtime_error("CW recording exceeds 30 minutes");
    CwDecoder decoder(options);
    uint64_t first = 0;
    std::array<float, 4096> audio{};
    auto report = [&](const QString& status) { publish({decoder.snapshot(), info.fileName(), status, 0}); };
    report("Decoding recording");
    while (!cancel()) {
        ma_uint64 read = 0;
        const auto result = ma_decoder_read_pcm_frames(&file, audio.data(), audio.size(), &read);
        if (result != MA_SUCCESS && result != MA_AT_END) throw std::runtime_error("Audio recording read failed");
        if (!read) break;
        if (read > sampleLimit - first) throw std::runtime_error("CW recording exceeds 30 minutes");
        decoder.process(std::span(audio.data(), size_t(read)), 48000, 1, first);
        first += read;
        report("Decoding recording");
    }
    // Drain the backend's documented analysis history at EOF only. Not speaker
    // audio or synthetic decoded content; resets/gaps never bridge with padding.
    audio.fill(0);
    for (uint64_t tail = 0; tail < 48000 * 3 && !cancel(); tail += audio.size()) {
        const auto count = size_t(std::min<uint64_t>(audio.size(), 48000 * 3 - tail));
        decoder.process(std::span(audio.data(), count), 48000, 1, first + tail);
    }
    report(cancel() ? "Stopped" : "Recording complete");
    const auto stats = decoder.snapshot();
    spdlog::info("CW file complete inputSamples={} decoderSamples={} blocks={} resets={} rejected={} processingMs={:.1f}",
        first, stats.samples, stats.blocks, stats.resets, stats.rejected, stats.processingMs);
}
