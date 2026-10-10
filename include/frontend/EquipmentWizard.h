#pragma once

#include "frontend/StationPassSession.h"
#include "frontend/StationProfile.h"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

// DEC-0210 operator panel. Power stays off until the confirm box is checked.
class EquipmentWizard : public QDialog {
public:
    explicit EquipmentWizard(QWidget* parent = nullptr);
    StationProfile profile() const;
    void setProfile(const StationProfile& profile);
    void refreshHint(double elevationDeg);

private:
    void syncCaution();
    PassChecklist checklist() const;
    QDoubleSpinBox* dish_ = nullptr;
    QDoubleSpinBox* nf_ = nullptr;
    QDoubleSpinBox* supplyMa_ = nullptr;
    QDoubleSpinBox* lnbMa_ = nullptr;
    QCheckBox* confirm_ = nullptr;
    QCheckBox* lease_ = nullptr;
    QCheckBox* ifSpan_ = nullptr;
    QCheckBox* tle_ = nullptr;
    QCheckBox* rotorOverride_ = nullptr;
    QLineEdit* host_ = nullptr;
    QSpinBox* port_ = nullptr;
    QDoubleSpinBox* predictAz_ = nullptr;
    QDoubleSpinBox* predictEl_ = nullptr;
    QLabel* caution_ = nullptr;
    QLabel* hint_ = nullptr;
    QLabel* power_ = nullptr;
    QPlainTextEdit* plan_ = nullptr;
    RotatorController rotor_;
    StationPassSession* session_ = nullptr;
};

void installEquipmentWizardMenu(class QMainWindow& window);
