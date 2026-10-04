#include "SatcomHubWidget.h"
#include "SatcomScannerWidget.h"
#include "SatcomScannerEngine.h"
#include "SatPassPlanner.h"
#include "DeviceManager.h"
#include "InmarsatWidget.h"
#include "InmarsatEngine.h"
#include "AircraftMapWidget.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QVariant>
#include <spdlog/spdlog.h>
#include <QHideEvent>
#include <QShowEvent>
#include <QTabWidget>
#include <QTabBar>
#include <QLineEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QToolButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QSettings>
#include <QStyle>
#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>
#include <limits>

namespace {

int autoCapturePriority(const std::string& role) {
    if (role == "sstv") return 0;
    if (role == "apt") return 1;
    if (role == "aprs") return 2;
    if (role == "data") return 3;
    if (role == "voice") return 4;
    return 5;
}

} // namespace

SatcomHubWidget::SatcomHubWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    tabs_->setTabsClosable(true);
    auto* add = new QToolButton(tabs_);
    add->setObjectName("addInmarsatSession");
    add->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    add->setToolTip("Open an Inmarsat receiver session");
    add->setAccessibleName("Add Inmarsat session");
    tabs_->setCornerWidget(add);
    connect(add, &QToolButton::clicked, this, [this] {
        bool ok = false;
        const auto id = QInputDialog::getText(this, "Inmarsat session", "Session name:",
            QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok || id.isEmpty()) return;
        try { openInmarsatSession(id); }
        catch (const std::exception& e) { QMessageBox::warning(this, "Inmarsat session", QString::fromUtf8(e.what())); }
    });
    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        auto* widget = qobject_cast<InmarsatWidget*>(tabs_->widget(index));
        if (!widget || widget == inmarsat_) return;
        tabs_->removeTab(index);
        delete widget; // Joins this engine before releasing its radio and settings.
        saveInmarsatSessions();
    });
    root->addWidget(tabs_);

    // A lightweight controller runs even before the visual tabs are created.
    // It only starts hardware when a selected, supported satellite is in range.
    autoCaptureTimer_ = new QTimer(this);
    autoCaptureTimer_->setInterval(1000);
    connect(autoCaptureTimer_, &QTimer::timeout, this, &SatcomHubWidget::autoCaptureTick);
    // DEC-0176: a saved satellite pass must not open RF during scripted dry-run.
    automaticCaptureAllowed_ = !QCoreApplication::instance()->property("sdrtown.guiDryRun").toBool();
    if (automaticCaptureAllowed_) autoCaptureTimer_->start();
    else spdlog::info("Satellite automatic capture suppressed for GUI dry-run.");
}

void SatcomHubWidget::ensureTabs() {
    if (satcom_) return;

    satcom_ = new SatcomScannerWidget(tabs_);
    satcom_->setWindowFlags(Qt::Widget);
    inmarsat_ = new InmarsatWidget(tabs_);
    inmarsat_->setWindowFlags(Qt::Widget);
    aircraft_ = new AircraftMapWidget(tabs_);
    aircraft_->setEmbedded(true);

    tabs_->addTab(satcom_, "Satcom");
    tabs_->addTab(inmarsat_, "Inmarsat");
    tabs_->addTab(aircraft_, "Aircraft");
    for (int i = 0; i < 3; ++i) {
        tabs_->tabBar()->setTabButton(i, QTabBar::RightSide, nullptr);
        tabs_->tabBar()->setTabButton(i, QTabBar::LeftSide, nullptr);
    }

    connect(satcom_, &SatcomScannerWidget::requestOpenSstvLive,
            this, &SatcomHubWidget::requestOpenSstvLive);
    const auto sessions = QSettings().value("inmarsat/openSessions").toStringList();
    for (const auto& id : sessions.mid(0, 16)) {
        try { openInmarsatSession(id); }
        catch (const std::exception& e) { spdlog::warn("Inmarsat saved session: {}", e.what()); }
    }
}

InmarsatWidget* SatcomHubWidget::openInmarsatSession(const QString& name) {
    ensureTabs();
    const auto id = QString::fromStdString(InmarsatEngine::normalizedSessionId(name.toStdString()));
    if (id.isEmpty()) { tabs_->setCurrentWidget(inmarsat_); return inmarsat_; }
    for (int i = 3; i < tabs_->count(); ++i) {
        if (tabs_->widget(i)->objectName() == "inmarsatSession." + id) {
            tabs_->setCurrentIndex(i);
            return qobject_cast<InmarsatWidget*>(tabs_->widget(i));
        }
    }
    // DEC-0187: UI/worker resource budget, not a radio or channel limit.
    if (tabs_->count() >= 19) throw std::runtime_error("Close an Inmarsat session before opening another (16-session limit)");
    auto* widget = new InmarsatWidget(tabs_, id);
    tabs_->addTab(widget, "Inmarsat: " + id);
    tabs_->setCurrentWidget(widget);
    saveInmarsatSessions();
    return widget;
}

void SatcomHubWidget::saveInmarsatSessions() {
    if (QCoreApplication::instance()->property("sdrtown.guiDryRun").toBool()) return;
    QStringList ids;
    for (int i = 3; i < tabs_->count(); ++i)
        ids.append(tabs_->widget(i)->objectName().mid(QString("inmarsatSession.").size()));
    QSettings().setValue("inmarsat/openSessions", ids);
}

QJsonObject SatcomHubWidget::controlInmarsatSessions(const QString& method, const QJsonObject& body) {
    ensureTabs();
    const auto report = [this] {
        QJsonArray result;
        for (int i = 0; i < tabs_->count(); ++i) {
            if (auto* widget = qobject_cast<InmarsatWidget*>(tabs_->widget(i))) {
                auto item = widget->engine().statusJson();
                // Status polling doesn't serialize high-rate display arrays.
                item.erase("spectrumDb"); item.erase("recentLines");
                result.append(QJsonDocument::fromJson(QByteArray::fromStdString(item.dump())).object());
            }
        }
        return QJsonObject{{"ok", true}, {"sessions", result}};
    };
    if (method == "GET") return report();
    if (method != "POST") return {{"ok",false},{"status",405},{"error","GET or POST required"}};
    if (!body.value("sessionId").isString() || body.value("sessionId").toString().isEmpty())
        return {{"ok",false},{"status",400},{"error","A named sessionId is required"}};
    try {
        const auto id = QString::fromStdString(InmarsatEngine::normalizedSessionId(body.value("sessionId").toString().toStdString()));
        const auto action = body.value("action").toString("open");
        if (!QStringList{"open","close","start","stop","configure"}.contains(action))
            return {{"ok",false},{"status",400},{"error","Unknown session action"}};
        InmarsatWidget* widget = nullptr;
        for (int i = 3; i < tabs_->count(); ++i)
            if (tabs_->widget(i)->objectName() == "inmarsatSession." + id)
                widget = qobject_cast<InmarsatWidget*>(tabs_->widget(i));
        if (action == "open") { openInmarsatSession(id); return report(); }
        if (!widget) return {{"ok",false},{"status",409},{"error","Session is not open"}};
        auto& engine = widget->engine();
        if (action == "close") {
            tabs_->removeTab(tabs_->indexOf(widget)); delete widget; saveInmarsatSessions();
        } else if (action == "stop") engine.stop();
        else if (action == "start") {
            if (QCoreApplication::instance()->property("sdrtown.guiDryRun").toBool())
                return {{"ok",false},{"status",409},{"error","RF disabled in GUI dry-run"}};
            if (!engine.start()) return {{"ok",false},{"status",409},{"error",QString::fromStdString(engine.snapshot().lastStatus)}};
        } else {
            if (engine.snapshot().state != InmarsatEngineState::Idle)
                return {{"ok",false},{"status",409},{"error","Stop this session before configuring it"}};
            if (!body.value("config").isObject())
                return {{"ok",false},{"status",400},{"error","config object required"}};
            const auto edit = body.value("config").toObject();
            for (auto it = edit.begin(); it != edit.end(); ++it)
                if (!QStringList{"deviceStableKey","channelHz","mode","baud","playAudio","watch"}.contains(it.key()))
                    return {{"ok",false},{"status",400},{"error","Unsupported session setting"}};
            auto json = engine.config().toJson();
            json.update(nlohmann::json::parse(QJsonDocument(edit).toJson(QJsonDocument::Compact).toStdString()));
            const auto config = InmarsatEngineConfig::fromJson(json);
            if (!std::isfinite(config.channelHz) || config.channelHz <= 0 ||
                !QStringList{"aero_msk","aero_oqpsk","aero_voice","aero_burst","egc"}.contains(QString::fromStdString(config.mode)) ||
                (config.baud != 600 && config.baud != 1200 && config.baud != 8400 && config.baud != 10500))
                return {{"ok",false},{"status",400},{"error","Invalid frequency, mode or bit rate"}};
            if (!engine.setConfig(config))
                return {{"ok",false},{"status",400},{"error",QString::fromStdString(engine.snapshot().lastStatus)}};
            widget->reloadSessionControls();
        }
        return report();
    } catch (const std::exception&) {
        return {{"ok",false},{"status",400},{"error","Invalid session request or unavailable session"}};
    }
}

void SatcomHubWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    ensureTabs();
}

void SatcomHubWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    // DEC-0185: workspace visibility is not a receiver stop command. Child
    // widgets pause their rendering timers; explicit Stop/shutdown owns RF.
    spdlog::info("Satellite workspace hidden; receiver ownership and capture continue unchanged.");
}

void SatcomHubWidget::stopAutoCapture(bool keepHandledKey) {
    auto& engine = SatcomScannerEngine::instance();
    engine.stopRecording();
    engine.disarmPass();
    if (autoEngineWasRunning_) engine.skip();
    else engine.stop();

    // SatcomScannerEngine owns the single authoritative device/session restore.
    // Do not retune a second time here: the host callback reactivates Listen only
    // after the original hardware centre and stream state are restored.

    autoCaptureOwned_ = false;
    autoEngineWasRunning_ = false;
    autoDeviceIndex_ = static_cast<size_t>(-1);
    autoPreviousCenterHz_ = 0.0;
    autoPreviousLeaseOwner_ = 0;
    if (!keepHandledKey) autoCapturePassKey_.clear();
}

void SatcomHubWidget::autoCaptureTick() {
    if (!automaticCaptureAllowed_) return;
    auto& engine = SatcomScannerEngine::instance();
    if (!engine.autoCaptureEnabled()) {
        if (autoCaptureOwned_) stopAutoCapture(false);
        return;
    }

    const auto plan = SatPassPlanner::instance().snapshot();
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    bool handledStillInRange = false;
    for (const auto& position : plan.positions) {
        const QString key = QString::fromStdString(position.satId + "/" + position.downlinkId);
        if (position.tleValid && position.inRange && key == autoCapturePassKey_) {
            handledStillInRange = true;
            break;
        }
    }
    if (!handledStillInRange && !plan.armed.armed && !autoCaptureOwned_)
        autoCapturePassKey_.clear();

    if (plan.armed.armed) {
        if (!autoCaptureOwned_) return; // never replace a manual arm
        const auto scan = engine.snapshot();
        const bool signalPresent = std::isfinite(scan.audioRmsDb) &&
                                   scan.audioRmsDb >= scan.config.squelchDb;
        if (signalPresent && scan.state != SatcomScannerState::Recording)
            engine.startRecording();
        return;
    }

    if (autoCaptureOwned_) {
        stopAutoCapture(true);
        return;
    }
    if (now < autoCaptureRetryAfter_) return;

    const SatCurrentPosition* best = nullptr;
    double bestScore = -std::numeric_limits<double>::infinity();
    for (const auto& position : plan.positions) {
        if (!position.tleValid || !position.inRange || !position.armable ||
            position.downlinkId.empty() || position.freqHz <= 0.0) {
            continue;
        }
        const QString key = QString::fromStdString(position.satId + "/" + position.downlinkId);
        if (key == autoCapturePassKey_) continue;
        const double score = 10000.0 - 1000.0 * autoCapturePriority(position.role) +
                             position.elevationDeg;
        if (!best || score > bestScore) {
            best = &position;
            bestScore = score;
        }
    }
    if (!best) return;

    auto& manager = DeviceManager::instance();
    std::string deviceError;
    const size_t deviceIndex = engine.resolveDeviceIndex(&deviceError);
    if (deviceIndex == static_cast<size_t>(-1)) {
        autoCaptureRetryAfter_ = now + 15;
        return;
    }

    const auto currentOwner = manager.deviceLeaseOwner(deviceIndex);
    if (!manager.canUseDevice(deviceIndex, DeviceManager::DeviceLeaseOwner::Satcom)) {
        autoCaptureRetryAfter_ = now + 10;
        return;
    }

    const auto before = engine.snapshot();
    autoEngineWasRunning_ = before.state != SatcomScannerState::Idle;
    autoDeviceIndex_ = deviceIndex;
    autoPreviousCenterHz_ = manager.getCurrentCenterFreq(deviceIndex);
    autoPreviousLeaseOwner_ = static_cast<int>(currentOwner);

    std::string error;
    if (!engine.armPass(best->satId, best->downlinkId, true, true, &error)) {
        autoEngineWasRunning_ = false;
        autoDeviceIndex_ = static_cast<size_t>(-1);
        autoPreviousCenterHz_ = 0.0;
        autoPreviousLeaseOwner_ = 0;
        autoCaptureRetryAfter_ = now + 15;
        return;
    }

    autoCaptureOwned_ = true;
    autoCapturePassKey_ = QString::fromStdString(best->satId + "/" + best->downlinkId);
}

void SatcomHubWidget::showSatcomTab() {
    ensureTabs();
    tabs_->setCurrentWidget(satcom_);
}

void SatcomHubWidget::showInmarsatTab() {
    ensureTabs();
    tabs_->setCurrentWidget(inmarsat_);
}

void SatcomHubWidget::showAircraftTab() {
    ensureTabs();
    tabs_->setCurrentWidget(aircraft_);
}
