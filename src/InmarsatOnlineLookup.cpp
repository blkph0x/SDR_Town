#include "InmarsatOnlineLookup.h"
#include "InmarsatIdentity.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDateTime>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace { constexpr qint64 maxBytes=512*1024; }
InmarsatOnlineLookup::InmarsatOnlineLookup(InmarsatTracking& model,QObject* parent,QNetworkAccessManager* network)
    :QObject(parent),model_(model),network_(network?network:new QNetworkAccessManager(this)) {
    deadline_.setSingleShot(true);
    connect(&deadline_,&QTimer::timeout,this,[this]{timedOut_=true;if(reply_)reply_->abort();});
}
InmarsatOnlineLookup::~InmarsatOnlineLookup() {cancel();}
void InmarsatOnlineLookup::cancel() {
    deadline_.stop();
    if(reply_) {
        auto* old=reply_.data();reply_.clear();
        disconnect(old,nullptr,this,nullptr);old->abort();old->deleteLater();++cancelled_;
    }
    body_.clear();requested_.clear();
}
void InmarsatOnlineLookup::setEnabled(bool on) {
    Q_ASSERT(QThread::currentThread()==thread());
    if(enabled_==on)return;
    cancel();enabled_=on;model_.setOnlineEnabled(on);next_=0;consecutiveFailures_=0;
    state_=on?"waiting":"disabled";if(changed)changed();
}
void InmarsatOnlineLookup::setActive(bool on) {
    if(active_==on)return;
    active_=on;if(!on)cancel();
    state_=!enabled_?"disabled":on?"waiting":"suspended";
}
void InmarsatOnlineLookup::poll() {
    Q_ASSERT(QThread::currentThread()==thread());
    const double now=inmarsatMonotonicSeconds();
    if(!enabled_ || !active_ || reply_ || now<next_)return;
    const auto ids=model_.eligibleIds(now);
    if(ids.empty()) {state_="waiting_for_identity";return;}
    requested_.clear();QStringList hex;
    // Fair bounded batches. Repeated discovery cannot create concurrent requests.
    cursor_%=ids.size();
    for(size_t i=0;i<std::min<size_t>(100,ids.size());++i) {
        const auto id=ids[(cursor_+i)%ids.size()];requested_.insert(id);
        hex<<QString::fromStdString(inmarsatClassicIcao(id));
    }
    cursor_=(cursor_+hex.size())%ids.size();
    QNetworkRequest request(QUrl("https://api.adsb.lol/v2/hex/"+hex.join(',')));
    request.setRawHeader("User-Agent","SDR-Town/Inmarsat-map");
    request.setRawHeader("Accept","application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute,false);
    body_.clear();oversize_=timedOut_=false;started_=now;next_=now+20;
    ++requests_;state_="requesting";reply_=network_->get(request);reply_->setReadBufferSize(maxBytes+1);
    connect(reply_,&QNetworkReply::readyRead,this,&InmarsatOnlineLookup::receive);
    connect(reply_,&QNetworkReply::finished,this,&InmarsatOnlineLookup::finish);
    deadline_.start(10000);
}
void InmarsatOnlineLookup::receive() {
    if(!reply_ || oversize_)return;
    const auto chunk=reply_->read(maxBytes+1-body_.size());bytes_+=chunk.size();body_+=chunk;
    if(body_.size()>maxBytes || reply_->header(QNetworkRequest::ContentLengthHeader).toLongLong()>maxBytes) {
        oversize_=true;reply_->abort();
    }
}
void InmarsatOnlineLookup::finish() {
    if(!reply_)return;
    // A finished reply can still have unread bytes. Do not call abort recursively
    // from here: content size is classified after the bounded final read.
    auto* done=reply_.data();deadline_.stop();reply_.clear();
    const auto tail=done->read(maxBytes+1-body_.size());bytes_+=tail.size();body_+=tail;
    oversize_=oversize_ || body_.size()>maxBytes || done->bytesAvailable()>0;
    const int status=done->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    lastHttpStatus_=status;
    lastTransportError_=int(done->error());
    const double now=inmarsatMonotonicSeconds();latencyMs_=(now-started_)*1000;
    bool ok=false;
    if(enabled_ && active_ && !oversize_ && !timedOut_) {
        if(status==404 || status==204)ok=true;
        else if(status==200 && done->error()==QNetworkReply::NoError) {
            try {
                const auto json=nlohmann::json::parse(body_.constData(),body_.constData()+body_.size(),
                    [](int depth,nlohmann::json::parse_event_t,nlohmann::json&) {
                        if(depth>16)throw std::runtime_error("Nested provider response");
                        return true;
                    },false);
                ok=model_.acceptOnline(json,requested_,now,QDateTime::currentMSecsSinceEpoch()/1000.0);
            } catch(const std::exception&) {ok=false;}
        }
    }
    if(ok) {++success_;consecutiveFailures_=0;lastErrorCode_=0;state_="ready";}
    else {
        ++failures_;consecutiveFailures_=std::min(6u,consecutiveFailures_+1);
        next_=now+std::min(300.0,20.0*(1u<<consecutiveFailures_));
        state_=oversize_?"response_too_large":timedOut_?"timeout":status==429?"rate_limited":
            status==200?"invalid_response":"network_error";
        lastErrorCode_=oversize_?1:timedOut_?2:status==429?3:status==200?4:5;
        if(status==429 || status==503) {
            if(status==429)++rateLimited_;
            const auto header=done->rawHeader("Retry-After");bool integer=false;
            double delay=header.toDouble(&integer);
            if(!integer) {
                const auto date=QDateTime::fromString(QString::fromLatin1(header),Qt::RFC2822Date);
                delay=date.isValid()?QDateTime::currentDateTimeUtc().msecsTo(date)/1000.0:0;
            }
            if(std::isfinite(delay) && delay>0)next_=std::max(next_,now+delay);
        }
    }
    done->deleteLater();body_.clear();requested_.clear();if(changed)changed();
}
nlohmann::json InmarsatOnlineLookup::report() const {
    return {{"lookupState",state_},{"lookupRequests",requests_},{"lookupSuccess",success_},
        {"lookupFailures",failures_},{"lookupCancelled",cancelled_},{"lookupBytes",bytes_},
        {"lookupRateLimited",rateLimited_},{"lookupLatencyMs",latencyMs_},
        {"lookupHttpStatus",lastHttpStatus_},{"lookupErrorCode",lastErrorCode_},{"lookupEnabled",enabled_?1:0},
        {"lookupTransportError",lastTransportError_},
        {"lookupInFlight",reply_?1:0},{"lookupRetrySeconds",std::max(0.0,next_-inmarsatMonotonicSeconds())}};
}
