#include "InmarsatIqFile.h"

#include <QDir>
#include <QFileInfo>
#include <QtEndian>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {
void require(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
uint64_t integer(const nlohmann::json& object, const char* key, uint64_t fallback = 0) {
    if (!object.contains(key)) return fallback;
    const auto& value = object.at(key);
    require(value.is_number_integer() && value.get<double>() >= 0,
            "IQ metadata integer is negative or not an integer");
    return value.get<uint64_t>();
}
template<class T> T le(const char* data) {
    return qFromLittleEndian<T>(reinterpret_cast<const uchar*>(data));
}
template<class T> T word(const char* data, bool big) {
    return big ? qFromBigEndian<T>(reinterpret_cast<const uchar*>(data)) : le<T>(data);
}
}

QStringList InmarsatIqFile::supportedFormats() {
    return {"cf32_le", "cf32_be", "cf64_le", "cf64_be", "ci16_le", "ci16_be",
            "ci32_le", "ci32_be", "cu16_le", "cu16_be", "cu32_le", "cu32_be", "ci8", "cu8"};
}

void InmarsatIqFile::openData(const QString& path) {
    file_.setFileName(path);
    require(file_.open(QIODevice::ReadOnly), "Cannot open IQ data file");
    require(!file_.isSequential(), "IQ replay requires a seekable file");
    dataBytes_ = static_cast<uint64_t>(file_.size());
}

void InmarsatIqFile::open(const QString& path, const InmarsatIqOptions& options) {
    file_.close();
    info_ = {};
    dataOffset_ = dataBytes_ = position_ = 0;
    discontinuity_ = true;
    try {
        if (options.format == "auto" &&
            (path.endsWith(".sigmf-meta", Qt::CaseInsensitive) ||
             path.endsWith(".sigmf-data", Qt::CaseInsensitive))) {
            openSigmf(path);
        } else if (options.format == "wav_iq" ||
                   (options.format == "auto" && path.endsWith(".wav", Qt::CaseInsensitive))) {
            openWav(path, options.centerHz);
        } else {
            require(options.format != "auto", "Raw IQ requires explicit format, sample rate and center frequency");
            info_.format = options.format;
            info_.sampleRateHz = options.sampleRateHz;
            info_.captures.push_back({0, options.centerHz});
            openData(path);
        }
        validate();
        seek(0);
    } catch (...) {
        file_.close();
        info_ = {};
        throw;
    }
}

void InmarsatIqFile::openSigmf(const QString& path) {
    const QString base = path.left(path.lastIndexOf('.'));
    QFile meta(base + ".sigmf-meta");
    require(meta.open(QIODevice::ReadOnly), "Cannot open SigMF metadata");
    // DEC-0120 input bounds: metadata must not turn a replay open into a GB allocation.
    require(meta.size() > 0 && meta.size() <= 1024 * 1024, "SigMF metadata exceeds 1 MiB limit");
    const auto json = nlohmann::json::parse(meta.readAll().toStdString());
    const auto& global = json.at("global");
    info_.format = QString::fromStdString(global.at("core:datatype").get<std::string>());
    info_.sampleRateHz = global.at("core:sample_rate").get<double>();
    require(integer(global, "core:num_channels", 1) == 1, "Multi-channel SigMF is unsupported");
    require(integer(global, "core:offset") == 0 && integer(global, "core:trailing_bytes") == 0,
            "SigMF byte offsets/trailers are unsupported");
    require(!global.value("core:metadata_only", false), "Metadata-only SigMF cannot be replayed");
    if (global.contains("core:extensions")) {
        for (const auto& extension : global.at("core:extensions"))
            require(extension.value("optional", false),
                    "Required SigMF extensions are unsupported");
    }
    const auto& captures = json.at("captures");
    require(captures.is_array() && !captures.empty() && captures.size() <= 4096,
            "SigMF requires 1..4096 capture segments");
    for (const auto& capture : captures) {
        require(integer(capture, "core:header_bytes") == 0,
                "SigMF per-capture byte headers are unsupported");
        // Global-index discontinuities need an explicit time-gap model, not concatenation.
        require(!capture.contains("core:global_index"), "SigMF global-index gaps are unsupported");
        info_.captures.push_back({integer(capture, "core:sample_start"),
                                  capture.at("core:frequency").get<double>()});
    }
    QString dataPath = base + ".sigmf-data";
    if (global.contains("core:dataset")) {
        const auto name = QString::fromStdString(global.at("core:dataset").get<std::string>());
        require(!name.isEmpty() && name != "." && name != ".." &&
                QFileInfo(name).fileName() == name && !name.contains('\\') && !name.contains(':'),
                "SigMF dataset must be a filename beside the metadata");
        dataPath = QFileInfo(meta).dir().filePath(name);
    }
    openData(dataPath);
}

void InmarsatIqFile::openWav(const QString& path, double centerHz) {
    openData(path);
    const auto header = file_.read(12);
    require(header.size() == 12 && header.left(4) == "RIFF" && header.mid(8, 4) == "WAVE",
            "Expected RIFF IQ WAV; RF64/RIFX are unsupported");
    const uint64_t riffEnd = static_cast<uint64_t>(le<quint32>(header.constData() + 4)) + 8;
    require(riffEnd >= 12 && riffEnd <= static_cast<uint64_t>(file_.size()), "Truncated WAV container");
    bool haveFormat = false, haveData = false;
    uint64_t cursor = 12;
    size_t chunks = 0;
    while (cursor + 8 <= riffEnd) {
        require(++chunks <= 4096, "WAV chunk count exceeds limit");
        require(file_.seek(static_cast<qint64>(cursor)), "Cannot seek WAV header");
        const auto chunk = file_.read(8);
        require(chunk.size() == 8, "Truncated WAV chunk");
        const uint64_t length = le<quint32>(chunk.constData() + 4);
        const uint64_t next = cursor + 8 + length + (length & 1);
        require(next <= riffEnd, "WAV chunk exceeds container");
        if (chunk.left(4) == "fmt ") {
            require(!haveFormat && length >= 16, "Invalid or duplicate WAV format");
            const auto fmt = file_.read(16);
            require(fmt.size() == 16, "Truncated WAV format");
            const auto type = le<quint16>(fmt.constData());
            const auto channels = le<quint16>(fmt.constData() + 2);
            const auto rate = le<quint32>(fmt.constData() + 4);
            const auto byteRate = le<quint32>(fmt.constData() + 8);
            const auto align = le<quint16>(fmt.constData() + 12);
            const auto bits = le<quint16>(fmt.constData() + 14);
            require(channels == 2, "IQ WAV must contain exactly two channels: I then Q");
            require((type == 1 && bits == 16) || (type == 3 && bits == 32),
                    "IQ WAV supports PCM16 or IEEE float32 only");
            require(align == bits / 4 && static_cast<uint64_t>(byteRate) == static_cast<uint64_t>(rate) * align,
                    "Inconsistent WAV block alignment or byte rate");
            info_.format = type == 1 ? "ci16_le" : "cf32_le";
            info_.sampleRateHz = rate;
            haveFormat = true;
        } else if (chunk.left(4) == "data") {
            require(!haveData, "Multiple WAV data chunks are unsupported");
            dataOffset_ = cursor + 8;
            dataBytes_ = length;
            haveData = true;
        }
        cursor = next;
    }
    require(haveFormat && haveData, "WAV has no format or IQ data chunk");
    info_.captures.push_back({0, centerHz});
}

void InmarsatIqFile::validate() {
    require(supportedFormats().contains(info_.format), "Unsupported complex IQ datatype");
    bytesPerSample_ = info_.format.contains("64") ? 16 : info_.format.contains("32") ? 8 :
        info_.format.contains("16") ? 4 : 2;
    require(std::isfinite(info_.sampleRateHz) && info_.sampleRateHz >= 8000 && info_.sampleRateHz <= 40e6,
            "IQ sample rate must be between 8 kHz and 40 MHz");
    require(dataBytes_ > 0 && dataBytes_ % bytesPerSample_ == 0, "Empty or truncated IQ sample pair");
    info_.sampleCount = dataBytes_ / bytesPerSample_;
    require(!info_.captures.empty() && info_.captures.front().sampleStart == 0,
            "First IQ capture must begin at sample zero");
    uint64_t previous = 0;
    for (size_t i = 0; i < info_.captures.size(); ++i) {
        const auto& c = info_.captures[i];
        require(c.sampleStart < info_.sampleCount && (i == 0 || c.sampleStart > previous),
                "Capture segments are outside the file or not strictly increasing");
        require(std::isfinite(c.centerHz) && c.centerHz > 0 && c.centerHz <= 100e9,
                "Capture center frequency is required and must be finite (Hz)");
        previous = c.sampleStart;
    }
}

void InmarsatIqFile::seek(uint64_t sample) {
    require(file_.isOpen() && sample <= info_.sampleCount, "IQ seek is outside the recording");
    require(file_.seek(static_cast<qint64>(dataOffset_ + sample * bytesPerSample_)), "IQ seek failed");
    position_ = sample;
    discontinuity_ = true;
}

InmarsatIqBlock InmarsatIqFile::read(size_t maxSamples) {
    require(file_.isOpen(), "No IQ file is open");
    require(maxSamples > 0, "IQ read block must be nonempty");
    InmarsatIqBlock block;
    block.startSample = position_;
    if (position_ == info_.sampleCount) return block;
    const auto next = std::upper_bound(info_.captures.begin(), info_.captures.end(), position_,
        [](uint64_t p, const InmarsatIqCapture& c) { return p < c.sampleStart; });
    const auto& capture = *std::prev(next);
    const uint64_t end = next == info_.captures.end() ? info_.sampleCount : next->sampleStart;
    const auto count = static_cast<size_t>(std::min<uint64_t>({end - position_, maxSamples, 65536}));
    const auto bytes = file_.read(static_cast<qint64>(count * bytesPerSample_));
    require(bytes.size() == static_cast<qint64>(count * bytesPerSample_), "IQ data truncated or unreadable during replay");
    block.centerHz = capture.centerHz;
    block.discontinuity = discontinuity_ || position_ == capture.sampleStart;
    block.samples.resize(count);
    const bool big = info_.format.endsWith("_be");
    const auto type = info_.format.left(4);
    for (size_t i = 0; i < count; ++i) {
        const char* p = bytes.constData() + i * bytesPerSample_;
        float values[2];
        for (size_t component = 0; component < 2; ++component) {
            if (type == "cf32") {
                const quint32 bits = word<quint32>(p + component * 4, big);
                std::memcpy(&values[component], &bits, sizeof(float));
            } else if (type == "cf64") {
                const quint64 bits = word<quint64>(p + component * 8, big);
                double value;
                std::memcpy(&value, &bits, sizeof(double));
                require(std::isfinite(value) && std::abs(value) <= 1e6, "Invalid float64 IQ amplitude");
                values[component] = static_cast<float>(value);
            } else if (type == "ci16") {
                values[component] = word<qint16>(p + component * 2, big) / 32768.0f;
            } else if (type == "ci32") {
                values[component] = static_cast<float>(word<qint32>(p + component * 4, big) / 2147483648.0);
            } else if (type == "cu16") {
                values[component] = static_cast<float>((word<quint16>(p + component * 2, big) - 32767.5) / 32767.5);
            } else if (type == "cu32") {
                values[component] = static_cast<float>((word<quint32>(p + component * 4, big) - 2147483647.5) / 2147483647.5);
            } else if (type == "ci8") {
                values[component] = static_cast<qint8>(p[component]) / 128.0f;
            } else {
                values[component] = (static_cast<unsigned char>(p[component]) - 127.5f) / 127.5f;
            }
            require(std::isfinite(values[component]), "Non-finite IQ sample; decoder stopped before contamination");
        }
        block.samples[i] = {values[0], values[1]};
    }
    position_ += count;
    discontinuity_ = false;
    return block;
}
