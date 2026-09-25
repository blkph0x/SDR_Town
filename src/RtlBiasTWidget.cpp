#include "RtlBiasTWidget.h"
#include <QCheckBox>
#include <QLabel>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QVBoxLayout>

RtlBiasTWidget::RtlBiasTWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    toggle_ = new QCheckBox("Bias-T"); toggle_->setObjectName("rtlBiasT");
    toggle_->setToolTip("DC antenna power. Requires a bias-T-equipped RTL-SDR and a DC-safe antenna/LNA. Saved per device; restored on receiver start.");
    status_ = new QLabel; status_->setObjectName("rtlBiasTStatus");
    status_->setWordWrap(true);
    layout->addWidget(toggle_); layout->addWidget(status_);
    confirmPower = [this] {
        return QMessageBox::warning(this, "Enable antenna DC power?",
            "Only enable bias-T if this RTL-SDR has a bias-T circuit and the connected antenna/LNA accepts DC power.\n\n"
            "Do not use with a DC-shorted antenna. This setting is saved and restored when reception starts again.",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
    };
    connect(toggle_, &QCheckBox::toggled, this, [this](bool on) {
        if (on && (!confirmPower || !confirmPower())) { setState(state_); return; }
        std::string error;
        if (!apply || !apply(on, error)) {
            setState(state_);
            status_->setText(QString::fromStdString("Failed: " + error));
            return;
        }
        // The owner refreshes with the manager's authoritative state.
        state_.enabled = on;
    });
    setState(state_);
}
void RtlBiasTWidget::setState(const RtlBiasT::State& state) {
    state_ = state;
    const QSignalBlocker block(toggle_);
    toggle_->setChecked(state.enabled);
    toggle_->setEnabled((state.probed && state.supported) || state.enabled);
    status_->setText(state.status.empty() ? "Start receiver to discover support" : QString::fromStdString(state.status));
}
