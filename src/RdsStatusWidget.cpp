#include "RdsStatusWidget.h"
#include <cmath>

RdsStatusWidget::RdsStatusWidget(QWidget* parent) : QLabel(parent) {
    setObjectName("rds.status");
    setTextFormat(Qt::PlainText);
    setWordWrap(true);
    setMinimumHeight(38);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setText("RDS: WFM inactive");
}

void RdsStatusWidget::presentTone(const CtcssSnapshot& s, bool eligible, double targetHz, int64_t nowMs, const DcsSnapshot& dcs) {
    if (!eligible) { setText("NFM tones: inactive"); return; }
    if (std::abs(s.targetHz-targetHz)>1 || !s.updatedMs || nowMs-s.updatedMs>2000) {
        setText("NFM tones: awaiting discriminator data"); return;
    }
    QStringList labels;
    if (s.frequencyHz>0) labels.append(QString("CTCSS: %1 Hz").arg(s.frequencyHz,0,'f',1));
    if (std::abs(dcs.targetHz-targetHz)<=1 && dcs.updatedMs && nowMs-dcs.updatedMs<=2000 && !dcs.identities.empty()) {
        QStringList aliases;
        for (const auto& id:dcs.identities)
            aliases.append(QString("%1%2").arg(id.code,3,8,QChar('0')).arg(id.inverted?'I':'N'));
        labels.append("DCS: " + aliases.join(" / ") + " (equivalent)");
    }
    setText(labels.isEmpty() ? QStringLiteral("NFM tones: searching CTCSS / DCS") : labels.join("  |  "));
}

void RdsStatusWidget::present(const RdsMpxSnapshot& s, bool eligible, double targetHz, int64_t nowMs) {
    if (!eligible) { setText("RDS: WFM inactive"); return; }
    if (std::abs(s.targetHz-targetHz)>1.0 || s.status=="Disabled") {
        setText("RDS: Acquiring"); return;
    }
    if (!s.station.identified) { setText("RDS: " + QString::fromStdString(s.status)); return; }
    // DEC-0080: five seconds is a UI freshness policy, not a decoder timeout.
    if (!s.lastGroupMs || nowMs-s.lastGroupMs>5000) { setText("RDS: No recent data"); return; }
    setText(QString("RDS: %1  |  PI %2  |  PTY %3%4\n%5")
        .arg(s.station.programmeService.empty() ? QStringLiteral("Station identified") : QString::fromStdString(s.station.programmeService))
        .arg(s.station.pi,4,16,QChar('0')).arg(s.station.pty)
        .arg(s.station.trafficAnnouncement ? "  |  Traffic announcement" : "")
        .arg(QString::fromStdString(s.station.radioText)));
}
