#include <catch2/catch_test_macros.hpp>
#ifdef HAVE_SOAPYSDR
#include "SdrplayControlFixture.h"
#include "DeviceManager.h"
#include "Receiver.h"
#include "SstvRfLiveSession.h"
#include "SstvWindow.h"
#include "SatcomScannerEngine.h"
#include "SatPassPlanner.h"
#include "TleStore.h"
#include "Sgp4.h"
#include <QDateTime>
#include <QDir>
#include <SoapySDR/Registry.hpp>
#include <QApplication>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFile>
#include <future>
#include <numbers>
#include <iostream>
#include <cstring>
#include <optional>

namespace {
class SstvVirtualSdr : public SdrplayControlFixture {
public:
    static inline std::vector<int16_t> audio;
    static inline std::atomic<bool> armed=false,done=false;
    static inline std::atomic<uint64_t> emitted=0;
    static inline double gapAt=-1;
    static inline bool satellite=false;
    static inline Sgp4::Elements orbit;
    static inline SatObserverConfig observer;
    double startUnix=0;
    std::chrono::steady_clock::time_point start;
    uint64_t position=0;double phase=0;bool begun=false,gapSent=false;
    uint32_t rng=217;
    int readStream(SoapySDR::Stream*,void* const* buffers,size_t elements,int&,long long&,long) override {
        if(!armed || done) {std::this_thread::sleep_for(std::chrono::milliseconds(5));return SOAPY_SDR_TIMEOUT;}
        if(!begun) {begun=true;start=std::chrono::steady_clock::now();startUnix=QDateTime::currentMSecsSinceEpoch()/1000.0;}
        if(!gapSent && gapAt>=0 && double(position)/rate>=gapAt) {
            gapSent=true; position+=uint64_t(rate/100); // Known 10 ms simulated hardware loss.
            return SOAPY_SDR_OVERFLOW;
        }
        auto* out=static_cast<std::complex<float>*>(buffers[0]);
        // One second of leading silence allows startup without consuming the VIS.
        const uint64_t total=uint64_t(rate*(1.+double(audio.size())/96000.));
        const size_t count=size_t(std::min<uint64_t>(elements,total-position));
        double offset=0;
        if(satellite) {
            const double jd=2440587.5+(startUnix+position/rate)/86400;
            const auto state=Sgp4::propagate(orbit,Sgp4::minutesSinceEpoch(orbit,jd));
            double el=0,az=0,range=0,rr=0;
            if(state.ok) Sgp4::lookAnglesTeme(jd,state.r,state.v,observer.latDeg,observer.lonDeg,observer.altM,&el,&az,&range,&rr);
            offset=Sgp4::dopplerShiftHz(437550000,rr);
        }
        for(size_t i=0;i<count;++i) {
            const double t=(double(position+i)/rate-1)*96000.;
            double value=0;
            if(t>=0 && t<double(audio.size()-1)) {
                const size_t n=size_t(t);value=(audio[n]*(1-(t-n))+audio[n+1]*(t-n))/32768.;
            }
            phase=std::remainder(phase+2*std::numbers::pi*(2500*value+offset)/rate,2*std::numbers::pi);
            const auto noise=[&] {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return (float(rng)/4294967295.f-.5f)*.002f;};
            out[i]=std::polar(.5f,float(phase))+std::complex<float>(noise(),noise());
        }
        position+=count;
        std::this_thread::sleep_until(start+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(position/rate)));
        emitted=position;if(position==total)done=true;
        return int(count);
    }
};
}

TEST_CASE("SSTV recorded NFM traverses paced virtual SDR and real live session", "[.sstv-virtual-sdr]") {
    const auto input=qEnvironmentVariable("SDR_TOWN_SSTV_VIRTUAL_PCM");
    REQUIRE_FALSE(input.isEmpty());
    QFile file(input);REQUIRE(file.open(QIODevice::ReadOnly));
    const auto bytes=file.readAll();REQUIRE(bytes.size()>2);REQUIRE(bytes.size()%2==0);
    SstvVirtualSdr::audio.resize(size_t(bytes.size()/2));
    std::memcpy(SstvVirtualSdr::audio.data(),bytes.data(),size_t(bytes.size()));
    int argc=1;char name[]="sstv-virtual";char* argv[]={name,nullptr};
    QApplication app(argc,argv);QStandardPaths::setTestModeEnabled(true);
    SstvVirtualSdr::gapAt=qEnvironmentVariable("SDR_TOWN_SSTV_VIRTUAL_GAP_AT","-1").toDouble();
    app.setOrganizationName("SDRTownTests");app.setApplicationName("SstvVirtualSdr");
    QTemporaryDir directory;REQUIRE(directory.isValid());
    SoapySDR::Registry registry("sstv_virtual_fixture",
        [](const SoapySDR::Kwargs&)->SoapySDR::KwargsList{return {{{"driver","sstv_virtual_fixture"},{"label","SSTV virtual SDR"},{"serial","test-sstv"}}};},
        [](const SoapySDR::Kwargs&)->SoapySDR::Device*{return new SstvVirtualSdr;},SOAPY_SDR_ABI_VERSION);
    auto& manager=DeviceManager::instance();const auto devices=manager.enumerateDevices(false);
    const auto it=std::find_if(devices.begin(),devices.end(),[](const auto& d){return d.driver=="sstv_virtual_fixture";});
    REQUIRE(it!=devices.end());const size_t index=it-devices.begin();
    struct Cleanup {DeviceManager& m;size_t i;~Cleanup(){m.stopStreaming(i);}} cleanup{manager,index};
    const bool dedicated=qEnvironmentVariableIntValue("SDR_TOWN_SSTV_VIRTUAL_DEDICATED")==1;
    SstvVirtualSdr::satellite=qEnvironmentVariableIntValue("SDR_TOWN_SSTV_VIRTUAL_SATELLITE")==1;
    if(SstvVirtualSdr::satellite) {
        SatcomScannerEngine engine("qa-virtual-satellite");
        auto config=engine.config();config.deviceIndex=index;config.deviceStableKey=it->stableKey;
        config.monitorAudio=false; config.sstvSaveDir=directory.path().toStdString(); engine.setConfig(config);
        // Synthetic overhead orbit at the test wall clock, never a downloaded
        // claim about ISS location. Feed its predicted receive offset into RF.
        const auto now=QDateTime::currentDateTimeUtc();
        const double day=now.date().dayOfYear()+now.time().msecsSinceStartOfDay()/86400000.0;
        TleSet tle{25544,"Synthetic test orbit",
            "1 25544U 98067A   21001.00000000  .00002182  00000-0  40864-4 0  9990",
            "2 25544  51.6456 247.4627 0003000  45.0000 315.0000 15.48900000200000"};
        const auto epoch=QString("%1%2").arg(now.date().year()%100,2,10,QChar('0')).arg(day,12,'f',8,QChar('0'));
        tle.line1.replace(18,14,epoch.toStdString());
        REQUIRE(Sgp4::parseTle(tle.line1,tle.line2,&SstvVirtualSdr::orbit));
        TleStore::instance().upsert(tle);
        engine.planner().setCatalogueSelection({"iss-sstv-uhf"});
        const auto subpoint=engine.planner().currentPositionsAt(now.toMSecsSinceEpoch()/1000.0).front();
        SstvVirtualSdr::observer.latDeg=subpoint.latitudeDeg;
        SstvVirtualSdr::observer.lonDeg=subpoint.longitudeDeg;
        engine.planner().setObserver(SstvVirtualSdr::observer);
        std::string error;
        for (const auto owner : {DeviceOwnership::Owner::P25, DeviceOwnership::Owner::Listen}) {
            const auto occupied = manager.claimDevice(index, owner, "satellite-busy-qa", &error);
            REQUIRE(occupied);
            CHECK_FALSE(engine.armPass("iss-sstv-uhf", "iss-sstv-uhf", true, false, &error));
            CHECK(manager.ownsDevice(occupied));
            CHECK_FALSE(engine.planner().snapshot().armed.armed);
            REQUIRE(manager.releaseDevice(occupied));
        }
        // Operational-stream validation correctly requires IQ. Unlike the
        // direct RF test, this owner cannot be armed with a silent fake driver.
        SstvVirtualSdr::armed=true;
        const bool started=engine.armPass("iss-sstv-uhf","iss-sstv-uhf",true,false,&error);
        INFO(error); REQUIRE(started);
        CHECK(engine.config().bandwidthHz==20000);
        const QString actual=QString::fromStdString(engine.snapshot().sstvOutputDir);
        REQUIRE_FALSE(actual.isEmpty());
        SstvVirtualSdr::armed=true;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30+int(SstvVirtualSdr::audio.size()/96000));
        while(!SstvVirtualSdr::done && std::chrono::steady_clock::now()<deadline) {
            app.processEvents();std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(SstvVirtualSdr::done);
        const auto drain=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(std::chrono::steady_clock::now()<drain) {app.processEvents();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        engine.disarmPass();
        QFile saved(actual+"/sstv-report.json"); REQUIRE(saved.open(QIODevice::ReadOnly));
        const auto report=nlohmann::json::parse(saved.readAll().toStdString());
        std::cout<<"SATELLITE PASS "<<report.dump()<<std::endl;
        REQUIRE(report.at("savedImages").get<int>()>=1);
        if (SstvVirtualSdr::gapAt >= 0) CHECK(report.at("discontinuities").get<uint64_t>() >= 1);
        else CHECK(report.at("discontinuities").get<uint64_t>() == 0);
        bool complete=false;
        for(const auto& image:report.at("images")) complete=complete||image.at("complete").get<bool>();
        CHECK(complete);
        CHECK(report.at("targetHz")==437550000);
        const auto output=qEnvironmentVariable("SDR_TOWN_SSTV_VIRTUAL_OUTPUT");
        if(!output.isEmpty()) {
            REQUIRE(QDir().mkpath(output));
            for(const auto& file:QDir(actual).entryList(QDir::Files)) REQUIRE(QFile::copy(actual+"/"+file,output+"/"+file));
        }
        return;
    }
    if(!dedicated) {
        REQUIRE(manager.startStreaming(index,true));
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(manager.getRuntimeStateLabel(index)!="live hardware" && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        REQUIRE(manager.getRuntimeStateLabel(index)=="live hardware");
    }
    auto receiver=std::make_shared<Receiver>();receiver->deviceIndex=index;
    receiver->freqHz=dedicated?145800000:manager.getCurrentCenterFreq(index);receiver->active=true;
    std::atomic<bool> cancel=false;uint64_t previews=0;
    uint64_t monitored=0;
    bool monitoredBeforeLock=false;
    const auto output=qEnvironmentVariable("SDR_TOWN_SSTV_VIRTUAL_OUTPUT",directory.filePath("result"));
    const auto rfMode=qEnvironmentVariable("SDR_TOWN_SSTV_VIRTUAL_MODE","auto");
    const auto decode=[&](const SstvPreview& observer,const std::function<void(const QString&)>& routeObserver) {
        std::optional<std::chrono::steady_clock::time_point> end;
        const auto finish=[&]{
                if(SstvVirtualSdr::done && !end)end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
                return end && std::chrono::steady_clock::now()>=*end;
            };
        QString selected;
        const auto route=[&](const QString& mode){selected=mode;std::cout<<"RF route: "<<mode.toStdString()<<std::endl;if(routeObserver)routeObserver(mode);SstvVirtualSdr::armed=true;};
        const auto monitor=[&](const float* samples,size_t count,double rate) {
            REQUIRE(samples); REQUIRE(rate==48000);
            monitored+=count;
            monitoredBeforeLock=monitoredBeforeLock || selected=="searching";
        };
        const auto preview=[&](const auto& image,const auto& mode,int rows) {++previews;if(observer)observer(image,mode,rows);};
        if(dedicated) return decodeSstvDedicatedRadio(QString::fromStdString(it->stableKey),receiver->freqHz,
            output,"auto",rfMode,finish,[&]{return cancel.load();},preview,route,monitor);
        return decodeSstvRfLive(receiver,output,"auto",rfMode,finish,[&]{return cancel.load();},preview,route,monitor);
    };
    nlohmann::json report;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30+int(SstvVirtualSdr::audio.size()/96000));
    if(qEnvironmentVariableIntValue("SDR_TOWN_SSTV_VIRTUAL_GUI")==1) {
        SstvWindow window({});
        window.setRfDevices({{QString::fromStdString(it->stableKey),"SSTV virtual SDR"}});
        REQUIRE(window.selectRfSource(dedicated?QString::fromStdString(it->stableKey):QString(),receiver->freqHz));
        window.setLiveSource([&](const auto&,const auto&,const auto& route)->SstvWindow::Decode {
            return [&,route](const auto&,const auto&,const auto&,const auto&,const auto& preview) {return decode(preview,route);};
        });
        window.show(); REQUIRE(window.startLive(output,"auto",rfMode));
        while(window.busy() && std::chrono::steady_clock::now()<deadline) {app.processEvents();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        if(window.busy()) {cancel=true;window.cancel();}
        while(window.busy()) {app.processEvents();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        INFO(window.statusMessage().toStdString());
        REQUIRE(window.statusMessage().contains("image(s) saved"));
        CHECK(window.grab().save(output+"/gui.png"));
        QFile saved(output+"/sstv-report.json"); REQUIRE(saved.open(QIODevice::ReadOnly));
        report=nlohmann::json::parse(saved.readAll().toStdString());
    } else {
        auto task=std::async(std::launch::async,[&]{return decode({},{});});
        if(task.wait_until(deadline)!=std::future_status::ready)cancel=true;
        report=task.get();
    }
    if(SstvVirtualSdr::gapAt>=0) CHECK(report.at("discontinuities").get<uint64_t>()>=1);
    if(dedicated) {
        CHECK(monitored>0);
        if(qEnvironmentVariable("SDR_TOWN_SSTV_VIRTUAL_MODE","auto")=="auto") CHECK(monitoredBeforeLock);
    }
    std::cout<<"Virtual SDR samples="<<SstvVirtualSdr::emitted<<" previews="<<previews<<" "<<report.dump()<<std::endl;
    bool valid=false;
    const int expected=qEnvironmentVariable("SDR_TOWN_SSTV_VIRTUAL_IMAGES","1").toInt(&valid);
    REQUIRE(valid);REQUIRE(expected>=0);
    REQUIRE(report.at("images").size()==size_t(expected));
    if(expected) {
        if(qEnvironmentVariable("SDR_TOWN_SSTV_VIRTUAL_COMPLETE","1")=="1")
            CHECK(report.at("images")[0].at("complete")==true);
        CHECK(report.at("rfModeSelected")=="NFM");
    }
}
#endif
