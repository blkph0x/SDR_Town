#include "frontend/EquipmentWizard.h"

#include "frontend/FrontEndPower.h"
#include "frontend/LinkBudgetHint.h"
#include "frontend/PassArming.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
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
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

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
    rfMHz_ = new QDoubleSpinBox;
    rfMHz_->setObjectName("trueRfMHz");
    rfMHz_->setRange(0, 40000);
    rfMHz_->setDecimals(6);
    rfMHz_->setValue(11700);
    rfMHz_->setSuffix(" MHz");
    mission_ = new QComboBox;
    mission_->setObjectName("stationMission");
    mission_->addItem("LEO track");
    mission_->addItem("GEO park");
    mission_->addItem("GEO box scan");
    mission_->addItem("Manual");
    horizontal_ = new QCheckBox("Horizontal / right (18 V)");
    horizontal_->setObjectName("horizontalPol");
    highBand_ = new QCheckBox("High band (22 kHz)");
    highBand_->setObjectName("highBand");
    follow_ = new QCheckBox("Follow armed pass");
    follow_->setObjectName("followPlanner");
    sky_ = new QLabel("No planner sample yet");
    sky_->setObjectName("skyFeed");
    sky_->setWordWrap(true);
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
    form->addRow("True RF", rfMHz_);
    form->addRow("Mission", mission_);
    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(caution_);
    root->addWidget(hint_);
    root->addWidget(sky_);
    root->addWidget(horizontal_);
    root->addWidget(highBand_);
    root->addWidget(follow_);
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
    auto* readButton = new QPushButton("Read armed pass");
    readButton->setObjectName("readPlanner");
    auto* saveButton = new QPushButton("Save pass log");
    saveButton->setObjectName("savePassLog");
    buttons->addWidget(connectButton);
    buttons->addWidget(readButton);
    buttons->addWidget(armButton);
    buttons->addWidget(abortButton);
    buttons->addWidget(saveButton);
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
        else {
            const StationProfile armed = profile();
            const QString pol = armed.horizontal ? "18 V horizontal/right" : "13 V vertical/left";
            const QString tone = armed.highBand ? ", 22 kHz" : "";
            plan_->setPlainText(QString("Pass armed. IF %1 MHz, %2%3. Bias-T command is not a measured voltage.")
                                    .arg(session_->tunedIfHz() / 1e6, 0, 'f', 3)
                                    .arg(pol, tone));
        }
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
    connect(readButton, &QPushButton::clicked, this, [this] { applySky(false); });
    connect(saveButton, &QPushButton::clicked, this, [this] {
        QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (root.isEmpty()) root = QDir::tempPath();
        const QString dir = root + "/station-passes/" +
            QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss");
        std::string error;
        if (!writePassFolder(dir.toStdString(), profile(), session_->metrics(), &error))
            sky_->setText(QString::fromStdString(error));
        else
            sky_->setText("Pass log: " + dir);
    });
    followTimer_ = new QTimer(this);
    followTimer_->setInterval(1000);
    connect(follow_, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            applySky(true);
            followTimer_->start();
        } else {
            followTimer_->stop();
        }
    });
    connect(followTimer_, &QTimer::timeout, this, [this] { applySky(true); });
    connect(mission_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        refreshHint(predictEl_->value());
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
    profile.trueRfHz = rfMHz_->value() * 1e6;
    profile.horizontal = horizontal_->isChecked();
    profile.highBand = highBand_->isChecked();
    switch (mission_->currentIndex()) {
    case 1: profile.mission = StationMission::GeoPark; break;
    case 2: profile.mission = StationMission::GeoBoxScan; break;
    case 3: profile.mission = StationMission::Manual; break;
    default: profile.mission = StationMission::LeoTrack; break;
    }
    profile.biasBackend = BiasBackend::External;
    return profile;
}

void EquipmentWizard::setProfile(const StationProfile& profile) {
    dish_->setValue(profile.dishCm);
    nf_->setValue(profile.lnb.noiseFigureDb);
    supplyMa_->setValue(profile.biasSupplyMa);
    lnbMa_->setValue(profile.lnb.maxCurrentMa);
    rfMHz_->setValue(profile.trueRfHz / 1e6);
    horizontal_->setChecked(profile.horizontal);
    highBand_->setChecked(profile.highBand);
    mission_->setCurrentIndex(profile.mission == StationMission::GeoPark ? 1 :
                              profile.mission == StationMission::GeoBoxScan ? 2 :
                              profile.mission == StationMission::Manual ? 3 : 0);
    syncCaution();
}

void EquipmentWizard::applySky(bool fromFollow) {
    const StationProfile current = profile();
    const SkyFeed feed = readArmedSatellite(current.minElevationDeg, current.tleMaxAgeHours);
    if (!feed.accepted) {
        sky_->setText(QString::fromStdString(feed.reject));
        tle_->setChecked(false);
        if (fromFollow && session_->tracking()) {
            if (feed.reject == "No armed satellite pass")
                session_->abort("planner-disarmed");
            else if (feed.reject == "TLE age is unknown" ||
                     feed.reject == "TLE is older than the station limit")
                session_->abort("stale-tle");
            else if (feed.reject == "Armed pass is below the elevation mask")
                session_->setJogPaused(true);
        }
        return;
    }
    if (session_->tracking()) session_->setJogPaused(false);
    predictAz_->blockSignals(true);
    predictEl_->blockSignals(true);
    rfMHz_->blockSignals(true);
    predictAz_->setValue(std::clamp(feed.azimuthDeg, 0.0, 360.0));
    predictEl_->setValue(std::clamp(feed.elevationDeg, 0.0, 90.0));
    rfMHz_->setValue(feed.trueRfHz / 1e6);
    predictAz_->blockSignals(false);
    predictEl_->blockSignals(false);
    rfMHz_->blockSignals(false);
    session_->noteSky(feed.azimuthDeg, feed.elevationDeg, feed.trueRfHz, feed.dopplerHz);
    tle_->setChecked(feed.tleFresh);
    sky_->setText(QString("Planner AZ %1 EL %2, Doppler-corrected RF %3 MHz (%4 Hz)")
                      .arg(feed.azimuthDeg, 0, 'f', 2)
                      .arg(feed.elevationDeg, 0, 'f', 2)
                      .arg(feed.trueRfHz / 1e6, 0, 'f', 6)
                      .arg(feed.dopplerHz, 0, 'f', 1));
    refreshHint(feed.elevationDeg, false);
}

void EquipmentWizard::refreshHint(double elevationDeg, bool updatePlan) {
    const auto hint = linkMarginHint(dish_->value(), nf_->value(), elevationDeg, 10.0);
    hint_->setText(QString("Margin hint: %1 (qualitative, not a measured C/N)").arg(linkMarginHintName(hint)));
    if (!updatePlan) return;
    const auto arm = planPassArm(checklist(), profile().mission);
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
