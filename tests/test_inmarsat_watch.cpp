#include <catch2/catch_test_macros.hpp>
#include "InmarsatWatch.h"
#include "InmarsatDiagnostics.h"
#include <limits>
#include <iostream>
#include <array>
#include "InmarsatIqFile.h"
#include <QProcessEnvironment>
#include <thread>
#include <chrono>
#include "InmarsatIcaoCountry.h"

TEST_CASE("Aero aircraft identity survives eviction without stale field replacement", "[inmarsat][watch]") {
    auto& store=InmarsatMessageStore::instance();store.clear();
    struct Cleanup { ~Cleanup(){InmarsatMessageStore::instance().clear();} } cleanup;
    InmarsatMessage m;m.kind=InmarsatMsgKind::Acars;m.validated=true;m.aesId=0x7c1234;
    m.unixTime=100;m.registration="VH-TEST";m.icaoHex="7C1234";m.callsign="TEST1";
    m.hasPosition=true;m.latDeg=-34;m.lonDeg=151;
    store.push(m);
    m.unixTime=110;m.registration.clear();m.icaoHex.clear();m.callsign.clear();m.hasPosition=false;
    store.push(m);
    m.unixTime=99;m.registration="OLD";store.push(m);
    for(int i=0;i<600;++i)store.push(InmarsatMessage{});
    auto a=store.aircraft();REQUIRE(a.size()==1);
    CHECK(a[0].messages==3);CHECK(a[0].identity.registration=="VH-TEST");
    CHECK(a[0].identity.icaoHex=="7C1234");CHECK(a[0].identity.callsign=="TEST1");
    CHECK(a[0].identity.unixTime==110);CHECK(a[0].positionTime==100);
    m.aesId=2;m.validated=false;store.push(m);CHECK(store.aircraft().size()==1);
    m.validated=true;m.unixTime=std::numeric_limits<double>::quiet_NaN();store.push(m);CHECK(store.aircraft().size()==1);
    m.unixTime=111;m.kind=InmarsatMsgKind::Status;store.push(m);CHECK(store.aircraft().size()==1);
    m.kind=InmarsatMsgKind::CAssign;
    for(uint32_t i=1;i<=300;++i){m.aesId=i;m.unixTime=200+i;store.push(m);}
    REQUIRE(store.aircraft().size()==256);CHECK(store.aircraft().front().identity.aesId==45);
    store.clearAircraft();CHECK(store.aircraft().empty());CHECK(store.positions().empty());CHECK(store.recent().size()>0);
}

TEST_CASE("ICAO allocation lookup is bounded and does not invent unassigned countries", "[inmarsat][watch]") {
    CHECK(inmarsatIcaoCountry(0x7c0000)=="Australia");CHECK(inmarsatIcaoCountry(0x7fffff)=="Australia");
    CHECK(inmarsatIcaoCountry(0x800000)=="India");CHECK(inmarsatIcaoCountry(0xab436f)=="United States");
    CHECK(inmarsatIcaoCountry(0x4002ac)=="United Kingdom");CHECK(inmarsatIcaoCountry(0x76cdb5)=="Singapore");
    CHECK(inmarsatIcaoCountry(0)=="");CHECK(inmarsatIcaoCountry(0xffffff)=="");
    CHECK(inmarsatIcaoCountry(0x1000000)=="");CHECK(inmarsatIcaoCountry(0xf00001)=="");
}

namespace {
InmarsatWatchConfig example() {
    InmarsatWatchConfig c;c.enabled=true;c.simultaneousInBand=false; // Explicit legacy rotation fixtures.
    c.channels={{"data","Position",1542e6,10500,true},{"voice","Voice",1543e6,8400,true}};
    return c;
}
}
TEST_CASE("Aero in-band data and voice remain continuous without timer retunes", "[inmarsat][watch]") {
    auto c=example();c.simultaneousInBand=true;c.positionTarget=1;
    InmarsatWatchSchedule s(c,2e6,0);
    REQUIRE(s.groupCount()==1);REQUIRE(s.group().simultaneous);
    REQUIRE(s.group().channels.size()==2);
    for(const auto& ch:s.group().channels)
        REQUIRE(std::abs(ch.frequencyHz-s.group().centerHz)+6500<=2e6*.45);
    s.position(1);s.validatedData("voice",1);
    REQUIRE(s.report(1)["validatedDataChannels"]==0);
    s.validatedData("data",1);
    REQUIRE(s.report(1)["dataChannels"]==1);REQUIRE(s.report(1)["dataReady"]==true);
    for(double now:{10.,30.,180.,600.,7200.})REQUIRE_FALSE(s.advance(now));
    REQUIRE(s.report(7200)["validatedDataChannels"]==0);
    REQUIRE(s.report(7200)["refreshDue"]==false);
    REQUIRE(InmarsatWatchConfig::fromJson(c.toJson()).simultaneousInBand);
    auto old=c.toJson();old.erase("simultaneousInBand");
    REQUIRE(InmarsatWatchConfig::fromJson(old).simultaneousInBand);
    SECTION("budget forces rotation") {c.maxConcurrentChannels=1;REQUIRE(planInmarsatWatch(c,2e6).size()==2);}
    SECTION("bandwidth forces rotation") {REQUIRE(planInmarsatWatch(c,96000).size()==2);}
    SECTION("opt out preserves rotation") {c.simultaneousInBand=false;REQUIRE(planInmarsatWatch(c,2e6).size()==2);}
    SECTION("disabled distant channel is excluded") {
        c.channels.push_back({"off","",1600e6,10500,false});
        REQUIRE(planInmarsatWatch(c,2e6).size()==1);
    }
}
TEST_CASE("Aero watch settings are bounded and round trip", "[inmarsat][watch]") {
    auto c=example();c.channels.push_back({"burst","Burst",1544e6,-1200,false});
    REQUIRE(InmarsatWatchConfig::fromJson(c.toJson()).toJson()==c.toJson());
    SECTION("duplicate ID") {c.channels[1].id=c.channels[0].id;REQUIRE_THROWS(c.validate());}
    SECTION("duplicate frequency and rate") {c.channels[1].frequencyHz=c.channels[0].frequencyHz;c.channels[1].rate=10500;REQUIRE_THROWS(c.validate());}
    SECTION("invalid rate") {c.channels[1].rate=4800;REQUIRE_THROWS(c.validate());}
    SECTION("expanded concurrency") {c.maxConcurrentChannels=16;REQUIRE_NOTHROW(c.validate());}
    SECTION("unbounded concurrency") {c.maxConcurrentChannels=17;REQUIRE_THROWS(c.validate());}
    SECTION("nonfinite") {c.channels[1].frequencyHz=std::numeric_limits<double>::quiet_NaN();REQUIRE_THROWS(c.validate());}
    SECTION("invalid timing") {c.maxVoiceSeconds=10;REQUIRE_THROWS(c.validate());}
    SECTION("invalid JSON") {REQUIRE_THROWS(InmarsatWatchConfig::fromJson({{"channels",false}}));}
    SECTION("empty selection") {for(auto& ch:c.channels)ch.enabled=false;REQUIRE_THROWS(planInmarsatWatch(c,2e6));}
}

TEST_CASE("Aero mixed workers preserve all channels across refresh deadlines", "[inmarsat][watch]") {
    auto c=example();c.simultaneousInBand=true;
    c.channels[1].frequencyHz=c.channels[0].frequencyHz+25000;
    int flushes=0,pcm=0;
    InmarsatWatchSession session(c,96000,0,{},[&](std::span<const int16_t>,uint32_t){++pcm;},[&]{++flushes;});
    std::vector<std::complex<float>> iq(4096);
    for(int i=0;i<3;++i) {
        session.process(iq,uint64_t(i)*iq.size(),96000,session.centerHz(),false,i*0.05);
        REQUIRE_FALSE(session.advance(7200+i));
    }
    const auto report=session.report(7202)["watch"];
    REQUIRE(report["simultaneous"]==true);REQUIRE(report["workerCount"]==2);
    REQUIRE(report["channels"][0]["rate"]==10500);REQUIRE(report["channels"][1]["rate"]==8400);
    REQUIRE(report["switches"]==0);REQUIRE(flushes==0);REQUIRE(pcm==0);
    const auto displays=session.displays();REQUIRE(displays.size()==2);
    REQUIRE(displays[0].id=="data");REQUIRE(displays[1].id=="voice");
}

TEST_CASE("Aero map retains validated latest positions beyond message history", "[inmarsat][watch]") {
    auto& store=InmarsatMessageStore::instance();store.clear();
    InmarsatMessage position;position.aesId=1;position.validated=true;position.hasPosition=true;
    position.unixTime=100;position.latDeg=-34;position.lonDeg=151;
    store.push(position);
    for(int i=0;i<600;++i)store.push(InmarsatMessage{});
    REQUIRE(store.recent(1000).size()==500);REQUIRE(store.positions().size()==1);
    CHECK(store.positions()[0].unixTime==100);
    position.unixTime=99;position.latDeg=-35;store.push(position);
    CHECK(store.positions()[0].latDeg==-34);
    position.unixTime=101;store.push(position);CHECK(store.positions()[0].latDeg==-35);
    position.aesId=2;position.validated=false;store.push(position);
    position.validated=true;position.latDeg=91;store.push(position);
    CHECK(store.positions().size()==1);
    position.latDeg=-34;
    for(uint32_t id=2;id<=300;++id) {position.aesId=id;position.unixTime=100+id;store.push(position);}
    REQUIRE(store.positions().size()==256);
    CHECK(store.positions().front().aesId==45);
    store.clear();CHECK(store.positions().empty());CHECK(store.recent().empty());
}
TEST_CASE("Aero watch grouping preserves full channels and separates roles", "[inmarsat][watch]") {
    auto c=example();c.channels.clear();
    for(int i=0;i<20;++i)c.channels.push_back({std::to_string(i),"",1542e6+i*12500,i<10?10500:8400,true});
    c.channels.push_back({"disabled","",1580e6,8400,false});
    for(double rate:{16000.,48000.,96000.,2.048e6,10e6}) {
        const auto groups=planInmarsatWatch(c,rate);size_t n=0;
        for(const auto& g:groups) {
            REQUIRE(g.channels.size()<=size_t(c.maxConcurrentChannels));
            for(const auto& ch:g.channels) {
                ++n;REQUIRE(ch.voice()==g.voice);
                REQUIRE(std::abs(ch.frequencyHz-g.centerHz)+6500<=rate*.45+0.01);
            }
        }
        REQUIRE(n==20);
    }
}
TEST_CASE("Aero watch target counts distinct current visit aircraft", "[inmarsat][watch]") {
    auto c=example();c.positionTarget=2;
    InmarsatWatchSchedule s(c,2e6,100);
    s.position(1);s.position(1);s.position(0);
    REQUIRE(s.report(110)["visitPositions"]==1);REQUIRE_FALSE(s.advance(110));
    s.position(2);s.validatedData("data",110);REQUIRE(s.advance(111));REQUIRE(s.group().voice);
    REQUIRE(s.report(111)["collection"]=="target reached");
    s.position(3);REQUIRE(s.report(111)["visitPositions"]==2);
    REQUIRE_FALSE(s.advance(122));REQUIRE(s.advance(123));
    REQUIRE_FALSE(s.group().voice);REQUIRE(s.report(123)["visitPositions"]==0);
}
TEST_CASE("Aero watch reports incomplete data and holds real voice with bounded refresh", "[inmarsat][watch]") {
    auto c=example();InmarsatWatchSchedule s(c,2e6,0);
    REQUIRE_FALSE(s.advance(29));REQUIRE(s.advance(30));
    REQUIRE(s.report(30)["collection"]=="no positions decoded");
    REQUIRE_FALSE(s.advance(41)); // Acquisition is not idle hold.
    for(int i=41;i<630;++i) {s.speech(i);REQUIRE_FALSE(s.advance(i));}
    REQUIRE(s.report(220)["refreshDue"]==true);
    s.speech(630);REQUIRE(s.advance(630));REQUIRE_FALSE(s.group().voice);
    REQUIRE(s.report(630)["reason"]=="Maximum voice visit; refreshing positions");
}
TEST_CASE("Aero watch visits multiple groups without cross visit stale counts", "[inmarsat][watch]") {
    auto c=example();c.positionTarget=1;
    c.channels.push_back({"data2","",1550e6,600,true});
    c.channels.push_back({"voice2","",1551e6,8400,true});
    InmarsatWatchSchedule s(c,96000,0);
    s.position(1);s.validatedData("data",9);REQUIRE(s.advance(10));REQUIRE_FALSE(s.group().voice);
    REQUIRE_FALSE(s.advance(19));s.validatedData("data2",19);REQUIRE(s.advance(20));REQUIRE(s.group().voice);
    REQUIRE(s.advance(32));REQUIRE(s.group().channels[0].id=="voice2");
    REQUIRE(s.advance(44));REQUIRE_FALSE(s.group().voice);
    REQUIRE(s.report(44)["visitPositions"]==0);
}
TEST_CASE("Aero watch single role does not retune one unchanged group", "[inmarsat][watch]") {
    auto c=example();c.channels.erase(c.channels.begin());
    InmarsatWatchSchedule s(c,96000,0);
    REQUIRE_FALSE(s.advance(13));REQUIRE(s.group().voice);
}
TEST_CASE("Aero watch silence cannot select speaker or refresh activity", "[inmarsat][watch]") {
    auto c=example();c.channels.erase(c.channels.begin());
    c.channels.push_back({"voice2","",1543012500,8400,true});
    int pcm=0,flush=0;
    InmarsatWatchSession session(c,48000,0,{},[&](std::span<const int16_t>,uint32_t){++pcm;},[&]{++flush;});
    std::vector<std::complex<float>> input(4096);
    session.process(input,0,48000,session.centerHz(),false,0);
    session.process(input,4096,48000,session.centerHz(),false,0.1);
    REQUIRE(pcm==0);REQUIRE(session.report(0.1)["watch"]["speakerChannel"]=="");
    session.process(input,20000,48000,session.centerHz(),true,0.2);
    REQUIRE(flush==1);REQUIRE(session.report(0.2)["watch"]["iqGaps"]==1);
}

TEST_CASE("Aero watch telemetry excludes frequencies and aircraft identifiers", "[inmarsat][watch]") {
    const auto payload=InmarsatDiagnostics::remotePayload({{"watch",{{"group",1},{"groups",2},
        {"switches",10},{"iqGaps",0},{"refreshDue",true},{"centerHz",1542e6},
        {"speakerChannel","private-label"},{"channels",{{"aesId",1234}}}}}});
    REQUIRE(payload.size()==5);REQUIRE(payload["watch_switches"]==10);
    REQUIRE(payload.dump().find("private")==std::string::npos);
    REQUIRE(payload.dump().find("1542")==std::string::npos);
}

TEST_CASE("Aero watch multichannel throughput probe", "[.inmarsat-watch-benchmark]") {
    auto c=example();c.channels.clear();
    for(int i=0;i<16;++i)c.channels.push_back({std::to_string(i),"",1542e6+i*25000,8400,true});
    for(double sampleRate:{2048000.,10000000.})for(int concurrent:{1,2,4,8,16}) {
    c.maxConcurrentChannels=concurrent;
    InmarsatWatchSession session(c,sampleRate,0,{},{},{});
    std::vector<std::complex<float>> iq(65536,{0.01f,0.02f});
    for(int i=0;i<64;++i)session.process(iq,uint64_t(i)*iq.size(),sampleRate,session.centerHz(),false,i*iq.size()/sampleRate);
    auto r=session.report(64*iq.size()/sampleRate);
    REQUIRE(r["watch"]["channels"].size()==size_t(concurrent));
    r["watch"].erase("channels");r["watch"]["concurrent"]=concurrent;r["watch"]["sampleRate"]=sampleRate;
    std::cout<<"WATCH_BENCH "<<r["watch"].dump()<<std::endl;
    }
}

TEST_CASE("Aero wide groups require majority data evidence before early voice", "[inmarsat][watch]") {
    auto c=example();c.maxConcurrentChannels=16;c.positionTarget=2;c.channels.clear();
    for(int i=0;i<8;++i)c.channels.push_back({"d"+std::to_string(i),"",1545e6+i*12500,10500,true});
    for(int i=0;i<8;++i)c.channels.push_back({"v"+std::to_string(i),"",1543e6+i*12500,8400,true});
    const auto groups=planInmarsatWatch(c,2e6);
    REQUIRE(groups.size()==2);REQUIRE(groups[0].channels.size()==8);REQUIRE(groups[1].channels.size()==8);
    InmarsatWatchSchedule s(c,2e6,0);
    s.position(1);s.position(2);
    s.validatedData("not-a-channel",5);
    REQUIRE(s.report(10)["validatedDataChannels"]==0);
    for(int i=0;i<4;++i)s.validatedData("d"+std::to_string(i),9);
    REQUIRE_FALSE(s.report(10)["dataReady"].get<bool>());
    REQUIRE_FALSE(s.advance(10)); // Half is not a majority.
    s.validatedData("d4",10);
    REQUIRE(s.report(10)["dataReady"]==true);
    REQUIRE(s.advance(10));REQUIRE(s.group().voice);
    REQUIRE(s.report(10)["validatedDataChannels"]==0);
    REQUIRE(s.advance(22));REQUIRE_FALSE(s.group().voice);
    REQUIRE(s.report(22)["visitPositions"]==0);
    REQUIRE(s.report(22)["validatedDataChannels"]==0);
    s.position(1);s.position(2);
    REQUIRE_FALSE(s.advance(32)); // Old visit's locks cannot shorten this visit.
    REQUIRE(s.advance(52)); // Deadline is still bounded, even if channels are quiet.
    REQUIRE(s.report(52)["collection"]=="partial refresh");
}

TEST_CASE("Aero data evidence expires rather than counting stale channels", "[inmarsat][watch]") {
    auto c=example();c.positionTarget=1;
    InmarsatWatchSchedule s(c,2e6,0);s.position(1);s.validatedData("data",0);
    REQUIRE(s.report(10)["dataReady"]==true);
    REQUIRE(s.report(31)["dataReady"]==false);
}

TEST_CASE("Aero watch speaker stays on one conversation until idle", "[inmarsat][watch]") {
    InmarsatWatchFocus focus;
    const std::array<uint8_t,2> both{1,1}, first{1,0}, second{0,1}, quiet{0,0};
    REQUIRE(focus.select(quiet,0,6)==-1);
    REQUIRE(focus.select(both,1,6)==0);
    REQUIRE(focus.select(second,2,6)==0);
    REQUIRE(focus.select(both,3,6)==0);
    REQUIRE(focus.select(second,8.99,6)==0);
    REQUIRE(focus.select(second,9,6)==1);
    REQUIRE(focus.select(both,10,6)==1);
    REQUIRE(focus.select(first,15,6)==1);
    REQUIRE(focus.select(first,16,6)==0);
    focus.reset();REQUIRE(focus.select(second,17,6)==1);
}

TEST_CASE("Aero watch group changes destroy old decoder state before new IQ", "[inmarsat][watch]") {
    auto c=example();c.dataMinSeconds=1;c.dataDwellSeconds=2;c.voiceAcquireSeconds=1;
    int flushes=0;
    InmarsatWatchSession session(c,48000,0,{},{},[&]{++flushes;});
    std::vector<std::complex<float>> iq(4096);
    const auto initial=session.centerHz();
    session.process(iq,0,48000,initial,false,0);
    REQUIRE(session.report(0)["watch"]["channels"][0]["id"]=="data");
    REQUIRE(session.advance(2));REQUIRE(session.centerHz()!=initial);
    REQUIRE(flushes==1);
    session.process(iq,100000,48000,session.centerHz(),false,2);
    const auto r=session.report(2);
    REQUIRE(r["watch"]["channels"][0]["id"]=="voice");
    REQUIRE(r["samples"]==4096);REQUIRE(r["resets"]==1);
    REQUIRE(r["voiceActive"]==false);REQUIRE(r["watch"]["speakerChannel"]=="");
    REQUIRE(session.advance(3));REQUIRE(session.centerHz()==initial);REQUIRE(flushes==2);
    REQUIRE(session.report(3)["watch"]["channels"][0]["id"]=="data");
}

TEST_CASE("Aero workers drain failures and retain independent ordered timelines", "[inmarsat][watch]") {
    auto cfg=example();cfg.channels={{"one","",1545e6,10500,true},{"two","",1545012500,1200,true}};
    InmarsatWatchSession session(cfg,96000,0,{},{},{});
    std::array<InmarsatPipeline,2> serial;
    std::vector<std::complex<float>> iq(4096,{0.02f,0.03f});
    for(int block=0;block<12;++block) {
        const uint64_t start=uint64_t(block)*iq.size();
        if(block==6) {
            iq[20]={std::numeric_limits<float>::quiet_NaN(),0};
            REQUIRE_THROWS(session.process(iq,start,96000,session.centerHz(),false,block*.04));
            iq[20]={0.02f,0.03f};
        }
        session.process(iq,start,96000,session.centerHz(),false,block*.04);
        const auto report=session.report(block*.04);
        REQUIRE(report["watch"]["workerCount"]==2);
        REQUIRE(report["watch"]["maxPendingBlocksPerChannel"]==1);
        for(size_t i=0;i<2;++i) {
            const auto& c=cfg.channels[i];
            serial[i].process(iq.data(),iq.size(),start,96000,session.centerHz(),c.frequencyHz,c.mode(),false);
            auto expected=serial[i].report();auto actual=report["watch"]["channels"][i]["decoder"];
            expected.erase("processingMs");expected.erase("maxBlockMs");
            actual.erase("processingMs");actual.erase("maxBlockMs");
            REQUIRE(actual==expected);
            REQUIRE(session.displays()[i].id==c.id);
        }
    }
}

TEST_CASE("Every active Aero watch worker supplies its own constellation", "[inmarsat][watch]") {
    auto cfg=example();
    cfg.channels={{"low","",1545000000,600,true},{"medium","",1545012500,1200,true},
                  {"high","",1545025000,10500,true}};
    cfg.maxConcurrentChannels=3;
    InmarsatWatchSession session(cfg,96000,0,{},{},{});
    std::vector<std::complex<float>> iq(19200);
    uint32_t seed=12345;
    auto sample=[&] {seed=1664525u*seed+1013904223u;return float(int(seed>>16)-32768)/65536;};
    for(auto& x:iq) {const auto re=sample();x={re,sample()};}
    session.process(iq,0,96000,session.centerHz(),false,0);
    // Each worker has its own wall-clock-limited JAERO scatter producer.
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    session.process(iq,iq.size(),96000,session.centerHz(),false,.2);
    const auto displays=session.displays();
    REQUIRE(displays.size()==3);
    for(size_t i=0;i<displays.size();++i) {
        CAPTURE(i);
        CHECK(displays[i].id==cfg.channels[i].id);
        CHECK(displays[i].frequencyHz==cfg.channels[i].frequencyHz);
        CHECK(displays[i].rate==cfg.channels[i].rate);
        CHECK(displays[i].constellation.sequence>0);
        CHECK_FALSE(displays[i].constellation.points.empty());
        CHECK_FALSE(displays[i].locked);
    }
}

TEST_CASE("Aero channel workers match serial decode of a supplied real IQ reference", "[.inmarsat-watch-reference]") {
    const auto path=qEnvironmentVariable("SDR_TOWN_AERO_REFERENCE");
    REQUIRE_FALSE(path.isEmpty());
    InmarsatIqFile file;file.open(path);
    const double rate=file.info().sampleRateHz, center=file.info().captures.front().centerHz;
    auto cfg=example();cfg.channels={{"a","",center,8400,true},{"b","",center+1000,8400,true}};
    InmarsatWatchSession session(cfg,rate,0,{},{},{});
    std::array<InmarsatPipeline,2> serial;
    while(true) {
        const auto block=file.read();if(block.samples.empty())break;
        session.process(block.samples,block.startSample,rate,block.centerHz,block.discontinuity,double(block.startSample)/rate);
        const auto report=session.report(double(block.startSample)/rate);
        for(size_t i=0;i<2;++i) {
            serial[i].process(block.samples.data(),block.samples.size(),block.startSample,rate,block.centerHz,
                cfg.channels[i].frequencyHz,cfg.channels[i].mode(),block.discontinuity);
            auto expected=serial[i].report(),actual=report["watch"]["channels"][i]["decoder"];
            expected.erase("processingMs");expected.erase("maxBlockMs");
            actual.erase("processingMs");actual.erase("maxBlockMs");
            REQUIRE(actual==expected);
        }
    }
    REQUIRE(serial[0].report()["validatedFrames"].get<uint64_t>()>0);
    REQUIRE(serial[0].report()["voiceFrames"].get<uint64_t>()>0);
}
