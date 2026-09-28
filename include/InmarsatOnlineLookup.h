#pragma once
#include "InmarsatTracking.h"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QByteArray>
#include <functional>
class QNetworkAccessManager;
class QNetworkReply;

class InmarsatOnlineLookup : public QObject {
public:
    explicit InmarsatOnlineLookup(InmarsatTracking& model, QObject* parent=nullptr,
                                 QNetworkAccessManager* network=nullptr);
    ~InmarsatOnlineLookup() override;
    void setEnabled(bool enabled);
    void setActive(bool active);
    void poll();
    nlohmann::json report() const;
    std::function<void()> changed;
private:
    void cancel();
    void receive();
    void finish();
    InmarsatTracking& model_;
    QNetworkAccessManager* network_;
    QPointer<QNetworkReply> reply_;
    QTimer deadline_;
    QByteArray body_;
    std::set<uint32_t> requested_;
    bool enabled_=false,active_=false,oversize_=false,timedOut_=false;
    size_t cursor_=0;
    double next_=0,started_=0,latencyMs_=0;
    uint64_t requests_=0,success_=0,failures_=0,cancelled_=0,bytes_=0,rateLimited_=0;
    unsigned consecutiveFailures_=0;
    int lastHttpStatus_=0,lastErrorCode_=0,lastTransportError_=0;
    std::string state_="disabled";
};
