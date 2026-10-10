#include "frontend/StationPassSession.h"

#include "frontend/LnbConversion.h"
#include "frontend/RotatorTrack.h"

#include <cmath>

StationPassSession::StationPassSession(RotatorController& rotor, QObject* parent)
    : QObject(parent), rotor_(rotor) {
    timer_.setInterval(1000);
    connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
    connect(&rotor_, &RotatorController::position, this, [this](double az, double el) {
        reportedAz_ = az;
        reportedEl_ = el;
        haveReport_ = true;
        metrics_.add("rotator.reportedAz", az);
        metrics_.add("rotator.reportedEl", el);
        if (tracking_) metrics_.add("rotator.errorAz", predictAz_ - az);
        if (parking_) finish("park-readback");
    });
    connect(&rotor_, &RotatorController::stopped, this, [this] {
        if (aborting_) beginPark();
    });
    connect(&rotor_, &RotatorController::state, this, [this](const QString& text) {
        metrics_.addText("rotator.state", text.toStdString());
        if (aborting_ && !finishedAbort_ && text.contains("No controller")) finish("no-controller");
    });
}

bool StationPassSession::arm(const PassChecklist& check, const StationProfile& profile,
                             double predictAz, double predictEl, std::string* error) {
    if (passActive_ || aborting_) {
        if (error) *error = "A pass is already active";
        return false;
    }
    const auto plan = planPassArm(check, profile.mission);
    if (!plan.accepted) {
        if (error) *error = plan.reject;
        metrics_.addText("arm.reject", plan.reject);
        return false;
    }
    BoxScan box;
    if (profile.mission == StationMission::GeoBoxScan) {
        box = planBoxScan(predictAz, predictEl, profile.boxSpanAzDeg, profile.boxSpanElDeg, profile.boxStepDeg);
        if (!box.accepted) {
            if (error) *error = box.reject;
            metrics_.addText("arm.reject", box.reject);
            return false;
        }
    }
    profile_ = profile;
    predictAz_ = predictAz;
    predictEl_ = predictEl;
    finishedAbort_ = false;
    boxScan_ = false;
    boxAz_.clear();
    boxEl_.clear();
    boxIndex_ = 0;
    std::string powerError;
    if (!power_.selectBackend(profile.biasBackend, &powerError)) {
        if (error) *error = powerError;
        metrics_.addText("arm.reject", powerError);
        return false;
    }
    const auto tune = lnbTune(profile.lnb, profile.trueRfHz, profile.horizontal, profile.highBand);
    metrics_.add("lnb.nfDb", profile.lnb.noiseFigureDb, "claimed");
    if (!tune.ok) {
        if (error) *error = tune.error;
        metrics_.addText("arm.reject", tune.error);
        return false;
    }
    if (!power_.enable(check.powerConfirmed, tune.voltageV, tune.tone22kHz, &powerError)) {
        if (error) *error = powerError;
        metrics_.addText("arm.reject", powerError);
        return false;
    }
    metrics_.add("lnb.ifHz", tune.ifHz);
    metrics_.add("lnb.voltageV", tune.voltageV);
    tunedIfHz_ = tune.ifHz;
    for (const auto& step : plan.steps) metrics_.addText("arm.step", step);
    passActive_ = true;
    tracking_ = profile.mission == StationMission::LeoTrack;
    if (profile.mission == StationMission::GeoBoxScan) {
        boxAz_ = box.azimuthDeg;
        boxEl_ = box.elevationDeg;
        boxScan_ = true;
        timer_.start();
    }
    if (tracking_ || boxScan_) tick();
    else if (profile.mission == StationMission::GeoPark)
        commandLook(predictAz_, predictEl_, "slew-park");
    if (error) error->clear();
    return true;
}

void StationPassSession::setPrediction(double az, double el) {
    predictAz_ = az;
    predictEl_ = el;
}

void StationPassSession::noteSky(double az, double el, double trueRfHz, double dopplerHz) {
    setPrediction(az, el);
    if (std::isfinite(trueRfHz)) {
        profile_.trueRfHz = trueRfHz;
        metrics_.add("sky.trueRfHz", trueRfHz);
    }
    if (std::isfinite(dopplerHz)) metrics_.add("sky.dopplerHz", dopplerHz);
}

void StationPassSession::setJogPaused(bool paused) { jogPaused_ = paused; }

bool StationPassSession::commandLook(double az, double el, const char* label) {
    TrackLimits limits;
    const auto point = planTrackTick(rotor_.armed(), rotor_.fresh(), true, jogPaused_, limits,
                                     az, el, profile_.backlashDeg,
                                     haveReport_ ? reportedAz_ : az);
    metrics_.addText(label, point.reason);
    if (!point.send) return false;
    metrics_.add("rotator.commandAz", point.az);
    metrics_.add("rotator.commandEl", point.el);
    rotor_.moveTo(point.az, point.el);
    return true;
}

void StationPassSession::tick() {
    if (boxScan_) {
        if (boxIndex_ >= boxAz_.size()) {
            timer_.stop();
            boxScan_ = false;
            metrics_.addText("box-scan", "complete");
            return;
        }
        if (!commandLook(boxAz_[boxIndex_], boxEl_[boxIndex_], "box-scan")) return;
        ++boxIndex_;
        if (boxIndex_ >= boxAz_.size()) {
            timer_.stop();
            boxScan_ = false;
            metrics_.addText("box-scan", "complete");
        }
        return;
    }
    if (!tracking_) return;
    commandLook(predictAz_, predictEl_, "track");
}

void StationPassSession::abort(const std::string& reason) {
    if (finishedAbort_) return;
    tracking_ = false;
    boxScan_ = false;
    timer_.stop();
    aborting_ = true;
    metrics_.addText("stop-worker", reason.empty() ? "abort" : reason);
    if (!rotor_.connected()) finish("no-controller");
    else rotor_.stop();
}

void StationPassSession::beginPark() {
    if (finishedAbort_ || parking_) return;
    metrics_.addText("rotator-stop", "commanded");
    if (rotor_.arm() && rotor_.moveTo(profile_.parkAz, profile_.parkEl)) {
        parking_ = true;
        metrics_.add("rotator.parkAz", profile_.parkAz);
        metrics_.add("rotator.parkEl", profile_.parkEl);
        return;
    }
    finish("park-blocked");
}

void StationPassSession::finish(const std::string& reason) {
    if (finishedAbort_) return;
    finishedAbort_ = true;
    aborting_ = false;
    parking_ = false;
    tracking_ = false;
    boxScan_ = false;
    passActive_ = false;
    timer_.stop();
    power_.disable(reason);
    metrics_.addText("power-off", reason);
    metrics_.addText("pass-summary", reason);
}
