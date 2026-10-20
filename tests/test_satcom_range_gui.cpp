#include "SatcomScannerWidget.h"
#include "SatcomScannerEngine.h"
#include "SatcomHubWidget.h"
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSlider>
#include <QTabWidget>
#include <QTableWidget>
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
    auto* smart = widget.findChild<QCheckBox*>("satcomSmartScan"); REQUIRE(smart);
    REQUIRE(tabs->count() == 4);
    CHECK(tabs->tabText(1) == "Satellite Passes");
    CHECK(tabs->tabText(3) == "Channels");
    const int broad = ranges->findText("UHF search 240-322"); REQUIRE(broad > 0);
    ranges->setCurrentIndex(broad);
    REQUIRE(QMetaObject::invokeMethod(ranges, "activated", Q_ARG(int, broad)));
    CHECK(low->value() == 240); CHECK(high->value() == 322);
    CHECK(widget.engine().config().lowHz == 243e6);
    CHECK(widget.engine().snapshot().state == SatcomScannerState::Idle);
    REQUIRE(QMetaObject::invokeMethod(&widget, "refreshUi", Qt::DirectConnection));
    CHECK(low->value() == 240); CHECK(high->value() == 322);
    CHECK(summary->text().contains("Smart wideband"));
    CHECK(summary->text().contains("Tile count pending capture"));
    CHECK_FALSE(summary->text().contains("tunes"));
    CHECK_FALSE(summary->text().contains("262.5 s"));
    CHECK(summary->text().contains("Pending"));
    smart->setChecked(false);
    CHECK(summary->text().contains("3281 tunes"));
    CHECK(summary->text().contains("262.5 s"));
    smart->setChecked(true);
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

namespace {
struct SatcomChannelsDryRun {
    QVariant previous = qApp->property("sdrtown.guiDryRun");
    SatcomChannelsDryRun() { qApp->setProperty("sdrtown.guiDryRun", true); }
    ~SatcomChannelsDryRun() { qApp->setProperty("sdrtown.guiDryRun", previous); }
};

void enterChannelLabel(QLineEdit* editor, const QString& text) {
    editor->selectAll();
    QKeyEvent input(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
    QApplication::sendEvent(editor, &input);
    REQUIRE(editor->isModified());
    REQUIRE(editor->text() == text);
}
}

TEST_CASE("Satcom channel editors persist priority watch and label without RF", "[satcom][gui][range-scan]") {
    SatcomChannelsDryRun dryRun;
    {
        SatcomScannerWidget widget(nullptr, "qa-satcom-channels-save");
        widget.engine().setConfig(SatcomScannerConfig::defaults());
        widget.reloadSessionControls();
        auto* table = widget.findChild<QTableWidget*>("satcomChannels"); REQUIRE(table);
        auto* frequency = widget.findChild<QDoubleSpinBox*>("satcomChannelMHz"); REQUIRE(frequency);
        auto* add = widget.findChild<QPushButton*>("satcomAddChannel"); REQUIRE(add);
        auto* tabs = widget.findChild<QTabWidget*>("satcomTasks"); REQUIRE(tabs);
        tabs->setCurrentIndex(3);
        frequency->setValue(250.125);
        add->click();
        REQUIRE(table->rowCount() == 1);
        REQUIRE(widget.engine().config().watchChannels.size() == 1);
        auto* label = qobject_cast<QLineEdit*>(table->cellWidget(0, 1)); REQUIRE(label);
        auto* priority = qobject_cast<QComboBox*>(table->cellWidget(0, 2)); REQUIRE(priority);
        auto* watch = qobject_cast<QCheckBox*>(table->cellWidget(0, 3)); REQUIRE(watch);
        CHECK(priority->itemText(0) == "High"); CHECK(priority->itemData(0).toInt() == 2);
        CHECK(priority->itemText(1) == "Medium"); CHECK(priority->itemData(1).toInt() == 1);
        CHECK(priority->itemText(2) == "Low"); CHECK(priority->itemData(2).toInt() == 0);
        CHECK(priority->currentData().toInt() == 1);
        CHECK(watch->isChecked());
        enterChannelLabel(label, "Primary voice");
        REQUIRE(QMetaObject::invokeMethod(label, "editingFinished", Qt::DirectConnection));
        priority->setCurrentIndex(priority->findData(2));
        watch->click();
        auto saved = widget.engine().config().watchChannels.front();
        CHECK(saved.frequencyHz == 250.125e6);
        CHECK(saved.label == "Primary voice");
        CHECK(saved.priority == 2);
        CHECK_FALSE(saved.enabled);
        add->click(); // Re-adding selects the existing channel, never resets its settings.
        CHECK(table->rowCount() == 1);
        CHECK(table->currentRow() == 0);
        CHECK(widget.engine().config().watchChannels.front().label == saved.label);
        CHECK(widget.engine().config().watchChannels.front().priority == 2);
        CHECK_FALSE(widget.engine().config().watchChannels.front().enabled);
        CHECK(widget.engine().snapshot().state == SatcomScannerState::Idle);
        CHECK(widget.engine().snapshot().activeDeviceIndex == size_t(-1));
    }
    SatcomScannerWidget reopened(nullptr, "qa-satcom-channels-save");
    auto* table = reopened.findChild<QTableWidget*>("satcomChannels"); REQUIRE(table);
    REQUIRE(table->rowCount() == 1);
    CHECK(qobject_cast<QLineEdit*>(table->cellWidget(0, 1))->text() == "Primary voice");
    CHECK(qobject_cast<QComboBox*>(table->cellWidget(0, 2))->currentData().toInt() == 2);
    CHECK_FALSE(qobject_cast<QCheckBox*>(table->cellWidget(0, 3))->isChecked());
    CHECK(table->item(0, 4)->text() == "Off");
    auto* remove = reopened.findChild<QPushButton*>("satcomRemoveChannel"); REQUIRE(remove);
    CHECK_FALSE(remove->isEnabled());
    // Focusing a cell editor must also select the row addressed by Remove.
    QFocusEvent focus(QEvent::FocusIn);
    QApplication::sendEvent(table->cellWidget(0, 1), &focus);
    REQUIRE(remove->isEnabled());
    remove->click();
    CHECK(table->rowCount() == 0);
    CHECK(reopened.engine().config().watchChannels.empty());
    CHECK_FALSE(remove->isEnabled());
    SatcomScannerConfig persisted;
    persisted.load("qa-satcom-channels-save");
    CHECK(persisted.watchChannels.empty());
}

TEST_CASE("Satcom discovery and polling preserve pending channel and range edits", "[satcom][gui][range-scan]") {
    SatcomChannelsDryRun dryRun;
    SatcomScannerWidget widget(nullptr, "qa-satcom-channels-poll");
    auto config = SatcomScannerConfig::defaults();
    config.watchChannels = {{250e6, "Original", 1, true}};
    widget.engine().setConfig(config);
    widget.reloadSessionControls();
    auto* table = widget.findChild<QTableWidget*>("satcomChannels"); REQUIRE(table);
    auto* label = qobject_cast<QLineEdit*>(table->cellWidget(0, 1)); REQUIRE(label);
    auto* priority = qobject_cast<QComboBox*>(table->cellWidget(0, 2)); REQUIRE(priority);
    auto* watch = qobject_cast<QCheckBox*>(table->cellWidget(0, 3)); REQUIRE(watch);
    auto* low = widget.findChild<QDoubleSpinBox*>("satcomRangeLow"); REQUIRE(low);
    low->setValue(249);
    table->setCurrentCell(0, 0);
    enterChannelLabel(label, "Unfinished label");
    label->setCursorPosition(5);
    REQUIRE(widget.engine().setWatchChannel({251e6, "Discovered", 1, true}));
    for (int i = 0; i < 3; ++i)
        REQUIRE(QMetaObject::invokeMethod(&widget, "refreshUi", Qt::DirectConnection));
    REQUIRE(table->rowCount() == 2);
    CHECK(table->cellWidget(0, 1) == label);
    CHECK(table->cellWidget(0, 2) == priority);
    CHECK(table->cellWidget(0, 3) == watch);
    CHECK(label->text() == "Unfinished label");
    CHECK(label->cursorPosition() == 5);
    CHECK(label->isModified());
    CHECK(table->currentRow() == 0);
    CHECK(low->value() == 249);
    CHECK(widget.engine().config().lowHz == config.lowHz);
    priority->setCurrentIndex(priority->findData(0));
    watch->click();
    CHECK(widget.engine().config().watchChannels.front().priority == 0);
    CHECK_FALSE(widget.engine().config().watchChannels.front().enabled);
    CHECK(widget.engine().config().watchChannels.front().label == "Original");
    REQUIRE(QMetaObject::invokeMethod(&widget, "refreshUi", Qt::DirectConnection));
    CHECK(label->text() == "Unfinished label");
    REQUIRE(QMetaObject::invokeMethod(label, "editingFinished", Qt::DirectConnection));
    const auto saved = widget.engine().config().watchChannels.front();
    CHECK(saved.label == "Unfinished label");
    CHECK(saved.priority == 0);
    CHECK_FALSE(saved.enabled);
    REQUIRE(QMetaObject::invokeMethod(&widget, "applyFieldsToConfig", Qt::DirectConnection));
    CHECK(widget.engine().config().watchChannels.size() == 2);
    CHECK(widget.engine().config().watchChannels.front().label == saved.label);
    CHECK(widget.engine().config().lowHz == 249e6);
    CHECK(widget.engine().snapshot().state == SatcomScannerState::Idle);
}

TEST_CASE("Satcom smart controls survive polling and apply from Channels without RF", "[satcom][gui][range-scan]") {
    SatcomChannelsDryRun dryRun;
    SatcomScannerWidget widget(nullptr, "qa-satcom-smart-controls");
    widget.engine().setConfig(SatcomScannerConfig::defaults());
    widget.reloadSessionControls();
    auto* smart = widget.findChild<QCheckBox*>("satcomSmartScan"); REQUIRE(smart);
    auto* automatic = widget.findChild<QCheckBox*>("satcomAutoSquelch"); REQUIRE(automatic);
    auto* margin = widget.findChild<QDoubleSpinBox*>("satcomSquelchMargin"); REQUIRE(margin);
    auto* hold = widget.findChild<QSpinBox*>("satcomQuietHold"); REQUIRE(hold);
    auto* manual = widget.findChild<QDoubleSpinBox*>("satcomSquelch"); REQUIRE(manual);
    auto* volume = widget.findChild<QSlider*>("satcomSmartVolume"); REQUIRE(volume);
    auto* output = widget.findChild<QLabel*>("satcomSmartOutput"); REQUIRE(output);
    auto* start = widget.findChild<QPushButton*>("satcomStartScan"); REQUIRE(start);
    auto* tabs = widget.findChild<QTabWidget*>("satcomTasks"); REQUIRE(tabs);
    CHECK(smart->isChecked()); CHECK(automatic->isChecked());
    CHECK(margin->value() == 12); CHECK(hold->value() == 600);
    CHECK(volume->value() == 80);
    CHECK(output->text() == "Default playback device");
    CHECK(margin->minimum() == 3); CHECK(margin->maximum() == 30);
    CHECK(hold->minimum() == 100); CHECK(hold->maximum() == 5000);
    CHECK_FALSE(manual->isEnabled());
    margin->setValue(15.5); hold->setValue(950); automatic->setChecked(false);
    volume->setValue(35);
    CHECK(widget.engine().config().smartVolume == 0.35);
    REQUIRE(QMetaObject::invokeMethod(&widget, "refreshUi", Qt::DirectConnection));
    CHECK(margin->value() == 15.5); CHECK(hold->value() == 950);
    CHECK_FALSE(automatic->isChecked());
    CHECK(volume->value() == 35);
    CHECK_FALSE(margin->isEnabled());
    CHECK(manual->isEnabled());
    manual->setValue(-82);
    tabs->setCurrentIndex(3); REQUIRE(start->isEnabled());
    smart->setChecked(false);
    CHECK_FALSE(start->isEnabled());
    CHECK_FALSE(automatic->isEnabled()); CHECK_FALSE(hold->isEnabled());
    CHECK_FALSE(volume->isEnabled());
    tabs->setCurrentIndex(0); REQUIRE(start->isEnabled());
    start->click();
    CHECK_FALSE(widget.engine().config().smartScan);
    CHECK_FALSE(widget.engine().config().autoSquelch);
    CHECK(widget.engine().config().squelchDb == -82);
    smart->setChecked(true); automatic->setChecked(true);
    tabs->setCurrentIndex(3); REQUIRE(start->isEnabled());
    start->click();
    const auto saved = widget.engine().config();
    CHECK(saved.smartScan); CHECK(saved.autoSquelch);
    CHECK(saved.squelchMarginDb == 15.5); CHECK(saved.quietHoldMs == 950);
    CHECK(saved.smartVolume == 0.35);
    CHECK(volume->isEnabled());
    CHECK(widget.engine().snapshot().state == SatcomScannerState::Idle);
    CHECK(widget.engine().snapshot().activeDeviceIndex == size_t(-1));
    SatcomScannerConfig persisted;
    persisted.load("qa-satcom-smart-controls");
    CHECK(persisted.smartScan); CHECK(persisted.autoSquelch);
    CHECK(persisted.squelchMarginDb == 15.5); CHECK(persisted.quietHoldMs == 950);
    CHECK(persisted.smartVolume == 0.35);
    auto external = saved;
    external.squelchMarginDb = 9; external.quietHoldMs = 1200; external.autoSquelch = false;
    widget.engine().setConfig(external); widget.reloadSessionControls();
    CHECK(margin->value() == 9); CHECK(hold->value() == 1200);
    CHECK_FALSE(automatic->isChecked()); CHECK(manual->isEnabled());
    widget.engine().setSmartVolume(0.6);
    widget.reloadSessionControls(); CHECK(volume->value() == 60);
    volume->setValue(0); CHECK(widget.engine().config().smartVolume == 0);
    volume->setValue(100); CHECK(widget.engine().config().smartVolume == 1);
    auto* mode = widget.findChild<QComboBox*>("satcomMode"); REQUIRE(mode);
    auto* summary = widget.findChild<QLabel*>("satcomRangeSummary"); REQUIRE(summary);
    for (const auto& dataMode : {"APT", "APRS"}) {
        mode->setCurrentText(dataMode);
        CHECK_FALSE(start->isEnabled()); // Channels only starts the supported Smart analog path.
        CHECK_FALSE(volume->isEnabled());
        CHECK_FALSE(automatic->isEnabled());
        CHECK(manual->isEnabled());
        CHECK_FALSE(summary->text().contains("Smart wideband"));
        CHECK(summary->text().contains("tunes"));
    }
    mode->setCurrentText("NFM");
    CHECK(start->isEnabled());
    tabs->setCurrentIndex(2); CHECK_FALSE(start->isEnabled());
    CHECK(tabs->tabText(1) == "Satellite Passes");
    const auto dir = qEnvironmentVariable("SDR_TOWN_TEST_VISUAL_DIR");
    if (!dir.isEmpty()) {
        REQUIRE(widget.engine().setWatchChannel({250.125e6, "Priority activity", 2, true}));
        REQUIRE(widget.engine().setWatchChannel({251.5e6, "Secondary", 1, true}));
        REQUIRE(widget.engine().setWatchChannel({260e6, "Paused", 0, false}));
        REQUIRE(QMetaObject::invokeMethod(&widget, "refreshUi", Qt::DirectConnection));
        tabs->setCurrentIndex(3);
        widget.resize(960, 720); widget.show(); QApplication::processEvents();
        REQUIRE(QDir().mkpath(dir));
        CHECK(widget.grab().save(dir + "/satcom-channels-compact.png"));
    }
}

TEST_CASE("Satcom channel table is bounded and removal frees a saved slot", "[satcom][gui][range-scan]") {
    SatcomChannelsDryRun dryRun;
    SatcomScannerWidget widget(nullptr, "qa-satcom-channel-limit");
    auto config = SatcomScannerConfig::defaults();
    for (size_t i = 0; i < SatcomScanPolicy::maxChannels; ++i)
        config.watchChannels.push_back({243e6 + i * 25000, {}, 1, true});
    widget.engine().setConfig(config); widget.reloadSessionControls();
    auto* table = widget.findChild<QTableWidget*>("satcomChannels"); REQUIRE(table);
    auto* add = widget.findChild<QPushButton*>("satcomAddChannel"); REQUIRE(add);
    auto* remove = widget.findChild<QPushButton*>("satcomRemoveChannel"); REQUIRE(remove);
    REQUIRE(table->rowCount() == static_cast<int>(SatcomScanPolicy::maxChannels));
    CHECK_FALSE(add->isEnabled());
    auto* retained = table->cellWidget(1, 1);
    table->setCurrentCell(0, 0); remove->click();
    CHECK(table->rowCount() == static_cast<int>(SatcomScanPolicy::maxChannels) - 1);
    CHECK(table->cellWidget(0, 1) == retained);
    CHECK(add->isEnabled());
    widget.findChild<QDoubleSpinBox*>("satcomChannelMHz")->setValue(260.125);
    add->click();
    CHECK(table->rowCount() == static_cast<int>(SatcomScanPolicy::maxChannels));
    CHECK_FALSE(add->isEnabled());
    CHECK(widget.engine().snapshot().state == SatcomScannerState::Idle);
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

TEST_CASE("Satcom remote Smart controls persist and preserve local channel edits", "[satcom][gui][range-scan]") {
    SatcomChannelsDryRun dryRun;
    SatcomHubWidget hub;
    hub.prepareRemoteWorkspace();
    const QString id = "qa-smart-web", otherId = "qa-smart-web-other";
    const auto post = [&](const QString& action, QJsonObject body = {}) {
        body.insert("sessionId", id);
        body.insert("action", action);
        return hub.controlReceiverSessions("satcom", "POST", body);
    };
    REQUIRE(post("open").value("ok").toBool());
    REQUIRE(hub.controlReceiverSessions("satcom", "POST",
        {{"sessionId", otherId}, {"action", "open"}}).value("ok").toBool());
    auto* widget = hub.findChild<SatcomScannerWidget*>("satcomSession." + id); REQUIRE(widget);
    auto* other = hub.findChild<SatcomScannerWidget*>("satcomSession." + otherId); REQUIRE(other);
    widget->engine().setConfig(SatcomScannerConfig::defaults());
    other->engine().setConfig(SatcomScannerConfig::defaults());
    const auto otherBefore = other->engine().config().toJson();
    REQUIRE(post("configure", {{"config", QJsonObject{
        {"smartScan", true}, {"autoSquelch", false}, {"squelchMarginDb", 15.5},
        {"quietHoldMs", 950}, {"smartVolume", 0.45}}}}).value("ok").toBool());
    auto* smart = widget->findChild<QCheckBox*>("satcomSmartScan"); REQUIRE(smart);
    auto* automatic = widget->findChild<QCheckBox*>("satcomAutoSquelch"); REQUIRE(automatic);
    auto* margin = widget->findChild<QDoubleSpinBox*>("satcomSquelchMargin"); REQUIRE(margin);
    auto* hold = widget->findChild<QSpinBox*>("satcomQuietHold"); REQUIRE(hold);
    auto* volume = widget->findChild<QSlider*>("satcomSmartVolume"); REQUIRE(volume);
    CHECK(smart->isChecked()); CHECK_FALSE(automatic->isChecked());
    CHECK(margin->value() == 15.5); CHECK(hold->value() == 950); CHECK(volume->value() == 45);
    const auto configured = widget->engine().config().toJson();
    for (const auto& invalid : {
            QJsonObject{{"smartScan", 1}}, QJsonObject{{"autoSquelch", "true"}},
            QJsonObject{{"squelchMarginDb", 2}}, QJsonObject{{"squelchMarginDb", 31}},
            QJsonObject{{"quietHoldMs", 99}}, QJsonObject{{"quietHoldMs", 5001}},
            QJsonObject{{"quietHoldMs", 600.5}}, QJsonObject{{"smartVolume", -0.1}},
            QJsonObject{{"smartVolume", 1.1}}, QJsonObject{{"smartVolume", "0.5"}}}) {
        const auto result = post("configure", {{"config", invalid}});
        CHECK_FALSE(result.value("ok").toBool());
        CHECK(result.value("status").toInt() == 400);
        CHECK(widget->engine().config().toJson() == configured);
    }

    const QJsonObject channel{{"frequencyHz", 250.125e6}, {"label", "Remote priority"},
                              {"priority", 2}, {"enabled", true}};
    REQUIRE(post("watch_set", channel).value("ok").toBool());
    const auto saved = widget->engine().config().toJson();
    for (const auto& invalid : {
            QJsonObject{{"frequencyHz", 0}}, QJsonObject{{"frequencyHz", "250.125"}},
            QJsonObject{{"priority", -1}}, QJsonObject{{"priority", 3}},
            QJsonObject{{"priority", 1.5}}, QJsonObject{{"enabled", "true"}},
            QJsonObject{{"label", 7}}, QJsonObject{{"label", QString(81, QChar('x'))}}}) {
        auto edit = channel;
        for (auto it = invalid.begin(); it != invalid.end(); ++it) edit.insert(it.key(), it.value());
        const auto result = post("watch_set", edit);
        CHECK_FALSE(result.value("ok").toBool());
        CHECK(result.value("status").toInt() == 400);
        CHECK(widget->engine().config().toJson() == saved);
    }
    REQUIRE(QMetaObject::invokeMethod(widget, "refreshUi", Qt::DirectConnection));
    auto* table = widget->findChild<QTableWidget*>("satcomChannels"); REQUIRE(table);
    REQUIRE(table->rowCount() == 1);
    auto* label = qobject_cast<QLineEdit*>(table->cellWidget(0, 1)); REQUIRE(label);
    auto* priority = qobject_cast<QComboBox*>(table->cellWidget(0, 2)); REQUIRE(priority);
    auto* enabled = qobject_cast<QCheckBox*>(table->cellWidget(0, 3)); REQUIRE(enabled);
    CHECK(label->text() == "Remote priority"); CHECK(priority->currentData().toInt() == 2);
    CHECK(enabled->isChecked());
    enterChannelLabel(label, "Unfinished local label");
    label->setCursorPosition(4);
    auto replacement = channel;
    replacement.insert("label", "Remote saved");
    replacement.insert("priority", 0);
    replacement.insert("enabled", false);
    REQUIRE(post("watch_set", replacement).value("ok").toBool());
    REQUIRE(QMetaObject::invokeMethod(widget, "refreshUi", Qt::DirectConnection));
    CHECK(table->rowCount() == 1);
    CHECK(table->cellWidget(0, 1) == label);
    CHECK(table->cellWidget(0, 2) == priority);
    CHECK(table->cellWidget(0, 3) == enabled);
    CHECK(label->text() == "Unfinished local label"); CHECK(label->cursorPosition() == 4);
    CHECK(priority->currentData().toInt() == 0); CHECK_FALSE(enabled->isChecked());
    REQUIRE(widget->engine().config().watchChannels.size() == 1);
    CHECK(widget->engine().config().watchChannels.front().label == "Remote saved");
    REQUIRE(QMetaObject::invokeMethod(label, "editingFinished", Qt::DirectConnection));
    CHECK(widget->engine().config().watchChannels.front().label == "Unfinished local label");
    CHECK(widget->engine().config().watchChannels.front().priority == 0);
    CHECK_FALSE(widget->engine().config().watchChannels.front().enabled);

    for (double value : {0.0, 1.0, 0.35}) {
        REQUIRE(post("smart_volume", {{"volume", value}}).value("ok").toBool());
        REQUIRE(QMetaObject::invokeMethod(widget, "refreshUi", Qt::DirectConnection));
        CHECK(widget->engine().config().smartVolume == value);
        CHECK(volume->value() == static_cast<int>(value * 100));
    }
    for (const auto& invalid : {QJsonObject{}, QJsonObject{{"volume", -0.1}},
            QJsonObject{{"volume", 1.1}}, QJsonObject{{"volume", "0.5"}}}) {
        const auto result = post("smart_volume", invalid);
        CHECK_FALSE(result.value("ok").toBool());
        CHECK(result.value("status").toInt() == 400);
        CHECK(widget->engine().config().smartVolume == 0.35);
    }
    const auto detail = hub.controlReceiverSessions("satcom", "GET", {{"sessionId", id}, {"view", "status"}});
    REQUIRE(detail.value("ok").toBool());
    const auto status = detail.value("satcom").toObject();
    CHECK(status.value("sessionId").toString() == id);
    for (const auto& counter : {"scanTiles", "foregroundSwitches", "scanInputSamples", "scanProcessingUs", "scanSlowBlocks"}) {
        CHECK(status.value(counter).isDouble());
        CHECK(status.value(counter).toInteger() == 0);
    }
    REQUIRE(status.value("watchStatus").isArray());
    CHECK(status.value("watchStatus").toArray().isEmpty()); // No RF measurements have been made.
    const auto publicConfig = status.value("config").toObject();
    const auto publicChannels = publicConfig.value("watchChannels").toArray();
    REQUIRE(publicChannels.size() == 1);
    CHECK(publicChannels[0].toObject().value("label").toString() == "Unfinished local label");
    CHECK(publicChannels[0].toObject().value("priority").toInt() == 0);
    CHECK_FALSE(publicChannels[0].toObject().value("enabled").toBool());
    CHECK(publicConfig.value("smartVolume").toDouble() == 0.35);
    CHECK_FALSE(publicConfig.contains("recordDir"));
    CHECK_FALSE(publicConfig.contains("logDir"));
    CHECK_FALSE(publicConfig.contains("sstvSaveDir"));
    CHECK(other->engine().config().toJson() == otherBefore);
    CHECK(widget->engine().snapshot().state == SatcomScannerState::Idle);
    CHECK(widget->engine().snapshot().activeDeviceIndex == size_t(-1));
    CHECK_FALSE(post("start").value("ok").toBool());
    CHECK(widget->engine().snapshot().state == SatcomScannerState::Idle);

    REQUIRE(post("close").value("ok").toBool());
    REQUIRE(post("open").value("ok").toBool());
    widget = hub.findChild<SatcomScannerWidget*>("satcomSession." + id); REQUIRE(widget);
    const auto restored = widget->engine().config();
    REQUIRE(restored.watchChannels.size() == 1);
    CHECK(restored.watchChannels.front().label == "Unfinished local label");
    CHECK(restored.watchChannels.front().frequencyHz == 250.125e6);
    CHECK(restored.watchChannels.front().priority == 0);
    CHECK_FALSE(restored.watchChannels.front().enabled);
    CHECK(restored.smartScan); CHECK_FALSE(restored.autoSquelch);
    CHECK(restored.squelchMarginDb == 15.5); CHECK(restored.quietHoldMs == 950);
    CHECK(restored.smartVolume == 0.35);
    CHECK_FALSE(post("watch_remove", {{"frequencyHz", "250.125"}}).value("ok").toBool());
    REQUIRE(post("watch_remove", {{"frequencyHz", 250.125e6}}).value("ok").toBool());
    CHECK(post("watch_remove", {{"frequencyHz", 250.125e6}}).value("status").toInt() == 404);
    REQUIRE(QMetaObject::invokeMethod(widget, "refreshUi", Qt::DirectConnection));
    CHECK(widget->findChild<QTableWidget*>("satcomChannels")->rowCount() == 0);
    SatcomScannerConfig persisted;
    persisted.load(id.toStdString());
    CHECK(persisted.watchChannels.empty());
    CHECK(persisted.smartVolume == 0.35);
    CHECK(other->engine().config().toJson() == otherBefore);
    REQUIRE(post("close").value("ok").toBool());
    REQUIRE(hub.controlReceiverSessions("satcom", "POST",
        {{"sessionId", otherId}, {"action", "close"}}).value("ok").toBool());
}
