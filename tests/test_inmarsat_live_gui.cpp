#include "InmarsatWidget.h"
#include "InmarsatEngine.h"
#include "InmarsatDiagnostics.h"
#include "DeviceManager.h"
#include "SatcomHostServices.h"
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QTableWidget>
#include <QSpinBox>
#include <QMouseEvent>
#include "InmarsatWatchSpectrum.h"
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <stdexcept>

#ifdef HAVE_SOAPYSDR
#error The Inmarsat GUI test must not enable hardware access
#endif

// Deliberately omit the network adapter in this UI test executable. The real
// widget, engine, device manager and decoder implementations are linked below.
void connectInmarsatRemoteDiagnostics() {}
void configureInmarsatReplaySharing(InmarsatReplayDialog&) {}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir settings;
    if (!settings.isValid()) return 2;
    QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName("SDR_Town_Tests");
    app.setApplicationName("inmarsat-live-gui-" + QFileInfo(settings.path()).fileName());
    const auto path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QFileInfo sandbox(path);
    if (!sandbox.isAbsolute() || sandbox.fileName() != app.applicationName() ||
        sandbox.dir().dirName() != app.organizationName()) return 3;
    const int result = Catch::Session().run(argc, argv);
    InmarsatEngine::instance().stop();
    QDir(path).removeRecursively(); // Only this generated, verified test directory.
    return result;
}

TEST_CASE("Opening Inmarsat preserves saved voice and burst selection", "[inmarsat][gui]") {
    auto& engine = InmarsatEngine::instance();
    REQUIRE(DeviceManager::instance().getDevices().empty()); // Never enumerate hardware.
    for (const int rate : {8400, -1200, -10500, 0}) {
        auto cfg = InmarsatEngineConfig::defaults();
        cfg.channelHz = 1546123456.0;
        cfg.mode = rate == 0 ? "egc" : rate < 0 ? "aero_burst" : "aero_voice";
        cfg.baud = rate == 0 ? 1200 : std::abs(rate);
        engine.setConfig(cfg);
        InmarsatWidget widget;
        CHECK(engine.config().toJson() == cfg.toJson());
        REQUIRE(widget.findChild<QComboBox*>("inmarsatDecoder"));
        CHECK(widget.findChild<QComboBox*>("inmarsatDecoder")->currentData().toInt() == rate);
        CHECK(widget.findChild<QDoubleSpinBox*>("inmarsatFrequencyMHz")->value() == cfg.channelHz/1e6);
    }
}

TEST_CASE("Inmarsat Start applies visible voice frequency without needing Tune", "[inmarsat][gui]") {
    auto& engine = InmarsatEngine::instance();
    engine.setConfig(InmarsatEngineConfig::defaults());
    InmarsatWidget widget;
    auto* frequency = widget.findChild<QDoubleSpinBox*>("inmarsatFrequencyMHz");
    auto* decoder = widget.findChild<QComboBox*>("inmarsatDecoder");
    frequency->setValue(1546.125);
    decoder->setCurrentIndex(decoder->findData(8400));
    REQUIRE(DeviceManager::instance().getDevices().empty());
    // The real Start handler configures the channel then reports no hardware.
    bool sawNoDevice = false;
    QTimer dismiss;
    QObject::connect(&dismiss, &QTimer::timeout, [&] {
        if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            sawNoDevice = message->text().contains("No SDR devices");
            message->accept();
        }
    });
    dismiss.start(10);
    widget.findChild<QPushButton*>("inmarsatStart")->click();
    REQUIRE(sawNoDevice);
    CHECK(engine.config().channelHz == 1546125000.0);
    CHECK(engine.config().mode == "aero_voice");
    CHECK(engine.config().baud == 8400);
    CHECK(engine.snapshot().state == InmarsatEngineState::Idle);
}

TEST_CASE("Inmarsat explicit plan and channel changes keep controls consistent", "[inmarsat][gui]") {
    auto& engine = InmarsatEngine::instance();
    engine.setConfig(InmarsatEngineConfig::defaults());
    InmarsatWidget widget;
    auto* plans = widget.findChild<QComboBox*>("inmarsatBandPlan");
    REQUIRE(plans->count() > 1);
    plans->setCurrentIndex((plans->currentIndex()+1)%plans->count());
    auto* frequency = widget.findChild<QDoubleSpinBox*>("inmarsatFrequencyMHz");
    auto* decoder = widget.findChild<QComboBox*>("inmarsatDecoder");
    CHECK(frequency->value() == engine.config().channelHz/1e6);
    CHECK(decoder->currentData().toInt() == engine.config().baud);
    REQUIRE(QMetaObject::invokeMethod(&widget, "onChannelActivated", Q_ARG(int,0), Q_ARG(int,0)));
    CHECK(frequency->value() == engine.config().channelHz/1e6);
    frequency->setValue(1545.5);
    decoder->setCurrentIndex(decoder->findData(8400));
    widget.findChild<QPushButton*>("inmarsatTune")->click();
    CHECK(engine.config().channelHz == 1545500000.0);
    CHECK(engine.config().baud == 8400);
    frequency->setValue(1545.75);
    REQUIRE(QMetaObject::invokeMethod(&widget, "refreshUi"));
    CHECK(frequency->value() == 1545.75); // Refresh must not overwrite an edit.
    CHECK(widget.findChild<QLabel*>("inmarsatAudioStatus")->text().contains("aero_voice / 8400"));
    if (const auto path = qEnvironmentVariable("SDR_TOWN_INMARSAT_LIVE_SCREENSHOT"); !path.isEmpty()) {
        widget.resize(1100,800);
        widget.show();
        QApplication::processEvents();
        REQUIRE(widget.grab().save(path));
    }
}

TEST_CASE("Inmarsat takeover refuses safely and restores on hardware startup failure", "[.inmarsat-host]") {
    auto& manager = DeviceManager::instance();
    const auto devices = manager.enumerateDevices(false, false);
    REQUIRE_FALSE(devices.empty()); // This executable compiles the stub-only backend.
    auto* device = manager.getDevice(0);
    REQUIRE(device);
    device->label = "Inmarsat isolated test receiver";
    device->enabled = false;
    const double originalCenter = manager.getCurrentCenterFreq(0);
    auto& engine = InmarsatEngine::instance();
    auto cfg = InmarsatEngineConfig::defaults();
    cfg.deviceIndex = 0;
    cfg.deviceStableKey = device->stableKey;
    engine.setConfig(cfg);
    auto& host = SatcomHostServices::instance();
    int begins = 0, ends = 0;
    bool parked = false;
    struct Cleanup {
        ~Cleanup() { InmarsatEngine::instance().stop(); SatcomHostServices::instance().clear(); }
    } cleanup;
    SatcomHostCallbacks callbacks;
    callbacks.beginReceiverTakeover = [&](size_t index, std::string* error) {
        ++begins;
        CHECK(index == 0);
        CHECK_FALSE(manager.isStreaming(0)); // Parking precedes any stream start.
        *error = "Test host refused takeover";
        return false;
    };
    callbacks.endReceiverTakeover = [&] {
        ++ends;
        CHECK_FALSE(manager.isStreaming(0)); // Restore hardware before Listen.
        parked = false;
    };
    host.install(callbacks);
    CHECK_FALSE(engine.start(true));
    CHECK(begins == 1); CHECK(ends == 0); CHECK_FALSE(parked);
    CHECK(engine.snapshot().lastStatus == "Test host refused takeover");
    CHECK(manager.deviceLeaseOwner() == DeviceManager::DeviceLeaseOwner::None);
    CHECK(manager.getCurrentCenterFreq(0) == originalCenter);

    callbacks.beginReceiverTakeover = [&](size_t, std::string*) {
        ++begins; parked = true; return true;
    };
    host.install(callbacks);
    // A simulated stream is never accepted as live hardware. It must unwind
    // both ownership layers, not leave Listen parked or keep a tuner lease.
    CHECK_FALSE(engine.start(true));
    CHECK(begins == 2); CHECK(ends == 1); CHECK_FALSE(parked);
    CHECK_FALSE(manager.getDevice(0)->enabled);
    CHECK(manager.deviceLeaseOwner() == DeviceManager::DeviceLeaseOwner::None);
    CHECK(engine.snapshot().state == InmarsatEngineState::Idle);
    engine.stop(); engine.stop();
    CHECK(ends == 1); // No double end or release of an unowned host session.
}

TEST_CASE("Inmarsat watch signal selection saves channels and timing across reopen", "[inmarsat][gui]") {
    auto& engine=InmarsatEngine::instance();
    REQUIRE(engine.setConfig(InmarsatEngineConfig::defaults()));
    {
        InmarsatWidget widget;
        auto* spectrum=widget.findChild<InmarsatWatchSpectrum*>("inmarsatWatchSpectrum");REQUIRE(spectrum);
        spectrum->frequencySelected(1546125000);
        auto* decoder=widget.findChild<QComboBox*>("inmarsatDecoder");decoder->setCurrentIndex(decoder->findData(8400));
        widget.findChild<QLineEdit*>("inmarsatWatchName")->setText("Tester voice");
        widget.findChild<QPushButton*>("inmarsatWatchAdd")->click();
        REQUIRE(engine.config().watch.channels.size()==1);
        REQUIRE(engine.config().watch.channels[0].frequencyHz==1546125000);
        spectrum->frequencySelected(1542125000);decoder->setCurrentIndex(decoder->findData(10500));
        widget.findChild<QPushButton*>("inmarsatWatchAdd")->click();
        widget.findChild<QCheckBox*>("inmarsatAutoWatch")->setChecked(true);
        widget.findChild<QSpinBox*>("inmarsatDataDwell")->setValue(45);
        widget.findChild<QPushButton*>("inmarsatWatchSave")->click();
        REQUIRE(engine.config().watch.dataDwellSeconds==45);
        REQUIRE(engine.snapshot().state==InmarsatEngineState::Idle);
    }
    InmarsatEngineConfig disk;disk.load();REQUIRE(disk.toJson()==engine.config().toJson());
    InmarsatWidget reopened;
    auto* table=reopened.findChild<QTableWidget*>("inmarsatWatchTable");REQUIRE(table->rowCount()==2);
    REQUIRE(reopened.findChild<QCheckBox*>("inmarsatAutoWatch")->isChecked());
    table->item(0,0)->setCheckState(Qt::Unchecked);REQUIRE_FALSE(engine.config().watch.channels[0].enabled);
    table->selectRow(1);reopened.findChild<QPushButton*>("inmarsatWatchRemove")->click();
    REQUIRE(engine.config().watch.channels.size()==1);
    REQUIRE_FALSE(engine.start());REQUIRE(engine.snapshot().lastStatus.find("Enable at least one")!=std::string::npos);
    if(const auto path=qEnvironmentVariable("SDR_TOWN_INMARSAT_WATCH_SCREENSHOT");!path.isEmpty()) {
        reopened.resize(1050,980);reopened.show();QApplication::processEvents();
        REQUIRE(reopened.grab().save(path));
    }
}

TEST_CASE("Inmarsat asks before leaving P25 and rechecks after confirmation", "[.inmarsat-host]") {
    auto& manager = DeviceManager::instance();
    REQUIRE_FALSE(manager.enumerateDevices(false, false).empty());
    manager.getDevice(0)->label = "Inmarsat confirmation test receiver";
    auto& engine = InmarsatEngine::instance();
    auto cfg = InmarsatEngineConfig::defaults();
    cfg.deviceIndex = 0;
    cfg.deviceStableKey = manager.getDevice(0)->stableKey;
    REQUIRE(engine.setConfig(cfg));
    auto& host = SatcomHostServices::instance();
    struct Cleanup {
        ~Cleanup() { InmarsatEngine::instance().stop(); SatcomHostServices::instance().clear(); }
    } cleanup;
    bool p25Configured = true;
    bool refuseConfirmed = false;
    int probes = 0, confirmations = 0, starts = 0, restores = 0;
    SatcomHostCallbacks callbacks;
    callbacks.prepareInmarsatTakeover = [&](size_t index, bool confirmed) -> InmarsatTakeoverResult {
        CHECK(index == 0);
        if (!confirmed) { ++probes; return {!p25Configured, p25Configured, {}}; }
        ++confirmations;
        if (refuseConfirmed) return {false, false, "Test decoder is busy"};
        p25Configured = false;
        return {true, false, {}};
    };
    callbacks.beginReceiverTakeover = [&](size_t index, std::string*) {
        CHECK(index == 0);
        ++starts;
        CHECK_FALSE(p25Configured);
        return true;
    };
    callbacks.endReceiverTakeover = [&] { ++restores; CHECK_FALSE(p25Configured); };
    host.install(callbacks);
    InmarsatWidget widget;
    bool answerYes = false;
    int questions = 0, warnings = 0;
    QTimer responder;
    QObject::connect(&responder, &QTimer::timeout, [&] {
        auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!message) return;
        if (message->icon() == QMessageBox::Question) {
            ++questions;
            CHECK(message->text().contains("P25 will stay stopped"));
            CHECK(message->defaultButton() == message->button(QMessageBox::No));
            message->button(answerYes ? QMessageBox::Yes : QMessageBox::No)->click();
        } else { ++warnings; message->accept(); }
    });
    responder.start(10);
    auto* start = widget.findChild<QPushButton*>("inmarsatStart"); REQUIRE(start);
    start->click(); // Cancel leaves P25 and hardware untouched.
    CHECK(probes == 1); CHECK(questions == 1); CHECK(confirmations == 0);
    CHECK(p25Configured); CHECK(starts == 0); CHECK(restores == 0); CHECK(warnings == 0);
    CHECK_FALSE(manager.isStreaming(0));

    answerYes = true; refuseConfirmed = true;
    start->click(); // State may change while the modal is open.
    CHECK(probes == 2); CHECK(questions == 2); CHECK(confirmations == 1);
    CHECK(p25Configured); CHECK(starts == 0); CHECK(warnings == 1);

    refuseConfirmed = false;
    start->click(); // Stub hardware fails after handover; never resurrect P25.
    CHECK(probes == 3); CHECK(questions == 3); CHECK(confirmations == 2);
    CHECK_FALSE(p25Configured); CHECK(starts == 1); CHECK(restores == 1);
    CHECK(warnings == 2); CHECK_FALSE(manager.isStreaming(0));
    CHECK(manager.deviceLeaseOwner() == DeviceManager::DeviceLeaseOwner::None);
    CHECK(engine.snapshot().state == InmarsatEngineState::Idle);

    cfg.watch.enabled = true;
    REQUIRE(engine.setConfig(cfg));
    CHECK_FALSE(engine.prepareTakeover(true).ready); // Invalid list cannot stop P25.
    CHECK(confirmations == 2);
}

TEST_CASE("Inmarsat watch spectrum maps clicks to actual RF coordinates", "[inmarsat][gui]") {
    InmarsatWatchSpectrum spectrum;spectrum.resize(800,200);
    double selected=0;
    QObject::connect(&spectrum,&InmarsatWatchSpectrum::frequencySelected,[&](double hz){selected=hz;});
    auto click=[&](double x,double y) {
        QMouseEvent e(QEvent::MouseButtonRelease,QPointF(x,y),QPointF(x,y),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&spectrum,&e);
    };
    click(400,80);REQUIRE(selected==0); // Empty view never supplies fake RF.
    spectrum.setSpectrum(std::vector<float>(1024,-110),1542e6,1e6,{});
    click(400,80);REQUIRE(selected==1542e6);
    click(204,150);REQUIRE(selected==1541750000); // Same axis in waterfall.
    spectrum.setSpectrum(std::vector<float>(1024,-110),1543e6,1e6,{});
    click(400,80);REQUIRE(selected==1543e6); // Retune invalidates the old axis.
}
