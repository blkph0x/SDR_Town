#pragma once

#include <QFile>
#include <QString>
#include <QStringList>
#include <complex>
#include <cstdint>
#include <vector>

struct InmarsatIqOptions {
    QString format = "auto"; // auto = SigMF or RIFF IQ WAV, never guessed raw
    double sampleRateHz = 0;
    double centerHz = 0;
};

struct InmarsatIqCapture {
    uint64_t sampleStart = 0;
    double centerHz = 0;
};

struct InmarsatIqInfo {
    QString format;
    double sampleRateHz = 0;
    uint64_t sampleCount = 0;
    std::vector<InmarsatIqCapture> captures;
};

struct InmarsatIqBlock {
    std::vector<std::complex<float>> samples;
    uint64_t startSample = 0;
    double centerHz = 0;
    bool discontinuity = false;
};

// DEC-0120: a bounded chronological reader; file layout is never inferred from RF.
// All methods are worker-owned. Errors throw; EOF alone returns an empty block.
class InmarsatIqFile {
public:
    void open(const QString& path, const InmarsatIqOptions& options = {});
    const InmarsatIqInfo& info() const { return info_; }
    InmarsatIqBlock read(size_t maxSamples = 65536);
    void seek(uint64_t sample);
    uint64_t position() const { return position_; }
    static QStringList supportedFormats();

private:
    void openSigmf(const QString& path);
    void openWav(const QString& path, double centerHz);
    void openData(const QString& path);
    void validate();
    QFile file_;
    InmarsatIqInfo info_;
    uint64_t dataOffset_ = 0;
    uint64_t dataBytes_ = 0;
    uint64_t position_ = 0;
    size_t bytesPerSample_ = 0;
    bool discontinuity_ = true;
};
