#include "SstvRfRouter.h"
#include "SstvVis.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <random>
#include <chrono>
#include <iostream>
#include <fstream>
#include <cstdlib>

namespace {
std::vector<std::complex<float>> rf(std::string_view mode, unsigned rate=48000, bool parity=true, unsigned leadMs=100) {
    std::vector<std::complex<float>> out;
    double phase=0, fmPhase=0; size_t end=0;
    std::mt19937 rng(122);
    std::normal_distribution<float> noise(0,0.0001f);
    auto tone=[&](unsigned ms,unsigned hz) {
        end+=size_t(ms)*rate/1000;
        while(out.size()<end) {
            const double sample=std::sin(phase);
            std::complex<float> value;
            if(mode=="NFM") {
                fmPhase=std::remainder(fmPhase+2*std::numbers::pi*2500*sample/rate,2*std::numbers::pi);
                value=std::polar(0.4f,float(fmPhase));
            } else if(mode=="AM") value={float(0.4+0.2*sample),0};
            else value=std::polar(0.4f,float(mode=="LSB"?-phase:phase));
            value+=std::complex<float>(noise(rng),noise(rng));
            out.push_back(value);
            phase=std::remainder(phase+2*std::numbers::pi*hz/rate,2*std::numbers::pi);
        }
    };
    tone(leadMs,1500); tone(300,1900); tone(10,1200); tone(300,1900); tone(30,1200);
    constexpr unsigned code=8;
    for(unsigned bit=0;bit<7;++bit) tone(30,code&(1u<<bit)?1100:1300);
    tone(30,parity?1100:1300); tone(30,1200); tone(1200,1500);
    return out;
}
std::vector<float> run(SstvRfRouter& router,const std::vector<std::complex<float>>& iq,size_t partition) {
    std::vector<float> out;
    for(size_t pos=0;pos<iq.size();) {
        const size_t count=std::min({partition,router.maxInputSamples(),iq.size()-pos});
        router.process({iq.begin()+pos,iq.begin()+pos+count},pos,17);
        while(auto audio=router.pop()) out.insert(out.end(),audio->samples.begin(),audio->samples.begin()+audio->count);
        pos+=count;
    }
    return out;
}
}
TEST_CASE("SSTV auto RF selects USB LSB and NFM with retained VIS", "[sstv][sstv-rf]") {
    for(const auto mode:{"USB","LSB","NFM"}) {
        CAPTURE(mode);
        const auto iq=rf(mode);
        SstvRfRouter router(48000,14230000,14230000);
        const auto audio=run(router,iq,4096);
        CHECK(router.selectedMode()==mode);
        REQUIRE(!audio.empty());
        SstvVisDetector vis(48000); size_t found=0;
        for(size_t i=0;i<audio.size();i+=4096)
            found+=vis.process(std::span(audio.data()+i,std::min(size_t{4096},audio.size()-i))).size();
        CHECK(found==1);
    }
}
TEST_CASE("SSTV manual RF supports AM and does not wait for a header", "[sstv][sstv-rf]") {
    for(const auto mode:{"USB","LSB","NFM","AM"}) {
        CAPTURE(mode);
        SstvRfRouter router(48000,14230000,14230000,mode);
        auto iq=rf(mode,48000,false); iq.resize(4800);
        CHECK(!run(router,iq,137).empty());
        CHECK(router.selectedMode()==mode);
    }
}
TEST_CASE("SSTV auto RF rejects corrupt headers and ambiguous sidebands", "[sstv][sstv-rf]") {
    SstvRfRouter invalid(48000,14230000,14230000);
    CHECK(run(invalid,rf("USB",48000,false),4096).empty());
    CHECK(invalid.selectedMode()=="searching");
    SstvRfRouter ambiguous(48000,14230000,14230000);
    CHECK_THROWS_WITH(run(ambiguous,rf("AM"),4096),"Ambiguous SSTV RF header; select USB, LSB, NFM or AM manually");
}
TEST_CASE("SSTV RF refuses discontinuities and invalid sources", "[sstv][sstv-rf]") {
    CHECK_THROWS(SstvRfRouter(48000,14230000,14230000,"WFM"));
    CHECK_THROWS(SstvRfRouter(48000,14230000,14270000));
    CHECK_THROWS(SstvRfRouter(0,14230000,14230000));
    SstvRfRouter router(48000,14230000,14230000);
    router.process(std::vector<std::complex<float>>(100),0,17);
    CHECK_THROWS(router.process(std::vector<std::complex<float>>(100),101,17));
    CHECK_THROWS(router.process(std::vector<std::complex<float>>(100),100,18));
    CHECK_THROWS(router.process(std::vector<std::complex<float>>(100),100,17,true));
}

TEST_CASE("SSTV RF acquisition survives long waits and irregular IQ partitions", "[sstv][sstv-rf]") {
    const auto iq=rf("LSB",48000,true,3000);
    SstvRfRouter first(48000,14230000,14230000),second(48000,14230000,14230000);
    const auto a=run(first,iq,4096),b=run(second,iq,137);
    REQUIRE(first.selectedMode()=="LSB"); REQUIRE(second.selectedMode()=="LSB");
    // Acquisition returns a bounded pre-roll, whose beginning may differ by one
    // input block. The common sample-clock suffix must not duplicate or stretch.
    const auto common=std::min(a.size(),b.size()); REQUIRE(common>48000);
    double error=0;
    for(size_t i=0;i<common;++i) error=std::max(error,std::abs(double(a[a.size()-common+i]-b[b.size()-common+i])));
    CHECK(error<0.0001);
}

TEST_CASE("SSTV RF acquisition at SDR rate and an offset channel", "[sstv][sstv-rf]") {
    constexpr unsigned rate=2048000;
    for(const auto mode:{"USB","LSB","NFM"}) {
        CAPTURE(mode);
        auto iq=rf(mode,rate);
        for(size_t i=0;i<iq.size();++i)
            iq[i]*=std::polar(1.f,float(std::remainder(2*std::numbers::pi*12000*i/rate,2*std::numbers::pi)));
        SstvRfRouter router(rate,14230000,14242000);
        const auto start=std::chrono::steady_clock::now();
        const auto audio=run(router,iq,65536);
        const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::cout<<"SSTV RF "<<mode<<" 2.048 MS/s: "<<elapsed<<" s for "<<double(iq.size())/rate<<" s IQ\n";
        CHECK(router.selectedMode()==mode); CHECK(!audio.empty());
    }
}

TEST_CASE("SSTV RF independent recording adapter", "[sstv-rf-recording]") {
    const char* input=std::getenv("SDR_TOWN_SSTV_RF_IQ");
    if(!input) SKIP("Independent modulated recording supplied by test_sstv_rf.py");
    const char* output=std::getenv("SDR_TOWN_SSTV_RF_PCM"); REQUIRE(output);
    const char* expected=std::getenv("SDR_TOWN_SSTV_RF_EXPECT"); REQUIRE(expected);
    std::ifstream file(input,std::ios::binary); REQUIRE(file.good());
    std::ofstream pcm(output,std::ios::binary); REQUIRE(pcm.good());
    const char* requested=std::getenv("SDR_TOWN_SSTV_RF_REQUEST");
    SstvRfRouter router(96000,14230000,14230000,requested?requested:"auto");
    uint64_t first=0;
    for(;;) {
        std::vector<std::complex<float>> iq(router.maxInputSamples());
        file.read(reinterpret_cast<char*>(iq.data()),std::streamsize(iq.size()*sizeof(iq[0])));
        REQUIRE(file.gcount()%sizeof(iq[0])==0);
        iq.resize(size_t(file.gcount())/sizeof(iq[0]));
        if(iq.empty()) break;
        router.process(iq,first,7); first+=iq.size();
        while(auto block=router.pop()) for(size_t i=0;i<block->count;++i) {
            const int16_t value=int16_t(std::lround(std::clamp(block->samples[i],-1.f,32767.f/32768.f)*32768));
            pcm.write(reinterpret_cast<const char*>(&value),sizeof(value));
        }
    }
    REQUIRE(router.selectedMode()==expected); REQUIRE(pcm.good());
}
