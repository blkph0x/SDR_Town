#include "frontend/DvbSurvey.h"
#include "frontend/EquipmentWizard.h"
#include "frontend/FrontEndMetrics.h"
#include "frontend/FrontEndPower.h"
#include "frontend/LinkBudgetHint.h"
#include "frontend/LnbConversion.h"
#include "frontend/PassArming.h"
#include "frontend/RotatorTrack.h"
#include "frontend/StationProfile.h"

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <cmath>
#include <complex>
#include <vector>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}

TEST_CASE("LNB noise temperature matches the published 0.3 dB example") {
    const double kelvin = noiseTempFromNoiseFigureDb(0.3);
    CHECK(kelvin > 20.0);
    CHECK(kelvin < 22.0);
    CHECK(claimedNoiseFigureNeedsCaution(0.1));
    CHECK_FALSE(claimedNoiseFigureNeedsCaution(0.3));
}

TEST_CASE("Universal Ku low and high band IF stay inside 950-2150 MHz") {
    LnbProfile lnb;
    const auto low = lnbTune(lnb, 11.7e9, false, false);
    REQUIRE(low.ok);
    CHECK(low.voltageV == 13);
    CHECK_FALSE(low.tone22kHz);
    CHECK(std::abs(low.ifHz - 1.95e9) < 1.0);
    const auto high = lnbTune(lnb, 12.2e9, true, true);
    REQUIRE(high.ok);
    CHECK(high.voltageV == 18);
    CHECK(high.tone22kHz);
    CHECK(std::abs(high.ifHz - 1.6e9) < 1.0);
    CHECK_FALSE(lnbTune(lnb, 1.0e9, false, false).ok);
}

TEST_CASE("Bias-T stays off without confirmation and refuses a second source") {
    FrontEndPower power;
    std::string error;
    CHECK_FALSE(power.enable(false, 18, true, &error));
    CHECK_FALSE(power.enabled());
    CHECK_FALSE(power.enable(true, 18, true, &error));
    REQUIRE(power.selectBackend(BiasBackend::External, &error));
    REQUIRE(power.enable(true, 18, true, &error));
    CHECK(power.enabled());
    CHECK_FALSE(power.selectBackend(BiasBackend::SdrInternal, &error));
    power.noteMeasurement(18.0, 600.0, 500.0);
    CHECK_FALSE(power.enabled());
    REQUIRE(power.audit().size() >= 2);
    CHECK(power.audit().back().reason == "over-current");
}

TEST_CASE("Tracking tick clamps, pauses for jog, and parks by teardown order") {
    TrackLimits limits;
    const auto paused = planTrackTick(true, true, true, true, limits, 20, 30, 0, 0);
    CHECK_FALSE(paused.send);
    const auto clamped = planTrackTick(true, true, true, false, limits, 400, 30, 1.0, 10);
    REQUIRE(clamped.send);
    CHECK(clamped.az == 360.0);
    CHECK(clamped.clamped);
    const auto teardown = planPassTeardown();
    REQUIRE(teardown.size() == 5);
    CHECK(teardown.front() == "stop-worker");
    CHECK(teardown[1] == "rotator-stop");
    CHECK(teardown[2] == "rotator-park");
    CHECK(teardown[3] == "power-off");
}

TEST_CASE("Arm checklist fail-closes and a complete checklist is ordered") {
    PassChecklist check;
    const auto rejected = planPassArm(check, StationMission::LeoTrack);
    CHECK_FALSE(rejected.accepted);
    check.leaseHeld = true;
    check.ifInSdrSpan = true;
    check.tleFreshOrNotRequired = true;
    check.biasCurrentOk = biasRatingCoversLnb(500, 200);
    check.rotatorReadyOrOverride = true;
    check.powerConfirmed = true;
    check.diseqcConfigured = true;
    const auto arm = planPassArm(check, StationMission::LeoTrack);
    REQUIRE(arm.accepted);
    CHECK(arm.steps.front() == "diseqc");
    CHECK(arm.steps.back() == "start-worker");
    CHECK(linkMarginHint(100, 0.3, 5, 10) == LinkMarginHint::Masked);
    CHECK(linkMarginHint(100, 0.3, 40, 10) == LinkMarginHint::Good);
}

TEST_CASE("Metrics indexes are monotonic and the profile round-trips") {
    FrontEndMetrics metrics;
    metrics.add("lnb.nfDb", 0.5, "claimed");
    metrics.addText("bias", "off");
    CHECK(metrics.records()[0].sampleIndex == 1);
    CHECK(metrics.records()[1].sampleIndex == 2);
    CHECK(metrics.toJsonl().find("\"name\":\"lnb.nfDb\"") != std::string::npos);
    StationProfile profile;
    profile.dishCm = 90;
    profile.lnb.noiseFigureDb = 0.1;
    profile.mission = StationMission::GeoPark;
    StationProfile loaded;
    std::string error;
    REQUIRE(stationProfileFromJson(stationProfileToJson(profile), &loaded, &error));
    CHECK(loaded.dishCm == 90);
    CHECK(loaded.mission == StationMission::GeoPark);
    CHECK(claimedNoiseFigureNeedsCaution(loaded.lnb.noiseFigureDb));
}

TEST_CASE("D1 survey reports a tone and refuses demod and decrypt") {
    constexpr int n = 256;
    std::vector<std::complex<float>> iq(n);
    for (int i = 0; i < n; ++i)
        iq[static_cast<size_t>(i)] = std::polar(1.0f, static_cast<float>(2.0 * 3.14159265358979323846 * 8.0 * i / n));
    const auto survey = surveyIfCapture(iq.data(), iq.size(), 2.4e6);
    CHECK(survey.powerMeasured);
    CHECK(survey.peakMeasured);
    CHECK(survey.peakOffsetHz > 0.0);
    CHECK_FALSE(survey.symbolRateMeasured);
    CHECK_FALSE(survey.plsDetected);
    CHECK_FALSE(dvbDemodAvailable());
    CHECK_FALSE(commercialDecryptAvailable());
}

TEST_CASE("Equipment wizard defaults Bias-T off and flags a sub-0.2 dB claim") {
    EquipmentWizard wizard;
    auto* confirm = wizard.findChild<QCheckBox*>("biasConfirm");
    auto* state = wizard.findChild<QLabel*>("biasState");
    REQUIRE(confirm);
    REQUIRE(state);
    CHECK_FALSE(confirm->isChecked());
    CHECK(state->text().contains("OFF"));
    wizard.findChild<QDoubleSpinBox*>("lnbNf")->setValue(0.1);
    CHECK(wizard.findChild<QLabel*>("nfCaution")->text().contains("0.2"));
}
