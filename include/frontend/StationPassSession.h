#pragma once

#include "AntennaControl.h"
#include "frontend/FrontEndMetrics.h"
#include "frontend/FrontEndPower.h"
#include "frontend/PassArming.h"
#include "frontend/StationProfile.h"

#include <QObject>
#include <QTimer>

#include <string>

// DEC-0210: one existing RotatorController, 1 Hz planned moves, fail-closed arm.
// Stop acknowledgement is not proof the motor is still. Park is the saved
// absolute target after stop, then Bias-T is commanded off.
class StationPassSession : public QObject {
    Q_OBJECT
public:
    explicit StationPassSession(RotatorController& rotor, QObject* parent = nullptr);
    bool arm(const PassChecklist& check, const StationProfile& profile,
             double predictAz, double predictEl, std::string* error);
    void setPrediction(double az, double el);
    void setJogPaused(bool paused);
    void abort(const std::string& reason);
    bool tracking() const { return tracking_; }
    const FrontEndMetrics& metrics() const { return metrics_; }
    const FrontEndPower& power() const { return power_; }

private:
    void tick();
    void beginPark();
    void finish(const std::string& reason);
    RotatorController& rotor_;
    FrontEndPower power_;
    FrontEndMetrics metrics_;
    StationProfile profile_{};
    QTimer timer_;
    bool tracking_ = false;
    bool jogPaused_ = false;
    bool aborting_ = false;
    bool parking_ = false;
    bool finishedAbort_ = false;
    double predictAz_ = 0.0;
    double predictEl_ = 0.0;
    double reportedAz_ = 0.0;
    double reportedEl_ = 0.0;
    bool haveReport_ = false;
};
