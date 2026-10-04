#include "AircraftMapWidget.h"
#include "ReceiverSourcePicker.h"
#include "AdsBTrackStore.h"
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QLabel>
#include <QDir>
#include <QComboBox>
#include <QPushButton>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Aircraft internet control removes sources and persists across windows", "[aircraft][gui]") {
    auto& store = AdsBTrackStore::instance();
    struct Restore {
        QSettings settings{"SDR_Town", "SDR Town"};
        QVariant saved = settings.value("aircraft/networkEnabled");
        bool enabled = AdsBTrackStore::instance().networkEnabled();
        ~Restore() {
            AdsBTrackStore::instance().setNetworkEnabled(enabled);
            if (saved.isValid()) settings.setValue("aircraft/networkEnabled", saved);
            else settings.remove("aircraft/networkEnabled");
        }
    } restore;
    store.setNetworkEnabled(true);
    AircraftMapWidget map;
    auto* check = map.findChild<QCheckBox*>("aircraftInternetEnabled"); REQUIRE(check);
    auto* bandwidth = map.findChild<QDoubleSpinBox*>("aircraftCaptureBandwidthMHz"); REQUIRE(bandwidth);
    CHECK(bandwidth->value() == 20);
    CHECK(bandwidth->maximum() == 20);
    const auto generation = store.networkGeneration();
    REQUIRE(store.mergeNetworkJson(R"({"states":[["a0b022","TEST",null,null,null,151,-34,1000,false,100,90,0]]})", generation));
    REQUIRE(store.trackByIcao(0xa0b022).positionValid);
    check->click();
    CHECK_FALSE(store.networkEnabled());
    CHECK(store.trackByIcao(0xa0b022).icao == 0);
    CHECK_FALSE(store.mergeNetworkJson(R"({"states":[]})", generation));
    AircraftMapWidget reopened;
    CHECK_FALSE(reopened.findChild<QCheckBox*>("aircraftInternetEnabled")->isChecked());
    // Render at compact and desktop sizes, with internet aircraft disabled.
    const auto directory = qEnvironmentVariable("SDR_TOWN_TEST_VISUAL_DIR");
    for (const auto& size : {QSize(560,360), QSize(1100,650)}) {
        map.resize(size); map.show(); QApplication::processEvents();
        for (auto* control : {static_cast<QWidget*>(check), static_cast<QWidget*>(bandwidth),
             static_cast<QWidget*>(map.findChild<QComboBox*>("aircraftDevice")),
             static_cast<QWidget*>(map.findChild<QPushButton*>("aircraftStop"))}) {
            REQUIRE(control);
            CHECK(control->isVisible());
            CHECK(map.rect().contains(QRect(control->mapTo(&map,QPoint{}),control->size())));
        }
        if (!directory.isEmpty()) CHECK(map.grab().save(directory + QString("/aircraft-%1.png").arg(size.width())));
    }
    map.hide();
}

TEST_CASE("Decoder receiver picker follows identity not receiver vector position", "[aircraft][gui][ownership]") {
    auto a=std::make_shared<Receiver>(), b=std::make_shared<Receiver>();
    a->deviceIndex=0; a->freqHz=98.1e6;
    b->deviceIndex=3; b->freqHz=145.8e6;
    std::vector<std::shared_ptr<Receiver>> receivers{a,b};
    ReceiverSourcePicker picker([&]{return receivers;});
    REQUIRE(picker.count()==2);
    picker.setCurrentIndex(1); REQUIRE(picker.selected()==b);
    const auto id=picker.currentData();
    std::swap(receivers[0],receivers[1]); picker.refresh();
    CHECK(picker.currentData()==id); CHECK(picker.selected()==b);
    receivers.erase(receivers.begin()); picker.refresh();
    CHECK_FALSE(picker.selected()); CHECK(picker.currentText().contains("unavailable"));
    auto replacement=std::make_shared<Receiver>(); receivers.insert(receivers.begin(),replacement);
    picker.refresh(); CHECK_FALSE(picker.selected()); CHECK(picker.currentData()==id);
    picker.setCurrentIndex(0); CHECK(picker.selected()==replacement);
}

TEST_CASE("Aircraft keeps a missing radio visible and refuses web fallback", "[aircraft][gui][ownership]") {
    QSettings settings;
    const auto saved = settings.value("aircraft/deviceKey");
    struct Restore { QVariant saved; ~Restore() {
        QSettings s; if(saved.isValid()) s.setValue("aircraft/deviceKey",saved); else s.remove("aircraft/deviceKey");
    }} restore{saved};
    settings.setValue("aircraft/deviceKey","missing-aircraft-radio");
    AircraftMapWidget widget;
    auto* combo=widget.findChild<QComboBox*>("aircraftDevice"); REQUIRE(combo);
    CHECK(combo->currentData().toString()=="missing-aircraft-radio");
    CHECK(combo->currentText().contains("Unavailable"));
    const auto result=widget.webControl({{"action","tune"}});
    CHECK_FALSE(result.value("ok").toBool());
    CHECK(result.value("error").toString().contains("unavailable"));
    CHECK_FALSE(widget.webStatus().value("radioBusy").toBool());
    CHECK_FALSE(widget.findChild<QPushButton*>("aircraftStop")->isEnabled());
    AircraftMapWidget reopened;
    CHECK(reopened.findChild<QComboBox*>("aircraftDevice")->currentData()==combo->currentData());
}
