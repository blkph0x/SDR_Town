#include "InmarsatTrackingPanel.h"
#include "InmarsatMapWidget.h"
#include "InmarsatIdentity.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QCheckBox>
#include <QMessageBox>
#include <QAbstractButton>
#include <QLabel>
#include <QSettings>
#include <QThread>
#include <catch2/catch_test_macros.hpp>
#include <cstring>

namespace {
class Reply : public QNetworkReply {
public:
    QByteArray data;qsizetype offset=0;bool aborted=false;
    explicit Reply(const QNetworkRequest& request,QObject* parent):QNetworkReply(parent) {
        setRequest(request);setUrl(request.url());open(QIODevice::ReadOnly|QIODevice::Unbuffered);
    }
    void deliver(QByteArray bytes,int status=200,QByteArray retry={}) {
        data=bytes;setAttribute(QNetworkRequest::HttpStatusCodeAttribute,status);
        if(!retry.isEmpty())setRawHeader("Retry-After",retry);
        emit readyRead();setFinished(true);emit finished();
    }
    void abort() override {aborted=true;setError(OperationCanceledError,"cancelled");setFinished(true);emit finished();}
    qint64 bytesAvailable() const override {return data.size()-offset+QNetworkReply::bytesAvailable();}
protected:
    qint64 readData(char* buffer,qint64 max) override {
        const auto n=std::min<qint64>(max,data.size()-offset);if(n<=0)return -1;
        std::memcpy(buffer,data.constData()+offset,n);offset+=n;return n;
    }
};
class Network : public QNetworkAccessManager {
public:
    QPointer<Reply> last;int calls=0;
protected:
    QNetworkReply* createRequest(Operation,const QNetworkRequest& request,QIODevice*) override {
        ++calls;last=new Reply(request,this);return last;
    }
};
InmarsatAircraft received() {
    InmarsatAircraft a;a.lastSeenMonotonic=inmarsatMonotonicSeconds();
    a.identity.aesId=0x123456;a.identity.classicAeroIdentity=true;a.identity.icaoHex="123456";a.identity.validated=true;
    return a;
}
void seed(InmarsatTracking& model) {
    // Sequence receipt before snapshot time: C++ argument evaluation order is
    // unspecified, and future receipt times are intentionally rejected.
    const auto a=received();model.setRf({a},inmarsatMonotonicSeconds());
}
QByteArray payload() {
    return QByteArray::fromStdString(nlohmann::json{{"now",QDateTime::currentMSecsSinceEpoch()},
        {"ac",{{{"hex","123456"},{"lat",-34},{"lon",151},{"seen_pos",0}}}}}.dump());
}
}
TEST_CASE("Online lookup is opt-in bounded asynchronous and cancels late replies", "[inmarsat][gui]") {
    InmarsatTracking model;seed(model);Network network;
    InmarsatOnlineLookup lookup(model,nullptr,&network);lookup.setActive(true);lookup.poll();CHECK(network.calls==0);
    lookup.setEnabled(true);lookup.poll();REQUIRE(network.last);CHECK(network.calls==1);
    CHECK(network.last->url().scheme()=="https");CHECK(network.last->url().path()=="/v2/hex/123456");
    for(int i=0;i<100;++i)lookup.poll();CHECK(network.calls==1);
    auto* old=network.last.data();lookup.setEnabled(false);CHECK(old->aborted);
    old->deliver(payload());CHECK(model.report({},inmarsatMonotonicSeconds(),false)["positions"].empty());
    lookup.setEnabled(true);lookup.poll();network.last->deliver(payload());
    CHECK(lookup.report()["lookupSuccess"]==1);CHECK(model.report({},inmarsatMonotonicSeconds(),false)["positions"].size()==1);
    lookup.setEnabled(false);CHECK(model.report({},inmarsatMonotonicSeconds(),false)["positions"].empty());
}
TEST_CASE("Lookup failures reject malformed oversized redirects and respect server backoff", "[inmarsat][gui]") {
    for(const int status:{200,302,429,503}) {
        InmarsatTracking model;seed(model);Network network;
        InmarsatOnlineLookup lookup(model,nullptr,&network);lookup.setActive(true);lookup.setEnabled(true);lookup.poll();
        REQUIRE(network.last);network.last->deliver("malformed",status,"120");
        CHECK(lookup.report()["lookupFailures"]==1);lookup.poll();CHECK(network.calls==1);
        if(status==429 || status==503)CHECK(lookup.report()["lookupRetrySeconds"].get<double>()>119);
    }
    for(const auto& bytes:{QByteArray(512*1024+1,'x'),QByteArray(32,'[')+QByteArray(32,']')}) {
        InmarsatTracking model;seed(model);Network network;
        InmarsatOnlineLookup lookup(model,nullptr,&network);lookup.setActive(true);lookup.setEnabled(true);lookup.poll();
        REQUIRE(network.last);network.last->deliver(bytes);CHECK(lookup.report()["lookupFailures"]==1);
    }
}
TEST_CASE("Closing hidden map suspends lookup without re-enabling internet", "[inmarsat][gui]") {
    InmarsatTracking model;seed(model);Network network;
    InmarsatOnlineLookup lookup(model,nullptr,&network);lookup.setActive(true);lookup.setEnabled(true);lookup.poll();
    REQUIRE(network.last);auto* old=network.last.data();lookup.setActive(false);CHECK(old->aborted);
    old->deliver(payload());CHECK(model.report({},inmarsatMonotonicSeconds(),false)["positions"].empty());
    lookup.poll();CHECK(network.calls==1);
}
TEST_CASE("Online lookup enforces total request deadline", "[inmarsat][gui]") {
    InmarsatTracking model;seed(model);Network network;
    InmarsatOnlineLookup lookup(model,nullptr,&network);lookup.setActive(true);lookup.setEnabled(true);lookup.poll();
    REQUIRE(network.last);QElapsedTimer elapsed;elapsed.start();
    while(lookup.report()["lookupInFlight"]==1 && elapsed.elapsed()<12000) {QApplication::processEvents();QThread::msleep(5);}
    CHECK(lookup.report()["lookupErrorCode"]==2);CHECK(lookup.report()["lookupInFlight"]==0);
}
TEST_CASE("Map consent is declined safely and persists only explicit acceptance", "[inmarsat][gui]") {
    InmarsatMessageStore::instance().clear();
    QSettings settings;const auto previous=settings.value("inmarsat/mapOnlineConsentV1");
    struct Restore {QVariant value;~Restore(){if(value.isValid())QSettings().setValue("inmarsat/mapOnlineConsentV1",value);else QSettings().remove("inmarsat/mapOnlineConsentV1");}} restore{previous};
    settings.remove("inmarsat/mapOnlineConsentV1");
    InmarsatTrackingPanel panel;panel.resize(600,420);panel.show();QApplication::processEvents();
    auto* online=panel.findChild<QCheckBox*>("inmarsatOnlinePositions");REQUIRE(online);CHECK_FALSE(online->isChecked());
    auto answer=[](QMessageBox::StandardButton button) {
        QTimer::singleShot(0,[button]{
            for(auto* widget:QApplication::topLevelWidgets())
                if(auto* box=qobject_cast<QMessageBox*>(widget))
                    if(auto* choice=box->button(button))choice->click();
        });
    };
    answer(QMessageBox::No);online->click();CHECK_FALSE(online->isChecked());
    answer(QMessageBox::Yes);online->click();REQUIRE(online->isChecked());
    CHECK(QSettings().value("inmarsat/mapOnlineConsentV1").toBool());
    InmarsatTrackingPanel reopened;CHECK(reopened.findChild<QCheckBox*>("inmarsatOnlinePositions")->isChecked());
    online->click();CHECK_FALSE(QSettings().value("inmarsat/mapOnlineConsentV1").toBool());
    auto* status=panel.findChild<QLabel*>("inmarsatTrackingStatus");REQUIRE(status);CHECK(status->text().contains("disabled"));
}
TEST_CASE("Hybrid map has distinct RF online and call markers at compact and desktop sizes", "[inmarsat][gui]") {
    InmarsatMapWidget map;
    const auto report=nlohmann::json{{"activeAesIds",{0x123456}},{"positions",{
        {{"aesId",0x123456},{"latDeg",0},{"lonDeg",0},{"positionSource","adsb_lol"}},
        {{"aesId",0x654321},{"latDeg",35},{"lonDeg",50},{"positionSource","adsb_lol"}},
        {{"aesId",0x234567},{"latDeg",35},{"lonDeg",-50},{"positionSource","rf_adsc"},{"estimated",true}}
    }}};
    map.setReport(report,false);
    for(const QSize size:{QSize(560,360),QSize(1100,650)}) {
        map.resize(size);map.show();QApplication::processEvents();const auto image=map.grab().toImage();
        int green=0,blue=0,yellow=0;
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
            const auto color=image.pixelColor(x,y);green+=color==QColor("#52ef88");blue+=color==QColor("#60c8ff");yellow+=color==QColor("#ffd65c");
        }
        CHECK(green>10);CHECK(blue>10);CHECK(yellow>5);
        auto* legend=map.findChild<QLabel*>("inmarsatMapLegend");REQUIRE(legend);
        CHECK(map.rect().contains(legend->geometry()));
        const auto directory=qEnvironmentVariable("SDR_TOWN_TEST_VISUAL_DIR");
        if(!directory.isEmpty())CHECK(image.save(directory+QString("/hybrid-map-%1.png").arg(size.width())));
    }
    auto bounded=nlohmann::json::object();bounded["positions"]=nlohmann::json::array();
    for(unsigned id=1;id<=513;++id)bounded["positions"].push_back({{"aesId",id},{"latDeg",0},{"lonDeg",0}});
    map.setReport(bounded,false);CHECK(map.aircraftCount()==512);
}
