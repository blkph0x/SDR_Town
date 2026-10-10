#pragma once

#include "frontend/StationProfile.h"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;

// DEC-0210 operator panel. Power stays off until the confirm box is checked.
class EquipmentWizard : public QDialog {
public:
    explicit EquipmentWizard(QWidget* parent = nullptr);
    StationProfile profile() const;
    void setProfile(const StationProfile& profile);
    void refreshHint(double elevationDeg);

private:
    void syncCaution();
    QDoubleSpinBox* dish_ = nullptr;
    QDoubleSpinBox* nf_ = nullptr;
    QDoubleSpinBox* supplyMa_ = nullptr;
    QDoubleSpinBox* lnbMa_ = nullptr;
    QCheckBox* confirm_ = nullptr;
    QLabel* caution_ = nullptr;
    QLabel* hint_ = nullptr;
    QLabel* power_ = nullptr;
    QPlainTextEdit* plan_ = nullptr;
};

void installEquipmentWizardMenu(class QMainWindow& window);
