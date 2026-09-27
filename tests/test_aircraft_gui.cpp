#include "AircraftMapWidget.h"
#include "AdsBTrackStore.h"
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QLabel>
#include <QDir>
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
        for (auto* control : {static_cast<QWidget*>(check), static_cast<QWidget*>(bandwidth)}) {
            CHECK(control->isVisible());
            CHECK(map.rect().contains(QRect(control->mapTo(&map,QPoint{}),control->size())));
        }
        if (!directory.isEmpty()) CHECK(map.grab().save(directory + QString("/aircraft-%1.png").arg(size.width())));
    }
    map.hide();
}
