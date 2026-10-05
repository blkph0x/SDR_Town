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
#include "SatcomScannerEngine.h"
#include "SatPassPlanner.h"
#include "WorkflowSessionId.h"
#include "SatcomHubWidget.h"
#include "SatcomScannerWidget.h"
#include "ObserverMapWidget.h"
#include <QTabWidget>
#include <QTemporaryDir>

TEST_CASE("Aircraft sample rate default and applied telemetry are explicit", "[aircraft][gui]") {
    QSettings settings;settings.remove("aircraft/sessions/qa-rate-default");
    AircraftMapWidget widget(nullptr,"qa-rate-default");
    auto* rate=widget.findChild<QDoubleSpinBox*>("aircraftCaptureBandwidthMHz");REQUIRE(rate);
    CHECK(rate->value()==2.4);CHECK(rate->suffix()==" MS/s");
    CHECK(widget.webStatus().value("appliedSampleRateHz").toDouble()==0);
    rate->setValue(2.048);CHECK(rate->value()==2.048);
}

TEST_CASE("Named satellite controllers isolate settings planner and track state", "[aircraft][gui][ownership]") {
    CHECK_THROWS(normalizedWorkflowSessionId("../escape"));
    CHECK_THROWS(normalizedWorkflowSessionId("bad\n"));
    CHECK(normalizedWorkflowSessionId("Radio-A") == "radio-a");
    {
        SatcomScannerEngine a("qa-sat-a"), b("qa-sat-b");
        CHECK_THROWS(SatcomScannerEngine("QA-SAT-A"));
        auto ca = SatcomScannerConfig::defaults();
        ca.deviceStableKey = "missing-sat-a"; ca.lowHz = 145.8e6; ca.highHz = 145.8e6;
        ca.monitorAudio = false; a.setConfig(ca);
        ca.deviceStableKey = "missing-sat-b"; ca.lowHz = 137.1e6; ca.highHz = 137.1e6; b.setConfig(ca);
        a.planner().setAutoTrack(false); b.planner().setAutoTrack(true);
        CHECK_FALSE(a.planner().snapshot().armed.autoTrack);
        CHECK(b.planner().snapshot().armed.autoTrack);
        auto observer = a.planner().observer(); observer.latDeg = -31; a.planner().setObserver(observer);
        observer.latDeg = -40; b.planner().setObserver(observer);
        CHECK(a.planner().observer().latDeg == -31);
        CHECK(b.planner().observer().latDeg == -40);
        CHECK_FALSE(a.start()); CHECK_FALSE(b.start());
        const auto reason = a.snapshot().lastStatus;
        CHECK((reason.find("unavailable") != std::string::npos || reason.find("No SDR devices") != std::string::npos));
        CHECK(a.snapshot().activeDeviceIndex == size_t(-1));
        a.stop(); CHECK(b.config().deviceStableKey == "missing-sat-b");
    }
    SatcomScannerEngine reopened("QA-SAT-A");
    CHECK(reopened.config().deviceStableKey == "missing-sat-a");
    CHECK(reopened.planner().observer().latDeg == -31);
    CHECK_FALSE(reopened.autoCaptureEnabled());

    AircraftMapWidget a(nullptr,"qa-plane-a"), b(nullptr,"qa-plane-b");
    a.trackStore().setNetworkEnabled(false); b.trackStore().setNetworkEnabled(false);
    a.trackStore().ingestAdscPosition(0xaabb01,-33,151);
    b.trackStore().ingestAdscPosition(0xaabb02,-34,152);
    CHECK(a.trackStore().trackByIcao(0xaabb02).icao == 0);
    CHECK(b.trackStore().trackByIcao(0xaabb01).icao == 0);
    REQUIRE(a.webControl({{"action","configure"},{"deviceKey","missing-aircraft-a"},{"captureBandwidthMHz",4}}).value("ok").toBool());
    REQUIRE(b.webControl({{"action","configure"},{"deviceKey","missing-aircraft-b"},{"captureBandwidthMHz",8}}).value("ok").toBool());
    CHECK(a.webStatus().value("deviceKey").toString() == "missing-aircraft-a");
    CHECK(b.webStatus().value("deviceKey").toString() == "missing-aircraft-b");
    CHECK_FALSE(a.webControl({{"action","configure"},{"deviceKey","must-not-apply"},{"captureBandwidthMHz","invalid"}}).value("ok").toBool());
    CHECK(a.webStatus().value("deviceKey").toString() == "missing-aircraft-a");
    CHECK_FALSE(a.webControl({{"action","tune"}}).value("ok").toBool());
    CHECK_FALSE(b.webStatus().value("radioBusy").toBool());
    a.hide(); CHECK(b.trackStore().trackByIcao(0xaabb02).positionValid);
}

TEST_CASE("Satellite workspace routes and restores the addressed named controller", "[aircraft][gui][ownership]") {
    const auto previous = qApp->property("sdrtown.guiDryRun");
    struct Restore { QVariant previous; ~Restore() { qApp->setProperty("sdrtown.guiDryRun",previous); } } restore{previous};
    qApp->setProperty("sdrtown.guiDryRun",true);
    SatcomHubWidget hub;
    auto* a = qobject_cast<SatcomScannerWidget*>(hub.openReceiverSession("satcom","visual-a"));
    auto* b = qobject_cast<SatcomScannerWidget*>(hub.openReceiverSession("satcom","visual-b"));
    REQUIRE(a); REQUIRE(b);
    CHECK(a != b);
    CHECK(hub.openReceiverSession("satcom","VISUAL-A") == a);
    const auto result = hub.controlReceiverSessions("satcom","POST",{{"sessionId","visual-a"},{"action","configure"},
        {"config",QJsonObject{{"lowHz",145800000},{"highHz",145800000},{"deviceStableKey","missing-visual-radio"},{"monitorAudio",false}}}});
    REQUIRE(result.value("ok").toBool());
    CHECK(a->engine().config().lowHz == 145800000);
    a->engine().setAutoCaptureEnabled(true);
    REQUIRE(hub.controlReceiverSessions("satcom","POST",{{"sessionId","visual-a"},{"action","stop"}}).value("ok").toBool());
    CHECK_FALSE(a->engine().autoCaptureEnabled());
    CHECK(b->engine().config().deviceStableKey != "missing-visual-radio");
    double beforeLat, beforeLon, beforeRadius;
    AdsBTrackStore::instance().observer(&beforeLat,&beforeLon,&beforeRadius);
    auto* observerMap = a->findChild<ObserverMapWidget*>(); REQUIRE(observerMap);
    observerMap->locationPicked(-32.25,150.5);
    CHECK(a->engine().planner().observer().latDeg == -32.25);
    double afterLat, afterLon, afterRadius;
    AdsBTrackStore::instance().observer(&afterLat,&afterLon,&afterRadius);
    CHECK(afterLat == beforeLat); CHECK(afterLon == beforeLon); CHECK(afterRadius == beforeRadius);
    const auto directory = qEnvironmentVariable("SDR_TOWN_TEST_VISUAL_DIR");
    hub.resize(1280,1200); hub.show(); QApplication::processEvents();
    auto* combo = a->findChild<QComboBox*>("satcomDevice"); REQUIRE(combo);
    CHECK(combo->isVisible());
    CHECK(combo->currentData().toString() == "missing-visual-radio");
    if (!directory.isEmpty()) CHECK(hub.grab().save(directory + "/satcom-sessions.png"));
    REQUIRE(hub.controlReceiverSessions("satcom","POST",{{"sessionId","visual-a"},{"action","close"}}).value("ok").toBool());
    CHECK(b->engine().config().deviceStableKey != "missing-visual-radio");
    CHECK(hub.controlReceiverSessions("satcom","POST",{{"sessionId","visual-a"},{"action","stop"}}).value("status").toInt() == 409);
    hub.hide();
}

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
    CHECK(bandwidth->value() == 2.4);
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
