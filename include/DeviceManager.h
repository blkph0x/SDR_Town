#pragma once

#include <vector>
#include <string>
#include <optional>
#include <nlohmann/json.hpp>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include <complex>

struct Receiver;  // forward for per-rx cursor methods (full def in Receiver.h, included in .cpp)

#ifdef HAVE_SOAPYSDR
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Types.hpp>
#include <SoapySDR/Modules.hpp>
#endif

struct DeviceInfo {
    std::string driver;           // e.g. "hackrf", "rtlsdr"
    std::string label;            // friendly name
    std::string serial;           // serial or unique id (important for multiple HackRFs)
    std::string hardware;         // hardware key
    std::string stableKey;         // driver + serial/hardware/label fallback for settings/re-enumeration identity
    std::vector<std::string> antennas;
    std::vector<double> sampleRates; // some common or full range later
    double minFreq = 0;
    double maxFreq = 0;
    // runtime
    bool enabled = false;
    double sampleRate = 2.4e6;
    double gain = 30.0;           // simplified master gain for now
    double gainMin = 0.0;
    double gainMax = 80.0;
    double frequencyCorrectionPpm = 0.0; // oscillator correction; persisted and applied live when supported
    std::string antenna;
    std::string gainName;         // for setGain with specific element (e.g. "TUNER" for RTL)
    // more per-device settings can be added
};

class DeviceManager {
public:
    static DeviceManager& instance();

    // Enumerate using SoapySDR (or stub).
    // probeHardware=true (default for dialog/rescan) does real Soapy::make probes on discovered
    // devices to populate antennas, sample rates, gain ranges, freq limits etc.
    // probeHardware=false for launch / initial count (completely avoids any hardware open/make
    // at startup so we never crash on open even with bad RTL driver/USB state).
    std::vector<DeviceInfo> enumerateDevices(bool probeHardware = true);

    // Activate / deactivate (for now just toggle flag + future stream start)
    bool setEnabled(size_t index, bool enabled);

    DeviceInfo* getDevice(size_t index);
    std::vector<DeviceInfo> getDevices() const { 
        std::lock_guard<std::mutex> lk(devicesMutex); 
        return devices; 
    }  // return copy under lock for thread-safety (P1)

    // Persistence (JSON in AppData)
    void loadSettings();
    void saveSettings() const;

    // Update a device's runtime params (gain, rate, antenna, oscillator correction)
    void updateDeviceParams(size_t index, double sampleRate, double gain, const std::string& antenna, double frequencyCorrectionPpm = 0.0);

    // Real streaming (PR3+)
    // attemptReal=true (default) tries the real Soapy Device::make + stream for hardware.
    // On launch/auto-start we pass false to start safe stub simulation only (prevents
    // crashes from flaky USB/driver state during ctor). User explicit "Apply" or
    // "Add/Scan" passes true (or default) and will upgrade a running stub to real if needed.
    bool startStreaming(size_t index, bool attemptReal = true);
    void stopStreaming(size_t index);
    bool isStreaming(size_t index) const;

    // Get next block of IQ samples (blocking with timeout or non-block)
    // Returns number of samples copied, or 0 if none.
    // NOTE: this is consuming (advances per-device read cursor) — used by primary spectrum/monitor.
    size_t getNextIQBlock(size_t index, std::complex<float>* buffer, size_t maxSamples, int timeoutMs = 10);

    // S0-3 (P1 audit): non-consuming recent window for per-receiver demod/channelizer.
    // Multiple receivers on the *same* device must each see the full recent RF capture
    // instead of fighting over one consuming queue (which caused sample drops/splits).
    // Returns up to maxSamples of the most recent IQ (from the live end of the per-dev queue)
    // without advancing any read offset. Short lock only.
    std::vector<std::complex<float>> getRecentIQWindow(size_t index, size_t maxSamples);
    struct RecentIQWindow {
        std::vector<std::complex<float>> samples;
        uint64_t startAbsolute = 0;
        uint64_t endAbsolute = 0;
        bool cursorDiscontinuity = false;
    };
    RecentIQWindow getRecentIQWindowWithCursor(size_t index, size_t maxSamples);

    // S0 / audit-followup-2 (P0): proper cursor-based consumption.
    // The receiver's lastConsumedAbsolute is updated. Returns only *new* samples in chronological order.
    // If the rx is too far behind the write cursor, log a drop, advance the cursor, and return a short zeroed/faded block.
    std::vector<std::complex<float>> getNewSamplesForReceiver(size_t devIndex, Receiver& rx, size_t maxSamples);
    RecentIQWindow getNewIQWindowForReceiver(size_t devIndex, Receiver& rx, size_t maxSamples);
    void setReceiverCursorToLiveEdge(size_t devIndex, Receiver& rx);
    // Place a receiver cursor slightly before the current live edge so a newly
    // created logical traffic-channel source can immediately decode with enough
    // Phase-2 pre-roll/context.  This is only safe for same-wideband/existing
    // sources; for physical retunes the caller should use live edge so old-RF IQ
    // is not decoded as traffic.
    void setReceiverCursorBeforeLiveEdge(size_t devIndex, Receiver& rx, size_t preRollSamples);

    // For spectrum: get latest power spectrum (dB) and center/sample info
    bool getLatestSpectrum(size_t index, std::vector<float>& powerDb, double& centerFreq, double& sampleRate);
    void setSpectrumFftBins(size_t index, size_t bins);
    size_t getSpectrumFftBins(size_t index) const;

    // Tune / scanner support
    void setCenterFreq(size_t index, double freqHz);

    // Live diagnostics (addressing audit P1 RF gain, P2 unlocked queue size)
    double getCurrentGain(size_t index) const;   // configured gain after clamping to the device's advertised range
    size_t getIQQueueDepth(size_t index) const;  // thread-safe locked peek of current iqQueue depth
    std::string getRuntimeStateLabel(size_t index) const;

    // P1: truly live hardware RF gain (separate from audioGain / displayGain).
    // If the device is currently streaming real hardware, this calls SoapySDR::setGain immediately.
    // Falls back to just updating the persisted setting if not streaming.
    void setLiveGain(size_t index, double gainDb);

    // Live oscillator correction. Uses native SoapySDR frequency correction when available,
    // otherwise tunes the hardware LO to a corrected frequency while keeping UI/logical center intact.
    void setFrequencyCorrection(size_t index, double ppm);

    // Diagnostics
    std::vector<std::string> getAvailableDrivers() const;

    // Setup for RTL-SDR discovery (paths + module load)
    void setupSoapyForRTLSDR();

private:
    DeviceManager() = default;
    std::vector<DeviceInfo> devices;
    mutable std::mutex devicesMutex;  // protects devices and streams access from enumerate vs workers (P1 race mitigation)

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

    // Internal streaming state
    struct StreamState {
        bool active = false;
        bool isReal = false;   // true only when we successfully did real Soapy make + activate (not the stub sim)
        std::string runtimeState = "stopped";
        std::thread rxThread;
        std::atomic<bool> rxThreadRunning{false};
        std::atomic<int> stubStopTimeoutCount{0};
        std::mutex lifecycleMutex;      // serializes start/stop and async real-hardware handoff
        // CRITICAL (P0 audit shutdown hang + best practice for untrusted SDR drivers):
        // realInitThread is intentionally a plain std::thread, not jthread.
        // SoapySDR::Device::make() (and the native USB stack) can block indefinitely.
        // We launch it, give it strong sessionGen + stopFlag guards so it never publishes stale state,
        // then on stop we try a short join and *detach* if it is still alive. Detaching the "stuck make"
        // thread is the only way to guarantee the rest of the application (GUI, CLI, installer, updater)
        // does not hang. The abandoned thread will die when the process exits.
        // This is the accepted pragmatic pattern for hardware that you do not control.
        std::thread realInitThread;
        std::mutex queueMutex;
        mutable std::mutex stateMutex;  // protects Soapy pointers + real/stub state handoff
        std::mutex ringMutex;           // protects iqRing against concurrent RX/write + DSP/spectrum reads
        std::deque<std::vector<std::complex<float>>> iqQueue;  // deque to support partial block consumption without dropping samples (P0 fix)
        size_t frontBlockReadOffset = 0;  // for consuming partials from front block without copying remainders under lock (P1)
        std::vector<float> latestPower;
        // Logical center requested by UI/scanner/P25 follow. Live hardware retunes are
        // queued via the sequence counters below and applied by rxThread between reads.
        double currentCenter = 0;
        double currentRate = 0;
        double frequencyCorrectionPpm = 0.0;
        bool nativeFrequencyCorrectionActive = false;
        std::atomic<bool> stopFlag{false};
        std::atomic<uint64_t> centerTuneRequestSeq{0};
        std::atomic<uint64_t> centerTuneAppliedSeq{0};

        // P1 audit: session generation to make init thread publishing and stop teardown safe.
        // Init thread captures the gen at launch; only publishes (soapyDev, active, rxThread, isReal)
        // if the gen still matches. stopStreaming bumps the gen before teardown.
        std::atomic<uint64_t> sessionGen{0};

        // S0 / audit-followup-2: per-device ring buffer with absolute sample count for per-receiver cursors.
        // Each receiver maintains its own lastConsumedAbsolute and pulls only *new* chronological samples.
        std::vector<std::complex<float>> iqRing;
        std::atomic<size_t> ringWriteIdx{0};       // wrapped write position
        std::atomic<uint64_t> totalSamplesWritten{0};
        size_t ringCapacity = 0;                   // power of 2

        // P1 SOTA spectrum pipeline state (real FFT + averaging + peak hold).
        // Maintained here so rxThread (or future separate worker) can publish high-res power.
        std::vector<float> spectrumAvg;   // exponential average per bin (dB)
        std::vector<float> spectrumPeak;  // peak-hold (with slow decay)
        size_t spectrumBins = 8192;       // 4096/8192/16384/65536 precision presets

#ifdef HAVE_SOAPYSDR
        SoapySDR::Device* soapyDev = nullptr;
        SoapySDR::Stream* rxStream = nullptr;
#endif
    };
    std::vector<std::unique_ptr<StreamState>> streams;

    StreamState* streamState(size_t index) const;
    void rxThreadFunc(size_t index, uint64_t expectedGeneration);  // background RX loop
    void resetStreamBuffers(StreamState& st);

    // Centralized append for both the consuming deque (for getNext/spectrum) and the per-rx ring.
    // Feeds ring *before* moving into queue so ring always gets the samples. Fixes the WFM ring bug.
    void appendIQBlock(size_t index, std::vector<std::complex<float>>&& block);

    // Real FFT power spectrum (8192-bin default, Blackman-Harris + Hann, used by rx pipeline and stub).
    // Returns fftshifted dB vector. See .cpp for radix-2 impl + windowing.
    std::vector<float> computeRealFFTPower(const std::vector<std::complex<float>>& time, size_t fftN, bool useBlackmanHarris);
};
