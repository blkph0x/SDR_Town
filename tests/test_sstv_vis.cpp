#include <catch2/catch_test_macros.hpp>
#include "SstvVis.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>

namespace {
std::vector<float> header(unsigned code,unsigned rate=8000,bool badParity=false,int stop=1200,double offset=0) {
    std::vector<float> result;
    double phase=0;
    unsigned ms=0;
    const auto tone=[&](unsigned duration,int hz) {
        ms+=duration;
        const size_t end=uint64_t{ms}*rate/1000;
        while(result.size()<end) {
            result.push_back(float(0.35*std::sin(phase)));
            phase=std::remainder(phase+2*std::numbers::pi*(hz+offset)/rate,2*std::numbers::pi);
        }
    };
    // Independent protocol construction from QSSTV TX sequence, DEC-0091.
    tone(300,1900); tone(10,1200); tone(300,1900); tone(30,1200);
    for(unsigned bit=0;bit<7;++bit) tone(30,code&(1u<<bit)?1100:1300);
    tone(30,((std::popcount(code)%2!=0)!=badParity)?1100:1300);
    tone(30,stop);
    return result;
}

std::vector<SstvVisEvent> feed(SstvVisDetector& decoder,const std::vector<float>& input,size_t chunk) {
    std::vector<SstvVisEvent> result;
    for(size_t i=0;i<input.size();i+=chunk) {
        const auto count=std::min(chunk,input.size()-i);
        auto events=decoder.process(std::span(input.data()+i,count));
        result.insert(result.end(),events.begin(),events.end());
    }
    return result;
}
}

TEST_CASE("SSTV VIS validates all seven-bit IDs without claiming image support","[sstv]") {
    for(unsigned code=0;code<128;++code) {
        SstvVisDetector decoder(8000);
        const auto events=feed(decoder,header(code),137);
        REQUIRE(events.size()==1);
        CHECK(events[0].code==code);
        CHECK(events[0].headerStartSample==0);
        CHECK(events[0].headerEndSample==7280);
    }
    CHECK(SstvVisDetector::modeName(44)=="Martin M1");
    CHECK(SstvVisDetector::modeName(95)=="PD120");
    CHECK(SstvVisDetector::modeName(127)=="Unknown");
}

TEST_CASE("SSTV VIS is sample-grid invariant across rates partitions and repeats","[sstv]") {
    for(unsigned rate:{8000u,11025u,44100u,48000u,96000u}) {
        auto audio=header(8,rate);
        const auto second=header(95,rate);
        audio.insert(audio.end(),second.begin(),second.end());
        audio.insert(audio.end(),rate/1000+1,0); // Fractional-rate fixture concatenation rounds down twice.
        std::vector<SstvVisEvent> reference;
        for(size_t chunk:{size_t{1},size_t{137},size_t{4096},size_t{8192}}) {
            CAPTURE(rate,chunk);
            SstvVisDetector decoder(rate);
            const auto events=feed(decoder,audio,chunk);
            REQUIRE(events.size()==2);
            CHECK(events[0].code==8);
            CHECK(events[1].code==95);
            if(reference.empty()) reference=events;
            CHECK(events[1].headerStartSample==reference[1].headerStartSample);
            CHECK(decoder.counters().samples==audio.size());
        }
    }
}

TEST_CASE("SSTV VIS rejects corrupt parity framing and truncation","[sstv]") {
    SstvVisDetector decoder(8000);
    CHECK(feed(decoder,header(8,8000,true),4096).empty());
    CHECK(decoder.counters().parityRejected>0);
    decoder.reset();
    CHECK(feed(decoder,header(8,8000,false,1900),4096).empty());
    CHECK(decoder.counters().framingRejected>0);
    decoder.reset();
    auto partial=header(8);
    partial.resize(partial.size()-80);
    CHECK(feed(decoder,partial,4096).empty());
    decoder.reset();
    auto invalidBreak=header(8);
    std::fill(invalidBreak.begin()+2400,invalidBreak.begin()+2480,0.0f);
    CHECK(feed(decoder,invalidBreak,4096).empty());
}

TEST_CASE("SSTV VIS handles volume phase offset and non-header inputs","[sstv]") {
    for(float gain:{0.01f,1.f,2.f}) {
        auto audio=header(60,44100,false,1200,10);
        for(auto& x:audio) x*=gain;
        audio.insert(audio.begin(),173,0); // Not aligned to search or block boundaries.
        audio.insert(audio.end(),441,0);
        SstvVisDetector decoder(44100);
        const auto result=feed(decoder,audio,137);
        REQUIRE(result.size()==1);
        CHECK(result[0].code==60);
        CHECK(std::abs(double(result[0].headerStartSample)-173)<88.2);
    }
    SstvVisDetector decoder(8000);
    CHECK(feed(decoder,std::vector<float>(16000,0),4096).empty());
    CHECK(feed(decoder,std::vector<float>(16000,0.3f),4096).empty());
    std::vector<float> tone(16000);
    for(size_t i=0;i<tone.size();++i) tone[i]=float(std::sin(2*std::numbers::pi*1900*i/8000));
    CHECK(feed(decoder,tone,4096).empty());
    std::mt19937 random(1234);
    std::uniform_real_distribution<float> noise(-0.5f,0.5f);
    for(auto& x:tone) x=noise(random);
    CHECK(feed(decoder,tone,137).empty());
}

TEST_CASE("SSTV VIS resets across gaps and rejects invalid blocks","[sstv]") {
    CHECK_THROWS(SstvVisDetector(7999));
    CHECK_THROWS(SstvVisDetector(96001));
    SstvVisDetector decoder(8000);
    const auto audio=header(8);
    CHECK(decoder.process(std::span(audio.data(),4000)).empty());
    decoder.reset();
    CHECK(decoder.process(std::span(audio.data()+4000,audio.size()-4000)).empty());
    CHECK_THROWS(decoder.process(std::vector<float>(8193)));
    CHECK(decoder.counters().samples==0);
    const float invalid=std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS(decoder.process(std::span(&invalid,1)));
    CHECK(decoder.counters().samples==0);
    REQUIRE(feed(decoder,audio,137).size()==1);
}
