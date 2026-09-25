#include "SdrplayControlsWidget.h"
#include "DeviceManager.h"
#include <catch2/catch_test_macros.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLabel>
#include <QApplication>

TEST_CASE("RSPdx widget follows verified capabilities and port power safety", "[sdrplay][gui]") {
    SdrplayControlsWidget widget;
    widget.resize(650, 700); widget.show(); QApplication::processEvents();
    DeviceInfo d; d.isSdrplay = true; d.sdrplayModel = "RSPdx";
    auto* antenna = widget.findChild<QComboBox*>("sdrplayAntenna");
    auto* bias = widget.findChild<QCheckBox*>("biasT_ctrl");
    auto* agc = widget.findChild<QCheckBox*>("sdrplayAgc");
    auto* ifgr = widget.findChild<QDoubleSpinBox*>("sdrplayIfgr");
    auto* rfgr = widget.findChild<QDoubleSpinBox*>("sdrplayRfgr");
    auto* bw = widget.findChild<QComboBox*>("sdrplayBandwidth");
    auto* setpoint = widget.findChild<QSpinBox*>("sdrplayAgcSetpoint");
    REQUIRE(antenna); REQUIRE(bias); REQUIRE(agc); REQUIRE(ifgr); REQUIRE(rfgr); REQUIRE(bw); REQUIRE(setpoint);
    widget.setDevice(&d);
    CHECK_FALSE(antenna->isEnabled()); CHECK_FALSE(bias->isEnabled()); CHECK_FALSE(agc->isEnabled());
    d.sdrplayProbed = d.sdrplayHasAgc = true;
    d.antennas = {"Antenna A", "Antenna B", "Antenna C"}; d.antenna = "Antenna A";
    d.gainElements = {"IFGR", "RFGR"}; d.gainMin = 0; d.gainMax = 27;
    d.bandwidthsHz = {200000, 300000, 1536000};
    d.sdrplaySettingKeys = {"biasT_ctrl", "rfnotch_ctrl", "dabnotch_ctrl", "hdr_ctrl", "iqcorr_ctrl", "agc_setpoint"};
    int writes = 0; bool reject = false;
    widget.apply = [&](const auto& c, std::string& error) {
        ++writes;
        try {
            if (reject) throw std::runtime_error("test driver rejected write");
            SdrplayControl::prepare(d, c); return true;
        } catch (const std::exception& e) { error = e.what(); return false; }
    };
    widget.changed = [&]() { widget.setDevice(&d, "live hardware"); };
    widget.setDevice(&d, "live hardware");
    CHECK(writes == 0); CHECK(antenna->count() == 3); CHECK(antenna->isEnabled());
    CHECK_FALSE(bias->isEnabled());
    CHECK_FALSE(widget.findChild<QCheckBox*>("extref_ctrl")->isEnabled());
    CHECK_FALSE(widget.findChild<QComboBox*>("sdrplayRfSelect")->isEnabled());
    antenna->setCurrentIndex(1); CHECK(d.antenna == "Antenna B"); CHECK(bias->isEnabled());
    bias->click(); CHECK(d.soapySettings.at("biasT_ctrl") == "true");
    antenna->setCurrentIndex(2); CHECK_FALSE(bias->isEnabled()); CHECK_FALSE(bias->isChecked());
    for (const auto* key : {"rfnotch_ctrl", "dabnotch_ctrl", "hdr_ctrl", "iqcorr_ctrl"}) {
        auto* box = widget.findChild<QCheckBox*>(key); REQUIRE(box); CHECK(box->isEnabled());
        box->click(); CHECK(d.soapySettings.at(key) == "true");
        box->click(); CHECK(d.soapySettings.at(key) == "false");
    }
    agc->click(); CHECK(d.agcEnabled); CHECK_FALSE(ifgr->isEnabled()); CHECK(setpoint->isEnabled());
    rfgr->setValue(12); CHECK(d.rfgrDb == 12); CHECK(d.agcEnabled);
    setpoint->setValue(-40); CHECK(d.soapySettings.at("agc_setpoint") == "-40");
    agc->click(); ifgr->setValue(50); CHECK(d.ifgrDb == 50);
    bw->setCurrentIndex(2); CHECK(d.bandwidthHz == 300000);
    bw->setCurrentIndex(0); CHECK(d.bandwidthHz == 0);
    reject = true;
    rfgr->setFocus(); rfgr->setValue(14); CHECK(rfgr->value() == 12);
    antenna->setCurrentIndex(1); CHECK(antenna->currentData().toString() == "Antenna C");
    CHECK(widget.findChild<QLabel*>("sdrplayControlStatus")->text().contains("rejected"));
    const int before = writes;
    for (int i = 0; i < 100; ++i) widget.setDevice(&d);
    CHECK(writes == before);
    if (qEnvironmentVariableIsSet("SDR_TOWN_SDRPLAY_GUI_SCREENSHOT")) {
        widget.grab().save(qEnvironmentVariable("SDR_TOWN_SDRPLAY_GUI_SCREENSHOT"));
    }
}
