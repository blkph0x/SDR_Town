#pragma once
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Errors.hpp>
#include <atomic>
#include <algorithm>
#include <complex>
#include <map>
#include <stdexcept>
#include <thread>
#include <vector>

// Contract fixture, not RF emulation. Matches pinned SoapySDRPlay3 Settings.cpp.
class SdrplayControlFixture : public SoapySDR::Device {
public:
    static inline std::atomic<bool> produceSamples{false};
    std::vector<std::string> calls;
    std::string antenna = "Antenna A";
    std::string fail;
    bool ignoreWrites = false;
    bool agc = false;
    double rf = 4, ifgr = 40, bw = 1536000, rate = 2048000, frequency = 100e6, ppm = 0;
    std::map<std::string, std::string> settings{{"biasT_ctrl", "false"}, {"rfnotch_ctrl", "false"},
        {"dabnotch_ctrl", "false"}, {"hdr_ctrl", "false"}, {"iqcorr_ctrl", "true"}, {"agc_setpoint", "-30"}};
    void write(const std::string& name) {
        calls.push_back(name);
        if (!fail.empty() && name.rfind(fail, 0) == 0) throw std::runtime_error("fixture rejection: " + name);
    }
    std::string getDriverKey() const override { return "sdrplay_controls_fixture"; }
    std::string getHardwareKey() const override { return "RSPdx"; }
    size_t getNumChannels(int direction) const override { return direction == SOAPY_SDR_RX ? 1 : 0; }
    std::vector<std::string> listAntennas(int, size_t) const override { return {"Antenna A", "Antenna B", "Antenna C"}; }
    std::string getAntenna(int, size_t) const override { return antenna; }
    void setAntenna(int, size_t, const std::string& name) override { write("antenna=" + name); if (!ignoreWrites) antenna = name; }
    std::vector<std::string> listGains(int, size_t) const override { return {"IFGR", "RFGR"}; }
    SoapySDR::Range getGainRange(int, size_t, const std::string& name) const override { return name == "RFGR" ? SoapySDR::Range(0, 27) : SoapySDR::Range(20, 59); }
    bool hasGainMode(int, size_t) const override { return true; }
    bool getGainMode(int, size_t) const override { return agc; }
    void setGainMode(int, size_t, bool value) override { write(value ? "agc=1" : "agc=0"); if (!ignoreWrites) agc = value; }
    double getGain(int, size_t, const std::string& key) const override { return key == "RFGR" ? rf : ifgr; }
    void setGain(int, size_t, const std::string& key, double value) override {
        write("gain=" + key); if (ignoreWrites) return;
        if (key == "RFGR") rf = value;
        else if (!agc) ifgr = value;
    }
    std::vector<double> listBandwidths(int, size_t) const override { return {200000, 300000, 600000, 1536000, 5000000, 6000000, 7000000, 8000000}; }
    void setBandwidth(int, size_t, double value) override { write("bandwidth"); if (!ignoreWrites) bw = value == 0 ? 1536000 : value; }
    double getBandwidth(int, size_t) const override { return bw; }
    SoapySDR::ArgInfoList getSettingInfo() const override {
        SoapySDR::ArgInfoList result;
        for (const auto& [key, value] : settings) {
            SoapySDR::ArgInfo info; info.key = key; info.value = value;
            info.type = key == "agc_setpoint" ? SoapySDR::ArgInfo::INT : SoapySDR::ArgInfo::BOOL;
            result.push_back(info);
        }
        return result;
    }
    std::string readSetting(const std::string& key) const override { return settings.at(key); }
    void writeSetting(const std::string& key, const std::string& value) override { write(key + "=" + value); if (!ignoreWrites) settings.at(key) = value; }
    void setSampleRate(int, size_t, double value) override { rate = value; }
    double getSampleRate(int, size_t) const override { return rate; }
    std::vector<double> listSampleRates(int, size_t) const override { return {2e6, 2.048e6, 4e6}; }
    void setFrequency(int, size_t, double value, const SoapySDR::Kwargs&) override { frequency = value; }
    double getFrequency(int, size_t) const override { return frequency; }
    SoapySDR::RangeList getFrequencyRange(int, size_t) const override { return {SoapySDR::Range(1e3, 2e9)}; }
    bool hasFrequencyCorrection(int, size_t) const override { return true; }
    void setFrequencyCorrection(int, size_t, double value) override { ppm = value; }
    double getFrequencyCorrection(int, size_t) const override { return ppm; }
    SoapySDR::Stream* setupStream(int, const std::string&, const std::vector<size_t>&, const SoapySDR::Kwargs&) override { return reinterpret_cast<SoapySDR::Stream*>(this); }
    int activateStream(SoapySDR::Stream*, int, long long, size_t) override {
        // Reproduce upstream selectDevice resetting the antenna at activation.
        antenna = "Antenna A"; return 0;
    }
    int deactivateStream(SoapySDR::Stream*, int, long long) override { return 0; }
    void closeStream(SoapySDR::Stream*) override {}
    int readStream(SoapySDR::Stream*, void* const* buffers, size_t elements, int&, long long&, long) override {
        if(!produceSamples.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));return SOAPY_SDR_TIMEOUT;
        }
        // DEC-0137 / fubarzi: silent fixture IQ, not simulated satellite speech.
        std::fill_n(static_cast<std::complex<float>*>(buffers[0]),elements,std::complex<float>{});
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return static_cast<int>(elements);
    }
};
