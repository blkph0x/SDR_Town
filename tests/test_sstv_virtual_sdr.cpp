#include <catch2/catch_test_macros.hpp>
#ifdef HAVE_SOAPYSDR
#include "SdrplayControlFixture.h"
#include "DeviceManager.h"
#include "Receiver.h"
#include "SstvRfLiveSession.h"
#include <SoapySDR/Registry.hpp>
#include <QCoreApplication>
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
    std::chrono::steady_clock::time_point start;
    uint64_t position=0;double phase=0;bool begun=false;
    int readStream(SoapySDR::Stream*,void* const* buffers,size_t elements,int&,long long&,long) override {
        if(!armed || done) {std::this_thread::sleep_for(std::chrono::milliseconds(5));return SOAPY_SDR_TIMEOUT;}
        if(!begun) {begun=true;start=std::chrono::steady_clock::now();}
        auto* out=static_cast<std::complex<float>*>(buffers[0]);
        // One second of leading silence allows startup without consuming the VIS.
        const uint64_t total=uint64_t(rate*(1.+double(audio.size())/96000.));
        const size_t count=size_t(std::min<uint64_t>(elements,total-position));
        for(size_t i=0;i<count;++i) {
            const double t=(double(position+i)/rate-1)*96000.;
            double value=0;
            if(t>=0 && t<double(audio.size()-1)) {
                const size_t n=size_t(t);value=(audio[n]*(1-(t-n))+audio[n+1]*(t-n))/32768.;
            }
            phase=std::remainder(phase+2*std::numbers::pi*2500*value/rate,2*std::numbers::pi);
            out[i]=std::polar(.5f,float(phase));
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
    QCoreApplication app(argc,argv);QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName("SDRTownTests");app.setApplicationName("SstvVirtualSdr");
    QTemporaryDir directory;REQUIRE(directory.isValid());
    SoapySDR::Registry registry("sstv_virtual_fixture",
        [](const SoapySDR::Kwargs&)->SoapySDR::KwargsList{return {{{"driver","sstv_virtual_fixture"},{"label","SSTV virtual SDR"},{"serial","test-sstv"}}};},
        [](const SoapySDR::Kwargs&)->SoapySDR::Device*{return new SstvVirtualSdr;},SOAPY_SDR_ABI_VERSION);
    auto& manager=DeviceManager::instance();const auto devices=manager.enumerateDevices(false);
    const auto it=std::find_if(devices.begin(),devices.end(),[](const auto& d){return d.driver=="sstv_virtual_fixture";});
    REQUIRE(it!=devices.end());const size_t index=it-devices.begin();
    struct Cleanup {DeviceManager& m;size_t i;~Cleanup(){m.stopStreaming(i);}} cleanup{manager,index};
    REQUIRE(manager.startStreaming(index,true));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(manager.getRuntimeStateLabel(index)!="live hardware" && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(manager.getRuntimeStateLabel(index)=="live hardware");
    auto receiver=std::make_shared<Receiver>();receiver->deviceIndex=index;
    receiver->freqHz=manager.getCurrentCenterFreq(index);receiver->active=true;
    std::atomic<bool> cancel=false;uint64_t previews=0;
    auto task=std::async(std::launch::async,[&] {
        std::optional<std::chrono::steady_clock::time_point> end;
        return decodeSstvRfLive(receiver,qEnvironmentVariable("SDR_TOWN_SSTV_VIRTUAL_OUTPUT",directory.filePath("result")),"auto",
            qEnvironmentVariable("SDR_TOWN_SSTV_VIRTUAL_MODE","auto"),[&]{
                if(SstvVirtualSdr::done && !end)end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
                return end && std::chrono::steady_clock::now()>=*end;
            },[&]{return cancel.load();},[&](const auto&,const auto&,int){++previews;},
            [&](const QString& mode){std::cout<<"RF route: "<<mode.toStdString()<<std::endl;SstvVirtualSdr::armed=true;});
    });
    if(task.wait_for(std::chrono::seconds(150))!=std::future_status::ready)cancel=true;
    const auto report=task.get();
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
