#include "frontend/DvbSurvey.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
void fft(std::vector<std::complex<double>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * 3.14159265358979323846 / static_cast<double>(len);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t j = 0; j < len / 2; ++j) {
                const auto u = a[i + j];
                const auto v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}
}

DvbSurveyResult surveyIfCapture(const std::complex<float>* iq, std::size_t count, double sampleRateHz) {
    DvbSurveyResult out;
    out.note = dvbStageNote();
    if (!iq || count < 16 || !std::isfinite(sampleRateHz) || sampleRateHz <= 0.0) return out;
    double power = 0.0;
    for (std::size_t i = 0; i < count; ++i) power += std::norm(iq[i]);
    out.meanPower = power / static_cast<double>(count);
    out.powerMeasured = std::isfinite(out.meanPower);
    std::size_t n = 1;
    while ((n << 1) <= count && n < 4096) n <<= 1;
    std::vector<std::complex<double>> bins(n);
    for (std::size_t i = 0; i < n; ++i) bins[i] = iq[i];
    fft(bins);
    std::size_t peak = 1;
    double peakMag = 0.0;
    std::vector<double> mag(n / 2);
    for (std::size_t i = 1; i < n / 2; ++i) {
        mag[i] = std::norm(bins[i]);
        if (mag[i] > peakMag) { peakMag = mag[i]; peak = i; }
    }
    out.peakMeasured = peakMag > 0.0;
    out.peakOffsetHz = static_cast<double>(peak) * sampleRateHz / static_cast<double>(n);
    std::vector<double> sorted(mag.begin() + 1, mag.end());
    if (!sorted.empty()) {
        std::sort(sorted.begin(), sorted.end());
        const double median = sorted[sorted.size() / 2];
        const double gate = median * 4.0;
        std::size_t occupied = 0;
        for (std::size_t i = 1; i < mag.size(); ++i) if (mag[i] >= gate) ++occupied;
        out.occupancyHz = static_cast<double>(occupied) * sampleRateHz / static_cast<double>(n);
    }
    return out;
}

ClearTsInventory inventoryClearTransportStream(const std::uint8_t* data, std::size_t size) {
    ClearTsInventory out;
    if (!data || size < 188) return out;
    std::size_t start = 0;
    bool found = false;
    for (std::size_t i = 0; i < 188 && i < size; ++i) {
        if (data[i] != 0x47) continue;
        if (i + 188 < size && data[i + 188] != 0x47) continue;
        start = i;
        found = true;
        break;
    }
    if (!found) return out;
    out.aligned = true;
    for (std::size_t i = start; i + 188 <= size; i += 188) {
        if (data[i] != 0x47) break;
        ++out.packets;
        const int pid = ((data[i + 1] & 0x1F) << 8) | data[i + 2];
        const int scramble = (data[i + 3] >> 6) & 0x3;
        if (scramble != 0) ++out.scrambledPackets;
        if (out.pids.size() < 64 && std::find(out.pids.begin(), out.pids.end(), pid) == out.pids.end())
            out.pids.push_back(pid);
    }
    return out;
}

bool dvbDemodAvailable() { return true; }
bool commercialDecryptAvailable() { return false; }
const char* dvbStageNote() {
    return "QPSK, 8PSK, 16APSK and 32APSK frames decode, short and normal, with or without pilots, from symbols or from root-raised-cosine samples at a known symbol rate. Short rate 9/10 does not exist. The app does not decode pictures; a clear transport stream is opened by the OS player. Clear TS playback refuses scrambled packets. Commercial decrypt is refused.";
}
