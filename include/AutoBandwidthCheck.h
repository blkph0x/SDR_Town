#pragma once
#include <QCheckBox>
#include <QSettings>
#include <QDebug>

// DEC-0144: one policy shared by every automatic monitor bandwidth source.
// Explicit user/preset/protocol widths deliberately do not pass through here.
class AutoBandwidthCheck : public QCheckBox {
public:
    explicit AutoBandwidthCheck(QWidget* parent = nullptr) : QCheckBox("Auto BW", parent) {
        setObjectName("autoBandwidthCheck");
        setToolTip("Allow automatic channel bandwidth changes in all monitor modes.");
        setChecked(QSettings().value("monitor/autoBandwidth", true).toBool());
        connect(this, &QCheckBox::toggled, this, [](bool enabled) {
            QSettings().setValue("monitor/autoBandwidth", enabled);
            qInfo() << "monitor.auto_bandwidth" << enabled;
        });
    }
    double resolve(double currentHz, double suggestedHz) const {
        return isChecked() ? suggestedHz : currentHz;
    }
};
