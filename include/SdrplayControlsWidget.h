#pragma once

#include "SdrplayControl.h"
#include <QGroupBox>
#include <functional>
#include <map>

struct DeviceInfo;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;
class QFormLayout;

class SdrplayControlsWidget : public QGroupBox {
public:
    explicit SdrplayControlsWidget(QWidget* parent = nullptr);
    void setDevice(const DeviceInfo* device, const QString& runtime = {});
    std::function<bool(const SdrplayControl::Change&, std::string&)> apply;
    std::function<void()> changed;
    QFormLayout* form() const { return form_; }
private:
    void submit(const SdrplayControl::Change& change);
    QFormLayout* form_;
    QLabel* info_;
    QLabel* status_;
    QComboBox* antenna_;
    QCheckBox* agc_;
    QDoubleSpinBox* ifgr_;
    QDoubleSpinBox* rfgr_;
    QComboBox* bandwidth_;
    QComboBox* rfSelect_;
    QSpinBox* setpoint_;
    std::map<std::string, QCheckBox*> settings_;
    bool refreshing_ = false;
    bool forceValues_ = false;
};
