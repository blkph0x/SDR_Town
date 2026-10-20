#include "SatcomScannerWidget.h"
#include "SatcomScannerEngine.h"
#include "SatcomHubWidget.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Satcom range draft survives polling and starts only explicitly", "[satcom][gui][range-scan]") {
    const auto previous = qApp->property("sdrtown.guiDryRun");
    struct Restore { QVariant value; ~Restore() { qApp->setProperty("sdrtown.guiDryRun", value); } } restore{previous};
    SatcomScannerWidget widget(nullptr, "qa-satcom-range-draft");
    widget.engine().setConfig(SatcomScannerConfig::defaults());
    widget.reloadSessionControls();
    auto* low = widget.findChild<QDoubleSpinBox*>("satcomRangeLow"); REQUIRE(low);
    auto* high = widget.findChild<QDoubleSpinBox*>("satcomRangeHigh"); REQUIRE(high);
    auto* ranges = widget.findChild<QComboBox*>("satcomRangePreset"); REQUIRE(ranges);
    auto* start = widget.findChild<QPushButton*>("satcomStartScan"); REQUIRE(start);
    auto* summary = widget.findChild<QLabel*>("satcomRangeSummary"); REQUIRE(summary);
    auto* tabs = widget.findChild<QTabWidget*>("satcomTasks"); REQUIRE(tabs);
    const int broad = ranges->findText("UHF search 240-322"); REQUIRE(broad > 0);
    ranges->setCurrentIndex(broad);
    REQUIRE(QMetaObject::invokeMethod(ranges, "activated", Q_ARG(int, broad)));
    CHECK(low->value() == 240); CHECK(high->value() == 322);
    CHECK(widget.engine().config().lowHz == 243e6);
    CHECK(widget.engine().snapshot().state == SatcomScannerState::Idle);
    REQUIRE(QMetaObject::invokeMethod(&widget, "refreshUi", Qt::DirectConnection));
    CHECK(low->value() == 240); CHECK(high->value() == 322);
    CHECK(summary->text().contains("3281"));
    CHECK(summary->text().contains("262.5 s"));
    CHECK(summary->text().contains("Pending"));
    low->setValue(400);
    CHECK_FALSE(start->isEnabled());
    CHECK(summary->text().contains("From"));
    low->setValue(240);
    tabs->setCurrentIndex(2); CHECK_FALSE(start->isEnabled());
    tabs->setCurrentIndex(0); CHECK(start->isEnabled());
    QApplication::instance()->setProperty("sdrtown.guiDryRun", true);
    start->click();
    CHECK(widget.engine().config().lowHz == 240e6);
    CHECK_FALSE(summary->text().contains("Pending"));
    CHECK(widget.engine().snapshot().state == SatcomScannerState::Idle);
    CHECK(widget.engine().snapshot().activeDeviceIndex == size_t(-1));

    widget.resize(960, 720); widget.show(); QApplication::processEvents();
    CHECK(widget.size() == QSize(960, 720));
    CHECK(start->isVisible());
    auto* empty = widget.findChild<QLabel*>("satcomSpectrumEmpty"); REQUIRE(empty);
    CHECK(empty->isVisible());
    CHECK(widget.rect().contains(start->mapTo(&widget, start->rect().bottomRight())));
    const auto dir = qEnvironmentVariable("SDR_TOWN_TEST_VISUAL_DIR");
    if (!dir.isEmpty()) {
        REQUIRE(QDir().mkpath(dir));
        CHECK(widget.grab().save(dir + "/satcom-range-compact.png"));
        auto* advanced = widget.findChild<QToolButton*>("satcomAdvanced"); REQUIRE(advanced);
        advanced->click(); QApplication::processEvents();
        CHECK(widget.grab().save(dir + "/satcom-range-advanced.png"));
        widget.resize(1280, 800); QApplication::processEvents();
        CHECK(widget.grab().save(dir + "/satcom-range-desktop.png"));
    }
}

TEST_CASE("Satcom custom receive ranges survive closing without tuning", "[satcom][gui][range-scan]") {
    {
        SatcomScannerWidget widget(nullptr, "qa-satcom-range-save");
        widget.engine().setConfig(SatcomScannerConfig::defaults());
        widget.reloadSessionControls();
        widget.findChild<QDoubleSpinBox*>("satcomRangeLow")->setValue(250.125);
        widget.findChild<QDoubleSpinBox*>("satcomRangeHigh")->setValue(251.625);
        widget.findChild<QSpinBox*>("satcomDwell")->setValue(125);
        QTimer::singleShot(0, [] {
            auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            if (dialog) { dialog->setTextValue("My receive range"); dialog->accept(); }
        });
        widget.findChild<QPushButton*>("satcomSaveRange")->click();
        CHECK(widget.engine().config().lowHz == 243e6);
        CHECK(widget.engine().config().presets.back().name == "My receive range");
        CHECK(widget.engine().config().presets.back().dwellMs == 125);
        CHECK(widget.engine().snapshot().state == SatcomScannerState::Idle);
    }
    SatcomScannerWidget reopened(nullptr, "qa-satcom-range-save");
    const auto config = reopened.engine().config();
    CHECK(config.presets.back().lowHz == 250.125e6);
    CHECK(config.presets.back().highHz == 251.625e6);
    CHECK(config.presets.back().dwellMs == 125);
    CHECK(config.lowHz == 243e6);
    auto* ranges = reopened.findChild<QComboBox*>("satcomRangePreset");
    auto* remove = reopened.findChild<QPushButton*>("satcomDeleteRange");
    REQUIRE(ranges); REQUIRE(remove);
    CHECK_FALSE(remove->isEnabled()); // Built-in default cannot be removed.
    const int custom = ranges->findText("My receive range"); REQUIRE(custom > 0);
    ranges->setCurrentIndex(custom);
    REQUIRE(QMetaObject::invokeMethod(ranges, "activated", Q_ARG(int, custom)));
    REQUIRE(remove->isEnabled());
    QTimer::singleShot(0, [] {
        if (auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            dialog->button(QMessageBox::No)->click();
    });
    remove->click();
    CHECK(reopened.engine().config().presets.size() == config.presets.size());
    QTimer::singleShot(0, [] {
        if (auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            dialog->button(QMessageBox::Yes)->click();
    });
    remove->click();
    CHECK(reopened.engine().config().presets.size() + 1 == config.presets.size());
    CHECK(reopened.engine().snapshot().state == SatcomScannerState::Idle);
}

TEST_CASE("Satcom invalid range is rejected before radio lookup", "[satcom][gui][range-scan]") {
    SatcomScannerEngine engine("qa-invalid-range");
    auto config = SatcomScannerConfig::defaults();
    config.deviceStableKey = "missing-qa-radio";
    config.lowHz = 400e6; config.highHz = 240e6;
    engine.setConfig(config);
    CHECK_FALSE(engine.start());
    CHECK(engine.snapshot().lastStatus.find("From") != std::string::npos);
    CHECK(engine.snapshot().activeDeviceIndex == size_t(-1));
    CHECK(engine.snapshot().state == SatcomScannerState::Idle);
}

TEST_CASE("Satcom remote ranges use desktop validation and remain idle", "[satcom][gui][range-scan]") {
    SatcomHubWidget hub;
    hub.prepareRemoteWorkspace();
    const QString id = "qa-range-web";
    REQUIRE(hub.controlReceiverSessions("satcom", "POST", {{"sessionId", id}, {"action", "open"}}).value("ok").toBool());
    const auto configure = [&](const QJsonObject& config) {
        return hub.controlReceiverSessions("satcom", "POST",
            {{"sessionId", id}, {"action", "configure"}, {"config", config}});
    };
    CHECK(configure({{"lowHz", 240e6}, {"highHz", 322e6}, {"stepHz", 25000}, {"dwellMs", 125}}).value("ok").toBool());
    CHECK_FALSE(configure({{"highHz", 239e6}}).value("ok").toBool());
    CHECK_FALSE(configure({{"dwellMs", 20.5}}).value("ok").toBool());
    CHECK_FALSE(configure({{"stepHz", 0}}).value("ok").toBool());
    CHECK_FALSE(configure({{"mode", "MUOS"}}).value("ok").toBool());
    auto* widget = hub.findChild<SatcomScannerWidget*>("satcomSession." + id); REQUIRE(widget);
    CHECK(widget->engine().config().highHz == 322e6);
    CHECK(widget->engine().config().dwellMs == 125);
    CHECK(widget->engine().snapshot().state == SatcomScannerState::Idle);
    REQUIRE(hub.controlReceiverSessions("satcom", "POST", {{"sessionId", id}, {"action", "close"}}).value("ok").toBool());
}
