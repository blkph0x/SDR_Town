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
    std::vector<std::string> antennas;
    std::vector<double> sampleRates; // some common or full range later
    double minFreq = 0;
    double maxFreq = 0;
    // runtime
    bool enabled = false;
    double sampleRate = 2.4e6;
    double gain = 30.0;           // simplified master gain for now
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

    // Update a device's runtime params (gain, rate, antenna)
    void updateDeviceParams(size_t index, double sampleRate, double gain, const std::string& antenna);

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
    size_t getNextIQBlock(size_t index, std::complex<float>* buffer, size_t maxSamples, int timeoutMs = 10);

    // For spectrum: get latest power spectrum (dB) and center/sample info
    bool getLatestSpectrum(size_t index, std::vector<float>& powerDb, double& centerFreq, double& sampleRate);

    // Tune / scanner support
    void setCenterFreq(size_t index, double freqHz);

    // Live diagnostics (addressing audit P1 RF gain, P2 unlocked queue size)
    double getCurrentGain(size_t index) const;   // the (possibly capped) configured gain for this dev
    size_t getIQQueueDepth(size_t index) const;  // thread-safe locked peek of current iqQueue depth

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
        std::thread rxThread;
        std::thread realInitThread;  // owned bg thread for real Soapy open; on stop we detach (with guards in lambda) to avoid hanging shutdown on stuck native make, while preventing resurrection.
        std::mutex queueMutex;
        std::deque<std::vector<std::complex<float>>> iqQueue;  // deque to support partial block consumption without dropping samples (P0 fix)
        size_t frontBlockReadOffset = 0;  // for consuming partials from front block without copying remainders under lock (P1)
        std::vector<float> latestPower;
        double currentCenter = 0;
        double currentRate = 0;
        std::atomic<bool> stopFlag{false};

        // P1 audit: session generation to make init thread publishing and stop teardown safe.
        // Init thread captures the gen at launch; only publishes (soapyDev, active, rxThread, isReal)
        // if the gen still matches. stopStreaming bumps the gen before teardown.
        std::atomic<uint64_t> sessionGen{0};

#ifdef HAVE_SOAPYSDR
        SoapySDR::Device* soapyDev = nullptr;
        SoapySDR::Stream* rxStream = nullptr;
#endif
    };
    std::vector<std::unique_ptr<StreamState>> streams;

    void rxThreadFunc(size_t index);  // background RX loop
};