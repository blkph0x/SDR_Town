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
#include <QElapsedTimer>
#include <iostream>
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

TEST_CASE("Aero presets retain exact surveyed frequency and rate", "[inmarsat][gui]") {
    const auto defaults = InmarsatEngineConfig::defaults();
    CHECK(defaults.channelHz == 1546005000.0);
    CHECK(defaults.baud == 10500);
    InmarsatWidget widget;
    auto* combo = widget.findChild<QComboBox*>("inmarsatBandPlan");
    auto* table = widget.findChild<QTableWidget*>("inmarsatPresetChannels");
    REQUIRE(combo); REQUIRE(table);
    REQUIRE(combo->findData("4f3") >= 0);
    combo->setCurrentIndex(combo->findData("4f3"));
    CHECK(table->horizontalHeaderItem(3)->text() == "bit/s");
    bool found = false;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 1)->text() != "1546.0625") continue;
        found = true;
        CHECK(table->item(row, 3)->text() == "10500");
        REQUIRE(QMetaObject::invokeMethod(table, "cellDoubleClicked", Qt::DirectConnection,
            Q_ARG(int, row), Q_ARG(int, 1)));
        CHECK(InmarsatEngine::instance().config().channelHz == 1546062500.0);
        CHECK(InmarsatEngine::instance().config().baud == 10500);
    }
    CHECK(found);
}

TEST_CASE("User rate selection previews a satellite preset without retuning", "[inmarsat][gui]") {
    auto& engine = InmarsatEngine::instance();
    REQUIRE(engine.setConfig(InmarsatEngineConfig::defaults()));
    InmarsatWidget widget;
    auto* plans = widget.findChild<QComboBox*>("inmarsatBandPlan");
    auto* decoder = widget.findChild<QComboBox*>("inmarsatDecoder");
    auto* frequency = widget.findChild<QDoubleSpinBox*>("inmarsatFrequencyMHz");
    auto* hint = widget.findChild<QLabel*>("inmarsatRatePresetStatus");
    REQUIRE(plans); REQUIRE(decoder); REQUIRE(frequency); REQUIRE(hint);
    for (const auto& plan : InmarsatBandPlanStore::instance().plans()) {
        plans->setCurrentIndex(plans->findData(QString::fromStdString(plan.id)));
        for (int rate : {600, 1200, 8400, 10500, -1200, -10500, 0}) {
            CAPTURE(plan.id, rate);
            frequency->setValue(1555.123456);
            const auto before = engine.config().toJson();
            const int row = decoder->findData(rate);
            REQUIRE(row >= 0);
            decoder->setCurrentIndex(row);
            CHECK(frequency->value() == 1555.123456); // Programmatic restore never selects a preset.
            REQUIRE(QMetaObject::invokeMethod(decoder, "activated", Qt::DirectConnection, Q_ARG(int, row)));
            CHECK(engine.config().toJson() == before); // Preview does not retune RF.
            const InmarsatChannel* match = nullptr;
            for (const auto& ch : plan.channels) if (ch.baud == rate) { match = &ch; break; }
            if (match) {
                CHECK(frequency->value() == match->freqHz / 1e6);
                CHECK(hint->text().contains("not yet tuned"));
                widget.findChild<QPushButton*>("inmarsatTune")->click();
                CHECK(engine.config().channelHz == match->freqHz);
                CHECK(engine.config().baud == rate);
                CHECK(engine.config().mode == match->mode);
            } else {
                CHECK(frequency->value() == 1555.123456);
                CHECK(hint->text().contains("No surveyed preset"));
            }
        }
    }
    plans->setCurrentIndex(plans->findData("4f2"));
    decoder->setCurrentIndex(decoder->findData(8400));
    frequency->setValue(1542.995); // Keep an existing matching voice center, not always the first.
    REQUIRE(QMetaObject::invokeMethod(decoder, "activated", Qt::DirectConnection,
        Q_ARG(int, decoder->currentIndex())));
    CHECK(frequency->value() == 1542.995);
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

TEST_CASE("Inmarsat multi click adds independent saved channels without duplicates", "[inmarsat][gui]") {
    auto& engine=InmarsatEngine::instance();REQUIRE(engine.setConfig(InmarsatEngineConfig::defaults()));
    InmarsatWidget widget;
    auto* add=widget.findChild<QCheckBox*>("inmarsatWatchClickAdd");REQUIRE(add);
    add->setChecked(true);
    auto* spectrum=widget.findChild<InmarsatWatchSpectrum*>();
    spectrum->frequencySelected(1542125000);spectrum->frequencySelected(1542137500);
    spectrum->frequencySelected(1542125000);
    REQUIRE(engine.config().watch.channels.size()==2);
    auto* selection=widget.findChild<QComboBox*>("inmarsatConstellationChannel");
    REQUIRE(selection->currentData().toString().toStdString()==engine.config().watch.channels[0].id);
    add->setChecked(false);spectrum->frequencySelected(1542150000);
    REQUIRE(engine.config().watch.channels.size()==2);
    REQUIRE(widget.findChild<QTimer*>("inmarsatVisualTimer"));
    widget.resize(1100,980);widget.show();QApplication::processEvents();
    REQUIRE(widget.findChild<QTimer*>("inmarsatVisualTimer")->interval()==50);
    widget.hide();REQUIRE_FALSE(widget.findChild<QTimer*>("inmarsatVisualTimer")->isActive());
}

TEST_CASE("Inmarsat visual history advances only for fresh RF and clears on retune", "[inmarsat][gui]") {
    InmarsatWatchSpectrum spectrum;spectrum.resize(1000,280);
    std::vector<float> bins(4096,-110);
    spectrum.setSpectrum(bins,1542e6,2e6,{});REQUIRE(spectrum.waterfallRows()==1);
    for(int i=0;i<20;++i)spectrum.setSpectrum(bins,1542e6,2e6,{});
    REQUIRE(spectrum.waterfallRows()==1);
    QElapsedTimer timer;timer.start();
    for(int i=0;i<200;++i){bins[i]=float(-100+i%70);spectrum.setSpectrum(bins,1542e6,2e6,{});spectrum.grab();}
    std::cout<<"AERO_VISUAL 200 updates/renders ms="<<timer.elapsed()<<std::endl;
    REQUIRE(spectrum.waterfallRows()==201);
    spectrum.setSpectrum(bins,1543e6,2e6,{});REQUIRE(spectrum.waterfallRows()==1);
    spectrum.setSpectrum({},0,0,{});REQUIRE(spectrum.waterfallRows()==0);
    InmarsatConstellationWidget scatter;scatter.resize(240,250);
    InmarsatChannelDisplay channel;channel.rate=10500;channel.frequencyHz=1546005000;
    channel.constellation.points={{.7f,.7f},{-.7f,.7f},{-.7f,-.7f},{.7f,-.7f}};
    scatter.setChannel(&channel);REQUIRE(scatter.pointCount()==4);
    REQUIRE_FALSE(scatter.grab().isNull());
    scatter.setChannel(nullptr);REQUIRE(scatter.pointCount()==0);
}

TEST_CASE("Manual Aero tuning releases a pinned watch constellation", "[inmarsat][gui]") {
    auto& engine=InmarsatEngine::instance();
    auto cfg=InmarsatEngineConfig::defaults();
    cfg.watch.channels={{"first","data",1546005000,10500,true},{"second","voice",1542935000,8400,true}};
    REQUIRE(engine.setConfig(cfg));
    InmarsatWidget widget;
    auto* selection=widget.findChild<QComboBox*>("inmarsatConstellationChannel");
    auto* decoder=widget.findChild<QComboBox*>("inmarsatDecoder");
    REQUIRE(selection); REQUIRE(decoder);
    for(int rate:{600,1200,8400,10500,-1200,-10500}) {
        selection->setCurrentIndex(selection->findData("second"));
        decoder->setCurrentIndex(decoder->findData(rate));
        REQUIRE(QMetaObject::invokeMethod(decoder,"activated",Qt::DirectConnection,Q_ARG(int,decoder->currentIndex())));
        CHECK(selection->currentData().toString()=="second"); // Preview is not a retune.
        widget.findChild<QPushButton*>("inmarsatTune")->click();
        CHECK(selection->currentData().toString().isEmpty());
    }
    selection->setCurrentIndex(selection->findData("first"));
    auto* table=widget.findChild<QTableWidget*>("inmarsatPresetChannels");
    REQUIRE(table); REQUIRE(table->rowCount()>0);
    REQUIRE(QMetaObject::invokeMethod(table,"cellDoubleClicked",Qt::DirectConnection,Q_ARG(int,0),Q_ARG(int,0)));
    CHECK(selection->currentData().toString().isEmpty());
}

TEST_CASE("Constellation switching never reuses another channel's symbols", "[inmarsat][gui]") {
    InmarsatConstellationWidget widget; widget.resize(270,250);
    InmarsatChannelDisplay a{"a",1546005000,10500,false,0,{}}, b{"b",1542935000,8400,true,0,{}};
    a.constellation.points={{.7f,.7f},{-.7f,.7f}};
    b.constellation.points={{-.7f,-.7f}};
    widget.setChannels({a,b},"a");
    CHECK(widget.pointCount()==2); CHECK(widget.channelText().contains("1546.005000"));
    CHECK(widget.statusText()=="Acquiring");
    widget.setChannels({a,b},"b");
    CHECK(widget.pointCount()==1); CHECK(widget.channelText().contains("1542.935000"));
    CHECK(widget.statusText()=="Protocol lock");
    const auto visualDir=qEnvironmentVariable("SDR_TOWN_TEST_VISUAL_DIR");
    if(!visualDir.isEmpty()) {
        CHECK(widget.grab().save(visualDir+"/inmarsat-constellation-active.png"));
        widget.resize(170,180);
        CHECK(widget.grab().save(visualDir+"/inmarsat-constellation-compact.png"));
    }
    widget.setChannels({a},"b");
    CHECK(widget.pointCount()==0); CHECK(widget.channelText().isEmpty());
    CHECK(widget.statusText()=="Not in active group");
    b.id.clear(); widget.setChannels({b},"");
    CHECK(widget.pointCount()==1); CHECK(widget.channelText().contains("8400"));
    b.constellation.points.clear();widget.setChannels({b},"");
    CHECK(widget.pointCount()==0);CHECK(widget.statusText()=="No fresh symbols");
    b.rate=0;widget.setChannels({b},"");
    CHECK(widget.pointCount()==0);CHECK(widget.statusText()=="No native EGC constellation");
    CHECK(widget.channelText().contains("EGC"));
    widget.setChannels({},"");
    CHECK(widget.pointCount()==0);CHECK(widget.statusText()=="No active decoder");
}
