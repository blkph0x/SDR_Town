#include "SdrplayControlsWidget.h"
#include "DeviceManager.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <algorithm>

using SdrplayControl::Kind;

SdrplayControlsWidget::SdrplayControlsWidget(QWidget* parent)
    : QGroupBox("SDRplay controls", parent), form_(new QFormLayout(this)) {
    info_ = new QLabel; info_->setWordWrap(true); form_->addRow(info_);
    antenna_ = new QComboBox; antenna_->setObjectName("sdrplayAntenna");
    antenna_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    antenna_->setMinimumContentsLength(20); form_->addRow("Antenna input", antenna_);
    agc_ = new QCheckBox("Automatic IF gain"); agc_->setObjectName("sdrplayAgc");
    form_->addRow("AGC", agc_);
    ifgr_ = new QDoubleSpinBox; ifgr_->setObjectName("sdrplayIfgr");
    ifgr_->setDecimals(0); ifgr_->setSuffix(" dB"); ifgr_->setKeyboardTracking(false);
    form_->addRow("IF gain reduction", ifgr_);
    rfgr_ = new QDoubleSpinBox; rfgr_->setObjectName("sdrplayRfgr");
    rfgr_->setDecimals(0); rfgr_->setKeyboardTracking(false);
    rfgr_->setToolTip("LNA gain-reduction state (not dB). Higher means less RF gain; IF AGC stays unchanged.");
    form_->addRow("RF / LNA state", rfgr_);
    bandwidth_ = new QComboBox; bandwidth_->setObjectName("sdrplayBandwidth");
    form_->addRow("Hardware bandwidth", bandwidth_);
    setpoint_ = new QSpinBox; setpoint_->setObjectName("sdrplayAgcSetpoint");
    setpoint_->setRange(-60, 0); setpoint_->setSuffix(" dBFS"); setpoint_->setKeyboardTracking(false);
    form_->addRow("AGC setpoint", setpoint_);
    rfSelect_ = new QComboBox; rfSelect_->setObjectName("sdrplayRfSelect");
    form_->addRow("Driver RF state menu", rfSelect_);
    for (const auto& entry : std::vector<std::pair<std::string, QString>>{
        {SdrplaySettings::kBiasT, "Bias-T power"},
        {SdrplaySettings::kRfNotch, "Broadcast notch"},
        {SdrplaySettings::kDabNotch, "DAB notch"},
        {SdrplaySettings::kExtRef, "Reference clock output"},
        {SdrplaySettings::kHdr, "HDR (RSPdx below 2 MHz)"},
        {SdrplaySettings::kIqCorr, "IQ correction"}}) {
        auto* box = new QCheckBox(entry.second);
        box->setObjectName(QString::fromStdString(entry.first));
        settings_[entry.first] = box;
        form_->addRow(box);
        connect(box, &QCheckBox::toggled, this, [this, key = entry.first](bool on) {
            submit({Kind::Setting, key, SdrplayProfile::boolSetting(on)});
        });
    }
    status_ = new QLabel; status_->setObjectName("sdrplayControlStatus");
    status_->setWordWrap(true); form_->addRow(status_);
    connect(antenna_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (antenna_->currentIndex() >= 0)
            submit({Kind::Antenna, "antenna", antenna_->currentData().toString().toStdString()});
    });
    connect(agc_, &QCheckBox::toggled, this, [this](bool on) { submit({Kind::Agc, "AGC", "", on ? 1.0 : 0.0}); });
    connect(ifgr_, &QDoubleSpinBox::valueChanged, this, [this](double v) { submit({Kind::Gain, "IFGR", "", v}); });
    connect(rfgr_, &QDoubleSpinBox::valueChanged, this, [this](double v) { submit({Kind::Gain, "RFGR", "", v}); });
    connect(bandwidth_, &QComboBox::currentIndexChanged, this, [this](int) {
        submit({Kind::Bandwidth, "bandwidth", "", bandwidth_->currentData().toDouble()});
    });
    connect(setpoint_, &QSpinBox::valueChanged, this, [this](int v) {
        submit({Kind::Setting, SdrplaySettings::kAgcSetpoint, std::to_string(v)});
    });
    connect(rfSelect_, &QComboBox::currentIndexChanged, this, [this](int) {
        submit({Kind::Setting, SdrplaySettings::kRfGainSel, rfSelect_->currentData().toString().toStdString()});
    });
    setDevice(nullptr);
}

void SdrplayControlsWidget::submit(const SdrplayControl::Change& change) {
    if (refreshing_ || !apply) return;
    std::string error;
    const bool ok = apply(change, error);
    forceValues_ = true;
    if (changed) changed(); // Repaint confirmed model, including on failure.
    forceValues_ = false;
    if (!ok) status_->setText(QString::fromStdString("Control failed: " + error));
}

void SdrplayControlsWidget::setDevice(const DeviceInfo* d, const QString& runtime) {
    refreshing_ = true;
    setEnabled(d && d->isSdrplay);
    if (!d || !d->isSdrplay) {
        info_->setText("Select an SDRplay receiver.");
        refreshing_ = false;
        return;
    }
    info_->setText(QString::fromStdString(d->sdrplayModel) + " | " + runtime);
    const bool ready = d->sdrplayProbed && !d->isDiversityComposite;
    const auto has = [&](const std::string& key) {
        return ready && std::find(d->sdrplaySettingKeys.begin(), d->sdrplaySettingKeys.end(), key) != d->sdrplaySettingKeys.end();
    };
    const auto val = [&](const std::string& key, const std::string& fallback = "false") {
        const auto it = d->soapySettings.find(key);
        return it == d->soapySettings.end() ? fallback : it->second;
    };
    // Only rebuild option lists when changed, so a periodic model refresh cannot
    // close an open popup or discard an unsubmitted numeric edit.
    const auto populate = [](QComboBox* box, const QStringList& names, const QVariantList& values, const QVariant& current) {
        bool same = box->count() == values.size();
        for (int i = 0; same && i < box->count(); ++i) same = box->itemData(i) == values[i] && box->itemText(i) == names[i];
        if (!same) {
            box->clear();
            for (int i = 0; i < names.size(); ++i) box->addItem(names[i], values[i]);
        }
        box->setCurrentIndex(box->findData(current));
    };
    QStringList names; QVariantList values;
    for (const auto& ant : d->antennas) {
        names << QString::fromStdString(ant); values << QString::fromStdString(ant);
    }
    populate(antenna_, names, values, QString::fromStdString(d->antenna));
    antenna_->setEnabled(ready && !d->antennas.empty());
    antenna_->setToolTip(QString::fromStdString(SdrplayProfile::antennaPortDescription(d->sdrplayModel, d->antenna)));
    agc_->setEnabled(ready && d->sdrplayHasAgc); agc_->setChecked(d->agcEnabled);
    ifgr_->setEnabled(ready && !d->agcEnabled && std::find(d->gainElements.begin(), d->gainElements.end(), "IFGR") != d->gainElements.end());
    ifgr_->setRange(d->ifgrMin, d->ifgrMax); if (forceValues_ || !ifgr_->hasFocus()) ifgr_->setValue(d->ifgrDb);
    rfgr_->setEnabled(ready && std::find(d->gainElements.begin(), d->gainElements.end(), "RFGR") != d->gainElements.end());
    rfgr_->setRange(d->gainMin, d->gainMax); if (forceValues_ || !rfgr_->hasFocus()) rfgr_->setValue(d->rfgrDb);
    names = {"Automatic (sample rate)"}; values = {0.0};
    for (double bw : d->bandwidthsHz) { names << QString::number(bw / 1000) + " kHz"; values << bw; }
    populate(bandwidth_, names, values, d->bandwidthHz);
    bandwidth_->setEnabled(ready && !d->bandwidthsHz.empty());
    setpoint_->setEnabled(has(SdrplaySettings::kAgcSetpoint) && d->agcEnabled);
    if (forceValues_ || !setpoint_->hasFocus()) setpoint_->setValue(QString::fromStdString(val(SdrplaySettings::kAgcSetpoint, "-30")).toInt());
    names.clear(); values.clear();
    const auto options = d->sdrplaySettingOptions.find(SdrplaySettings::kRfGainSel);
    if (options != d->sdrplaySettingOptions.end()) for (const auto& v : options->second) {
        names << QString::fromStdString(v); values << QString::fromStdString(v);
    }
    populate(rfSelect_, names, values, QString::fromStdString(val(SdrplaySettings::kRfGainSel)));
    rfSelect_->setEnabled(has(SdrplaySettings::kRfGainSel) && !values.empty());
    for (const auto& [key, box] : settings_) {
        bool enabled = has(key);
        QString tip = enabled ? "Driver-supported control" : "Not exposed by this device/driver";
        if (key == SdrplaySettings::kBiasT) {
            enabled = enabled && SdrplayProfile::biasTAllowedForAntenna(d->sdrplayModel, d->antenna);
            tip = QString::fromStdString(SdrplayProfile::antennaPortDescription(d->sdrplayModel, d->antenna));
            if (d->sdrplayModel == "RSPdx" || d->sdrplayModel == "RSPdx-R2") tip += "; Bias-T requires Antenna B";
        }
        if (key == SdrplaySettings::kExtRef && d->sdrplayModel.rfind("RSPdx", 0) == 0)
            tip = "RSPdx has an external clock INPUT; this driver exposes no clock-output control.";
        box->setEnabled(enabled); box->setToolTip(tip);
        box->setChecked(SdrplayProfile::parseBoolSetting(val(key)));
    }
    status_->setText(ready ? QString::fromStdString(d->sdrplayControlStatus) :
        "Controls have not been read from the receiver. Start reception or rescan while stopped.");
    refreshing_ = false;
}
