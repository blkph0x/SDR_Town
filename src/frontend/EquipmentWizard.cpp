#include "frontend/EquipmentWizard.h"

#include "frontend/FrontEndPower.h"
#include "frontend/LinkBudgetHint.h"
#include "frontend/PassArming.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QVBoxLayout>

EquipmentWizard::EquipmentWizard(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Station front-end");
    setObjectName("equipmentWizard");
    auto* form = new QFormLayout;
    dish_ = new QDoubleSpinBox; dish_->setObjectName("dishCm"); dish_->setRange(0, 2000); dish_->setSuffix(" cm");
    nf_ = new QDoubleSpinBox; nf_->setObjectName("lnbNf"); nf_->setRange(0, 5); nf_->setDecimals(2); nf_->setValue(0.7); nf_->setSuffix(" dB");
    supplyMa_ = new QDoubleSpinBox; supplyMa_->setObjectName("biasSupplyMa"); supplyMa_->setRange(0, 2000); supplyMa_->setValue(500); supplyMa_->setSuffix(" mA");
    lnbMa_ = new QDoubleSpinBox; lnbMa_->setObjectName("lnbMaxMa"); lnbMa_->setRange(0, 2000); lnbMa_->setValue(200); lnbMa_->setSuffix(" mA");
    confirm_ = new QCheckBox("I confirm Bias-T may be enabled for this pass");
    confirm_->setObjectName("biasConfirm");
    caution_ = new QLabel; caution_->setObjectName("nfCaution"); caution_->setWordWrap(true);
    hint_ = new QLabel; hint_->setObjectName("linkHint");
    power_ = new QLabel("Bias-T: OFF"); power_->setObjectName("biasState");
    plan_ = new QPlainTextEdit; plan_->setObjectName("armPlan"); plan_->setReadOnly(true);
    form->addRow("Dish", dish_);
    form->addRow("Claimed LNB NF", nf_);
    form->addRow("Bias-T rating", supplyMa_);
    form->addRow("LNB max draw", lnbMa_);
    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(caution_);
    root->addWidget(hint_);
    root->addWidget(confirm_);
    root->addWidget(power_);
    root->addWidget(plan_);
    connect(nf_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { syncCaution(); });
    connect(confirm_, &QCheckBox::toggled, this, [this](bool) {
        power_->setText(confirm_->isChecked() ? "Bias-T: confirmation armed, still OFF until pass arm" : "Bias-T: OFF");
        refreshHint(30.0);
    });
    syncCaution();
    refreshHint(30.0);
}

void EquipmentWizard::syncCaution() {
    if (claimedNoiseFigureNeedsCaution(nf_->value()))
        caution_->setText("Claimed NF below 0.2 dB is stored as a manufacturer claim, not a measured G/T.");
    else
        caution_->setText("Ku claims are commonly 0.2-0.8 dB. C-band is usually given in kelvin.");
}

StationProfile EquipmentWizard::profile() const {
    StationProfile profile;
    profile.dishCm = dish_->value();
    profile.biasSupplyMa = supplyMa_->value();
    profile.lnb.noiseFigureDb = nf_->value();
    profile.lnb.maxCurrentMa = lnbMa_->value();
    profile.biasBackend = BiasBackend::External;
    profile.mission = StationMission::LeoTrack;
    return profile;
}

void EquipmentWizard::setProfile(const StationProfile& profile) {
    dish_->setValue(profile.dishCm);
    nf_->setValue(profile.lnb.noiseFigureDb);
    supplyMa_->setValue(profile.biasSupplyMa);
    lnbMa_->setValue(profile.lnb.maxCurrentMa);
    syncCaution();
}

void EquipmentWizard::refreshHint(double elevationDeg) {
    const auto hint = linkMarginHint(dish_->value(), nf_->value(), elevationDeg, 10.0);
    hint_->setText(QString("Margin hint: %1 (qualitative, not a measured C/N)").arg(linkMarginHintName(hint)));
    PassChecklist check;
    check.leaseHeld = true;
    check.ifInSdrSpan = true;
    check.tleFreshOrNotRequired = true;
    check.biasCurrentOk = biasRatingCoversLnb(supplyMa_->value(), lnbMa_->value());
    check.rotatorReadyOrOverride = true;
    check.powerConfirmed = confirm_->isChecked();
    const auto arm = planPassArm(check, StationMission::LeoTrack);
    plan_->setPlainText(arm.accepted ? QString::fromStdString("Arm: " + arm.steps.front()) : QString::fromStdString(arm.reject));
}

void installEquipmentWizardMenu(QMainWindow& window) {
    QMenu* menu = nullptr;
    for (auto* action : window.menuBar()->actions())
        if (action->menu() && action->text().remove('&') == "Tools") { menu = action->menu(); break; }
    if (!menu) menu = window.menuBar()->addMenu("&Tools");
    auto* panel = new EquipmentWizard(&window);
    menu->addAction("Station front-end...", panel, [panel] { panel->show(); panel->raise(); panel->activateWindow(); });
}
