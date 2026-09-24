#include "InmarsatVoice.h"

#include <cstdio>
#include <cstring>
#include <ctime>

struct InmarsatVoice::Impl {};

InmarsatVoice::InmarsatVoice() { impl_ = new Impl(); }
InmarsatVoice::~InmarsatVoice() {
    closeWav();
    delete impl_;
    impl_ = nullptr;
}

bool InmarsatVoice::backendAvailable() const {
    // DEC-0120: P25/DMR mbelib is not Aero mini-m AMBE4800x3600.
    return false;
}

void InmarsatVoice::reset() {
    acc_.clear();
    framesDecoded_ = 0;
}

int InmarsatVoice::decodeFrame(const uint8_t frame12[12], int16_t out160[160]) {
    if (!frame12 || !out160) return -1;
    // Fail closed until the Aero-specific interleaver/FEC/vocoder is integrated.
    (void)frame12;
    std::memset(out160, 0, 160 * sizeof(int16_t));
    return -1;
}

void InmarsatVoice::feedBytes(const uint8_t* data, size_t len) {
    if (!backendAvailable() || !data || len == 0) return;
    acc_.insert(acc_.end(), data, data + len);
    while (acc_.size() >= 12) {
        int16_t pcm[160];
        const int errs = decodeFrame(acc_.data(), pcm);
        acc_.erase(acc_.begin(), acc_.begin() + 12);
        if (errs < 0) continue;
        if (errs > 6) continue; // drop badly damaged frames
        if (pcmSink_) pcmSink_(pcm, 160);
        writePcm(pcm, 160);
    }
    if (acc_.size() > 256) acc_.erase(acc_.begin(), acc_.begin() + static_cast<std::ptrdiff_t>(acc_.size() - 64));
}

void InmarsatVoice::setRecording(bool on, const std::string& dir, uint32_t aesId) {
    if (!on || !backendAvailable()) {
        closeWav();
        record_ = false;
        return;
    }
    record_ = true;
    recordDir_ = dir;
    aesId_ = aesId;
}

void InmarsatVoice::closeWav() {
    if (!wav_) return;
    // Patch sizes
    const uint32_t dataBytes = wavSamples_ * 2;
    const uint32_t riffSize = 36 + dataBytes;
    std::fseek(wav_, 4, SEEK_SET);
    std::fwrite(&riffSize, 4, 1, wav_);
    std::fseek(wav_, 40, SEEK_SET);
    std::fwrite(&dataBytes, 4, 1, wav_);
    std::fclose(wav_);
    wav_ = nullptr;
    wavSamples_ = 0;
}

void InmarsatVoice::writePcm(const int16_t* pcm, int n) {
    if (!record_ || !pcm || n <= 0) return;
    if (!wav_) {
        char path[512];
        const std::time_t t = std::time(nullptr);
        std::snprintf(path, sizeof(path), "%s/inmarsat_voice_%06X_%lld.wav",
                      recordDir_.empty() ? "." : recordDir_.c_str(),
                      aesId_ & 0xFFFFFFu, static_cast<long long>(t));
#ifdef _WIN32
        if (fopen_s(&wav_, path, "wb") != 0) wav_ = nullptr;
#else
        wav_ = std::fopen(path, "wb");
#endif
        if (!wav_) return;
        // Minimal PCM WAV header (8 kHz mono 16-bit)
        const unsigned char hdr[44] = {
            'R','I','F','F', 0,0,0,0, 'W','A','V','E',
            'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
            0x40,0x1F,0,0, // 8000
            0x80,0x3E,0,0, // byte rate 16000
            2,0, 16,0,
            'd','a','t','a', 0,0,0,0
        };
        std::fwrite(hdr, 1, 44, wav_);
        wavSamples_ = 0;
    }
    std::fwrite(pcm, sizeof(int16_t), static_cast<size_t>(n), wav_);
    wavSamples_ += static_cast<uint32_t>(n);
}
