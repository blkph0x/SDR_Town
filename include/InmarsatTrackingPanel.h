#pragma once
#include "InmarsatTracking.h"
#include "InmarsatOnlineLookup.h"
#include "InmarsatDiagnostics.h"
#include <QWidget>
#include <QTimer>
class QCheckBox;
class QLabel;
class InmarsatMapWidget;

class InmarsatTrackingPanel : public QWidget {
public:
    explicit InmarsatTrackingPanel(QWidget* parent=nullptr);
    void setReceiverReport(const nlohmann::json& report);
protected:
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
private:
    void refresh();
    InmarsatTracking model_;
    InmarsatOnlineLookup lookup_;
    InmarsatDiagnostics diagnostics_;
    nlohmann::json activity_=nlohmann::json::object();
    QTimer timer_;
    InmarsatMapWidget* map_;
    QCheckBox *online_,*estimates_;
    QLabel* status_;
    double lastLog_=0;
    double lastActivity_=0;
    QString diagnosticError_;
};
