#include "frontend/EquipmentWizard.h"

#include "frontend/FrontEndPower.h"
#include "frontend/LinkBudgetHint.h"
#include "frontend/PassArming.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
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
    lease_ = new QCheckBox("Radio lease is held for this pass");
    lease_->setObjectName("attestLease");
    ifSpan_ = new QCheckBox("Corrected IF is inside the SDR span");
    ifSpan_->setObjectName("attestIf");
    tle_ = new QCheckBox("TLE is inside the station age limit");
    tle_->setObjectName("attestTle");
    rotorOverride_ = new QCheckBox("Override fresh rotator feedback for this pass");
    rotorOverride_->setObjectName("rotorOverride");
    host_ = new QLineEdit("127.0.0.1");
    host_->setObjectName("rotatorHost");
    port_ = new QSpinBox;
    port_->setObjectName("rotatorPort");
    port_->setRange(1, 65535);
    port_->setValue(4533);
    predictAz_ = new QDoubleSpinBox;
    predictAz_->setObjectName("predictAz");
    predictAz_->setRange(0, 360);
    predictAz_->setValue(180);
    predictEl_ = new QDoubleSpinBox;
    predictEl_->setObjectName("predictEl");
    predictEl_->setRange(0, 90);
    predictEl_->setValue(20);
    caution_ = new QLabel; caution_->setObjectName("nfCaution"); caution_->setWordWrap(true);
    hint_ = new QLabel; hint_->setObjectName("linkHint");
    power_ = new QLabel("Bias-T: OFF"); power_->setObjectName("biasState");
    plan_ = new QPlainTextEdit; plan_->setObjectName("armPlan"); plan_->setReadOnly(true);
    form->addRow("Dish", dish_);
    form->addRow("Claimed LNB NF", nf_);
    form->addRow("Bias-T rating", supplyMa_);
    form->addRow("LNB max draw", lnbMa_);
    form->addRow("rotctld host", host_);
    form->addRow("rotctld port", port_);
    form->addRow("Predicted AZ", predictAz_);
    form->addRow("Predicted EL", predictEl_);
    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(caution_);
    root->addWidget(hint_);
    root->addWidget(lease_);
    root->addWidget(ifSpan_);
    root->addWidget(tle_);
    root->addWidget(rotorOverride_);
    root->addWidget(confirm_);
    root->addWidget(power_);
    auto* buttons = new QHBoxLayout;
    auto* connectButton = new QPushButton("Connect rotator");
    connectButton->setObjectName("connectRotator");
    auto* armButton = new QPushButton("Arm pass");
    armButton->setObjectName("armPass");
    auto* abortButton = new QPushButton("Abort");
    abortButton->setObjectName("abortPass");
    buttons->addWidget(connectButton);
    buttons->addWidget(armButton);
    buttons->addWidget(abortButton);
    root->addLayout(buttons);
    root->addWidget(plan_);
    session_ = new StationPassSession(rotor_, this);
    connect(nf_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { syncCaution(); });
    connect(confirm_, &QCheckBox::toggled, this, [this](bool) {
        power_->setText(confirm_->isChecked() ? "Bias-T: confirmation armed, still OFF until pass arm" : "Bias-T: OFF");
        refreshHint(predictEl_->value());
    });
    connect(connectButton, &QPushButton::clicked, this, [this] {
        RotorLimits limits;
        rotor_.connectTo(host_->text(), static_cast<quint16>(port_->value()), limits);
    });
    connect(armButton, &QPushButton::clicked, this, [this] {
        std::string error;
        if (!session_->arm(checklist(), profile(), predictAz_->value(), predictEl_->value(), &error))
            plan_->setPlainText(QString::fromStdString(error));
        else
            plan_->setPlainText("Pass armed. Bias-T command is not a measured voltage.");
        power_->setText(session_->power().enabled() ? "Bias-T: commanded ON" : "Bias-T: OFF");
    });
    connect(abortButton, &QPushButton::clicked, this, [this] {
        session_->abort("operator");
        power_->setText(session_->power().enabled() ? "Bias-T: commanded ON until park completes" : "Bias-T: OFF");
        plan_->setPlainText("Abort commanded stop, then park, then Bias-T off. A stop reply is not a physical stop.");
    });
    connect(predictAz_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double az) {
        session_->setPrediction(az, predictEl_->value());
    });
    connect(predictEl_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double el) {
        session_->setPrediction(predictAz_->value(), el);
        refreshHint(el);
    });
    syncCaution();
    refreshHint(30.0);
}

PassChecklist EquipmentWizard::checklist() const {
    PassChecklist check;
    check.leaseHeld = lease_->isChecked();
    check.ifInSdrSpan = ifSpan_->isChecked();
    check.tleFreshOrNotRequired = tle_->isChecked();
    check.biasCurrentOk = biasRatingCoversLnb(supplyMa_->value(), lnbMa_->value());
    check.rotatorReadyOrOverride = rotorOverride_->isChecked() || (rotor_.armed() && rotor_.fresh());
    check.powerConfirmed = confirm_->isChecked();
    return check;
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
    const auto arm = planPassArm(checklist(), StationMission::LeoTrack);
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
