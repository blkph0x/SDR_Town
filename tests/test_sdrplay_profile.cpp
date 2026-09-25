#include <catch2/catch_test_macros.hpp>
#include "SdrplayProfile.h"

#include <algorithm>
#include <cmath>

TEST_CASE("SdrplayProfile detects driver names", "[sdrplay]") {
    CHECK(SdrplayProfile::isSdrplayDriver("sdrplay"));
    CHECK(SdrplayProfile::isSdrplayDriver("sdrPlay"));
    CHECK_FALSE(SdrplayProfile::isSdrplayDriver("rtlsdr"));
    CHECK_FALSE(SdrplayProfile::isSdrplayDriver("hackrf"));
}

TEST_CASE("SdrplayProfile normalizes USB and network models", "[sdrplay]") {
    CHECK(SdrplayProfile::normalizeModel("RSPdx", "unused") == "RSPdx");
    CHECK(SdrplayProfile::normalizeModel("", "SDRplay Dev0 RSPdx 12345") == "RSPdx");
    CHECK(SdrplayProfile::normalizeModel("", "SDRplay Dev0 RSPdx-R2 12345") == "RSPdx-R2");
    CHECK(SdrplayProfile::normalizeModel("", "SDRplay Dev0 RSPduo 123 - Dual Tuner") == "RSPduo");
    CHECK(SdrplayProfile::normalizeModel("", "RSP1A serial") == "RSP1A");
    CHECK(SdrplayProfile::normalizeModel("", "RSP1B") == "RSP1B");
    CHECK(SdrplayProfile::normalizeModel("RSPdx-R2", "") == "RSPdx-R2");
    CHECK(SdrplayProfile::normalizeModel("", "RSP2pro") == "RSP2");
    CHECK(SdrplayProfile::normalizeModel("", "SDRplay nRSP-ST network receiver") == "nRSP-ST");
}

TEST_CASE("SdrplayProfile duo mode display names", "[sdrplay]") {
    CHECK(SdrplayProfile::duoModeDisplayName("ST") == "Single Tuner");
    CHECK(SdrplayProfile::duoModeDisplayName("DT") == "Dual Tuner");
    CHECK(SdrplayProfile::duoModeDisplayName("MA") == "Master");
    CHECK(SdrplayProfile::duoModeDisplayName("MA8") == "Master (8 MHz)");
    CHECK(SdrplayProfile::duoModeDisplayName("SL") == "Slave");
}

TEST_CASE("SdrplayProfile uses only probed driver controls", "[sdrplay]") {
    auto caps = SdrplayProfile::capabilitiesFromProbe(
        "sdrplay", "RSPdx", "SDRplay RSPdx",
        {"IFGR", "RFGR"},
        {"Antenna A", "Antenna B", "Antenna C"},
        {200e3, 300e3, 600e3, 1.536e6, 5e6, 6e6, 7e6, 8e6},
        {"iqcorr_ctrl", "biasT_ctrl", "rfnotch_ctrl", "dabnotch_ctrl", "hdr_ctrl", "agc_setpoint", "rfgain_sel"},
        {{"rfgain_sel", {"0", "1", "2", "3", "4"}}});
    CHECK(caps.isSdrplay);
    CHECK(caps.probeVerified);
    CHECK(caps.model == "RSPdx");
    CHECK(caps.hasAgc);
    CHECK(caps.hasIfgr);
    CHECK(caps.hasRfgr);
    CHECK(SdrplayProfile::settingSupported(caps, SdrplaySettings::kHdr));
    CHECK(SdrplayProfile::settingSupported(caps, SdrplaySettings::kBiasT));
    CHECK_FALSE(SdrplayProfile::settingSupported(caps, "not_a_real_key"));

    auto defaults = SdrplayProfile::defaultSettings(caps);
    CHECK_FALSE(defaults.agcEnabled);
    CHECK(defaults.soapySettings.count(SdrplaySettings::kIqCorr) == 1);
    CHECK(defaults.soapySettings.count(SdrplaySettings::kHdr) == 1);
    CHECK(SdrplayProfile::parseBoolSetting("true"));
    CHECK_FALSE(SdrplayProfile::parseBoolSetting("false"));
    CHECK(SdrplayProfile::boolSetting(true) == "true");
}

TEST_CASE("SdrplayProfile does not invent capabilities after an empty probe", "[sdrplay]") {
    auto caps = SdrplayProfile::capabilitiesFromProbe(
        "sdrplay", "RSPdx", "SDRplay RSPdx", {}, {}, {}, {}, {});
    CHECK(caps.isSdrplay);
    CHECK_FALSE(caps.probeVerified);
    CHECK_FALSE(caps.hasAgc);
    CHECK_FALSE(caps.hasIfgr);
    CHECK_FALSE(caps.hasRfgr);
    CHECK(caps.gainElements.empty());
    CHECK_FALSE(SdrplayProfile::settingSupported(caps, SdrplaySettings::kBiasT));
    CHECK(SdrplayProfile::defaultSettings(caps).soapySettings.empty());
}

TEST_CASE("SdrplayProfile ignores non-SDRplay probes", "[sdrplay]") {
    auto caps = SdrplayProfile::capabilitiesFromProbe(
        "rtlsdr", "RTL", "RTL-SDR", {"TUNER"}, {"RX"}, {}, {}, {});
    CHECK_FALSE(caps.isSdrplay);
    auto defaults = SdrplayProfile::defaultSettings(caps);
    CHECK(defaults.soapySettings.empty());
}

TEST_CASE("SdrplayProfile known setting keys cover SoapySDRPlay3", "[sdrplay]") {
    const auto& keys = SdrplayProfile::knownSettingKeys();
    CHECK(keys.size() >= 8);
    CHECK(std::find(keys.begin(), keys.end(), SdrplaySettings::kBiasT) != keys.end());
    CHECK(std::find(keys.begin(), keys.end(), SdrplaySettings::kHdr) != keys.end());
}

TEST_CASE("SdrplayProfile enforces connector-specific tuning ranges", "[sdrplay]") {
    const auto dxC = SdrplayProfile::antennaCapabilities("RSPdx-R2", "Antenna C");
    REQUIRE(dxC.has_value());
    CHECK(dxC->connector == "BNC");
    CHECK(dxC->maxFrequencyHz == 200.0e6);
    CHECK(SdrplayProfile::frequencyAllowedForAntenna("RSPdx-R2", "Antenna C", 199.9e6));
    CHECK_FALSE(SdrplayProfile::frequencyAllowedForAntenna("RSPdx-R2", "Antenna C", 200.1e6));
    CHECK(SdrplayProfile::antennaFrequencyError("RSPdx-R2", "Antenna C", 438.5e6).find("BNC") != std::string::npos);

    const auto duoHz = SdrplayProfile::antennaCapabilities("RSPduo", "Tuner 1 Hi-Z");
    REQUIRE(duoHz.has_value());
    CHECK(duoHz->highImpedance);
    CHECK(duoHz->maxFrequencyHz == 30.0e6);
    CHECK(SdrplayProfile::frequencyAllowedForAntenna("RSPduo", "Hi-Z", 14.2e6));
    CHECK_FALSE(SdrplayProfile::frequencyAllowedForAntenna("RSPduo", "Hi-Z", 145.8e6));

    CHECK(SdrplayProfile::frequencyAllowedForAntenna("RSP1B", "RX", 1.0e3));
    CHECK(SdrplayProfile::frequencyAllowedForAntenna("RSP1B", "RX", 2.0e9));
    CHECK_FALSE(SdrplayProfile::frequencyAllowedForAntenna("unknown", "RX", 100.0e6));
}

TEST_CASE("SdrplayProfile antenna ports Bias-T and dual rate limits", "[sdrplay]") {
    CHECK_FALSE(SdrplayProfile::biasTAllowedForAntenna("RSPdx", "Antenna A"));
    CHECK(SdrplayProfile::biasTAllowedForAntenna("RSPdx", "Antenna B"));
    CHECK_FALSE(SdrplayProfile::biasTAllowedForAntenna("RSPdx", "Antenna C"));
    CHECK_FALSE(SdrplayProfile::biasTAllowedForAntenna("RSPduo", "Tuner 1 Hi-Z"));
    CHECK_FALSE(SdrplayProfile::biasTAllowedForAntenna("RSPduo", "Tuner 1 50 ohm"));
    CHECK(SdrplayProfile::biasTAllowedForAntenna("RSPduo", "Tuner 2 50 ohm"));
    CHECK_FALSE(SdrplayProfile::biasTAllowedForAntenna("RSP2", "Antenna A"));
    CHECK(SdrplayProfile::biasTAllowedForAntenna("RSP2", "Antenna B"));
    CHECK_FALSE(SdrplayProfile::biasTAllowedForAntenna("unknown", "mystery"));
    CHECK(SdrplayProfile::maxSampleRateHz("ST") == 10.0e6);
    CHECK(SdrplayProfile::maxSampleRateHz("DT") == 2.0e6);
    CHECK(SdrplayProfile::clampSampleRateHz("DT", 5e6) == 2.0e6);
    CHECK(SdrplayProfile::clampSampleRateHz("ST", 5e6) == 5.0e6);
    CHECK(SdrplayProfile::antennaPortDescription("RSPdx", "Antenna C").find("BNC") != std::string::npos);
    CHECK(SdrplayProfile::rfNotchUiLabel().find("MW/FM") != std::string::npos);
    CHECK(SdrplayProfile::rfNotchUiTooltip().find("rfNotchEnable") != std::string::npos);
    CHECK(SdrplayProfile::extRefUiLabel("RSPduo").find("OUT") != std::string::npos);
    CHECK(SdrplayProfile::extRefUiTooltip("RSPduo").find("extRefOutputEn") != std::string::npos);
}

TEST_CASE("SdrplayProfile distinguishes USB Hardware API and nRSP WebSocket", "[sdrplay]") {
    CHECK(SdrplayProfile::usesHardwareApi("RSP1B"));
    CHECK_FALSE(SdrplayProfile::usesWebsocketApi("RSP1B"));
    CHECK_FALSE(SdrplayProfile::usesHardwareApi("nRSP-ST"));
    CHECK(SdrplayProfile::usesWebsocketApi("nRSP-ST"));
    CHECK(SdrplayProfile::backendDescription("nRSP-ST").find("WebSocket") != std::string::npos);
}

TEST_CASE("SdrplayProfile covers installed Windows runtime layouts", "[sdrplay]") {
    const auto api = SdrplayProfile::windowsApiCandidates("C:\\SDR Town");
    const auto modules = SdrplayProfile::windowsSoapyModuleCandidates("C:\\SDR Town");
    CHECK(std::find(api.begin(), api.end(), "C:\\SDR Town\\sdrplay_api.dll") != api.end());
    CHECK(std::find(api.begin(), api.end(),
                    "C:\\Program Files\\SDRplay\\API\\x64\\sdrplay_api.dll") != api.end());
    CHECK(std::find(api.begin(), api.end(),
                    "C:\\Program Files\\SDRplay\\API\\x86\\sdrplay_api.dll") != api.end());
    CHECK(std::find(api.begin(), api.end(),
                    "C:\\Program Files\\PothosSDR\\bin\\sdrplay_api.dll") != api.end());
    CHECK(std::find(api.begin(), api.end(),
                    "C:\\Program Files (x86)\\PothosSDR\\bin\\sdrplay_api.dll") != api.end());
    CHECK(std::find(api.begin(), api.end(),
                    "C:\\ProgramData\\radioconda\\Library\\bin\\sdrplay_api.dll") != api.end());
    CHECK(std::find(modules.begin(), modules.end(),
                    "C:\\Program Files\\PothosSDR\\bin\\sdrPlaySupport.dll") != modules.end());
    CHECK(std::find(modules.begin(), modules.end(),
                    "C:\\Program Files\\PothosSDR\\lib\\SoapySDR\\modules0.8\\sdrPlaySupport.dll") != modules.end());
    CHECK(std::find(modules.begin(), modules.end(),
                    "C:\\Program Files (x86)\\PothosSDR\\lib\\SoapySDR\\modules0.8\\sdrPlaySupport.dll") != modules.end());
    CHECK(std::find(modules.begin(), modules.end(),
                    "C:\\Program Files\\Afreet\\SkyRoof\\lib\\SoapySDR\\modules0.8\\sdrPlaySupport.dll") != modules.end());
    CHECK(std::find(modules.begin(), modules.end(),
                    "C:\\Program Files (x86)\\Afreet\\SkyRoof\\lib\\SoapySDR\\modules0.8\\sdrPlaySupport.dll") != modules.end());
    CHECK(std::find(modules.begin(), modules.end(),
                    "C:\\ProgramData\\radioconda\\Library\\lib\\SoapySDR\\modules0.8\\sdrPlaySupport.dll") != modules.end());
    CHECK(std::find(modules.begin(), modules.end(),
                    "C:\\SDR Town\\lib\\SoapySDR\\modules0.8\\sdrPlaySupport.dll") != modules.end());
    CHECK(std::find(modules.begin(), modules.end(),
                    "C:\\SDR Town\\lib64\\SoapySDR\\modules0.8\\SoapySDRPlay3.dll") != modules.end());
    CHECK(std::find(api.begin(), api.end(),
                    "C:\\Program Files (x86)\\SDRplay\\API\\x64\\sdrplay_api.dll") != api.end());
}
