#include "SatcomHubWidget.h"
#include "SatcomScannerWidget.h"
#include "SatcomScannerEngine.h"
#include "SatPassPlanner.h"
#include "DeviceManager.h"
#include "InmarsatWidget.h"
#include "InmarsatEngine.h"
#include "AircraftMapWidget.h"

#include <QDateTime>
#include <QHideEvent>
#include <QShowEvent>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

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
    root->addWidget(tabs_);

    // A lightweight controller runs even before the visual tabs are created.
    // It only starts hardware when a selected, supported satellite is in range.
    autoCaptureTimer_ = new QTimer(this);
    autoCaptureTimer_->setInterval(1000);
    connect(autoCaptureTimer_, &QTimer::timeout, this, &SatcomHubWidget::autoCaptureTick);
    autoCaptureTimer_->start();
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

    connect(satcom_, &SatcomScannerWidget::requestOpenSstvLive,
            this, &SatcomHubWidget::requestOpenSstvLive);
}

void SatcomHubWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    ensureTabs();
}

void SatcomHubWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    auto& engine = SatcomScannerEngine::instance();
    const auto snap = engine.snapshot();
    // Keep an armed manual or automatic pass alive in the background. Ordinary
    // band scanning still stops when the hub is hidden.
    if (!autoCaptureOwned_ && !snap.passArmed) {
        engine.stopRecording();
        engine.stop();
        engine.disarmPass();
    }
    InmarsatEngine::instance().stop();
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
    const auto currentOwner = manager.deviceLeaseOwner();
    if (currentOwner == DeviceManager::DeviceLeaseOwner::P25 ||
        currentOwner == DeviceManager::DeviceLeaseOwner::Inmarsat ||
        currentOwner == DeviceManager::DeviceLeaseOwner::Aircraft) {
        autoCaptureRetryAfter_ = now + 10;
        return;
    }

    std::string deviceError;
    const size_t deviceIndex = engine.resolveDeviceIndex(&deviceError);
    if (deviceIndex == static_cast<size_t>(-1)) {
        autoCaptureRetryAfter_ = now + 15;
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
