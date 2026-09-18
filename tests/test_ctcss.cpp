#include "CtcssDecoder.h"
#include "Demod.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <numbers>

namespace {
std::vector<float> signal(double hz, unsigned rate=8000, double gain=.1, double voice=0) {
    std::vector<float> result(rate*3);
    for (size_t i=0;i<result.size();++i) result[i]=static_cast<float>(
        gain*std::sin(2*std::numbers::pi*hz*i/rate)+voice*std::sin(2*std::numbers::pi*1000*i/rate));
    return result;
}
CtcssSnapshot decode(const std::vector<float>& input, unsigned rate=8000, size_t chunk=4096) {
    CtcssDecoder decoder;
    for (size_t i=0;i<input.size();i+=chunk) {
        const auto count=std::min(chunk,input.size()-i);
        REQUIRE(decoder.process(std::span(input.data()+i,count),rate,476.4625e6,1,i,i==0));
    }
    return decoder.snapshot();
}
}

TEST_CASE("CTCSS identifies every supported tone without changing chunk results", "[ctcss]") {
    for (double tone:CtcssDecoder::tones()) {
        INFO("tone="<<tone);
        const auto input=signal(tone);
        const auto a=decode(input);
        const auto b=decode(input,8000,137);
        REQUIRE(a.frequencyHz==tone);
        REQUIRE(b.frequencyHz==a.frequencyHz);
        REQUIRE(a.windows==3);
        REQUIRE(b.confirmedWindows==a.confirmedWindows);
    }
}

TEST_CASE("CTCSS has measured gain rate and speech interference tolerance", "[ctcss]") {
    for (unsigned rate:{8000u,48000u,96000u}) for (double gain:{.005,.1,1.0}) {
        INFO("rate="<<rate<<" gain="<<gain);
        REQUIRE(decode(signal(123,rate,gain,gain*6),rate).frequencyHz==123);
    }
}

TEST_CASE("CTCSS rejects silence noise ambiguous tones and unsupported frequencies", "[ctcss]") {
    REQUIRE(decode(std::vector<float>(24000)).frequencyHz==0);
    REQUIRE(decode(signal(1000)).confirmedWindows==0);
    REQUIRE(decode(signal(98.5)).confirmedWindows==0);
    REQUIRE(decode(signal(69.3)).confirmedWindows==0); // Not in classic-38 support.
    auto mixed=signal(100); const auto second=signal(123);
    for (size_t i=0;i<mixed.size();++i) mixed[i]+=second[i];
    REQUIRE(decode(mixed).confirmedWindows==0);
    uint32_t random=12345;
    for (auto& sample:mixed) { random^=random<<13; random^=random>>17; random^=random<<5; sample=float(int32_t(random))/2147483648.f; }
    REQUIRE(decode(mixed).confirmedWindows==0);
}

TEST_CASE("CTCSS clears history on gaps retunes invalid input and tone loss", "[ctcss]") {
    CtcssDecoder decoder; auto input=signal(100);
    REQUIRE(decoder.process(input,8000,100e6,1,0,true));
    REQUIRE(decoder.snapshot().frequencyHz==100);
    REQUIRE(decoder.process(std::span(input.data(),800),8000,100e6,1,input.size()+1,false));
    REQUIRE(decoder.snapshot().frequencyHz==0);
    REQUIRE(decoder.snapshot().resets==2);
    REQUIRE(decoder.process(input,8000,101e6,1,0,false));
    REQUIRE(decoder.snapshot().frequencyHz==100);
    std::vector<float> silence(8000);
    REQUIRE(decoder.process(silence,8000,101e6,1,input.size(),false));
    REQUIRE(decoder.snapshot().frequencyHz==0);
    input[0]=std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(decoder.process(input,8000,101e6,1,32000,false));
    REQUIRE(decoder.snapshot().frequencyHz==0);
    REQUIRE_FALSE(decoder.process(silence,4800,101e6,1,0,true));
}

TEST_CASE("CTCSS rejects two minutes of reproducible noise", "[ctcss][soak]") {
    CtcssDecoder decoder; std::vector<float> block(8000); uint32_t random=0x31415926;
    for (uint64_t second=0;second<120;++second) {
        for (auto& sample:block) {
            random^=random<<13; random^=random>>17; random^=random<<5;
            sample=float(int32_t(random))/2147483648.f;
        }
        REQUIRE(decoder.process(block,8000,476.4625e6,1,second*8000,second==0));
        REQUIRE(decoder.snapshot().confirmedWindows==0);
    }
    REQUIRE(decoder.snapshot().windows==120);
}

TEST_CASE("Nominal NFM tap identity survives continuous AFC without changing audio", "[ctcss][demod]") {
    Demodulator tapped,plain;
    std::vector<std::complex<float>> iq(1200,{1,0});
    uint64_t epoch=0;
    for (int block=0;block<10;++block) {
        FmMultiplexBlock raw; double rms=0;
        auto run=[&](Demodulator& demod,FmMultiplexBlock* output) {
            return demod.demodulateToAudio(iq,48000,100e6,100e6+block*1000,DemodMode::NFM,rms,
                3000,-200,1,75,.96,12500,0,48000,std::numeric_limits<double>::quiet_NaN(),true,output,100e6);
        };
        REQUIRE(run(tapped,&raw)==run(plain,nullptr));
        if (!block) epoch=raw.epoch;
        else { REQUIRE(raw.epoch==epoch); REQUIRE_FALSE(raw.discontinuity); }
        REQUIRE(raw.targetHz==100e6);
    }
}

TEST_CASE("NFM raw tap decodes tone with speech LPF and squelch without audio changes", "[ctcss][demod]") {
    constexpr unsigned rate=48000;
    const auto data=signal(123,rate,.08,.5);
    std::vector<std::complex<float>> iq(data.size()); double phase=0;
    for (size_t i=0;i<iq.size();++i) {
        phase+=2*std::numbers::pi*2500*data[i]/rate;
        iq[i]={float(std::cos(phase)),float(std::sin(phase))};
    }
    Demodulator tapped, plain; CtcssDecoder decoder;
    for (size_t i=0;i<iq.size();i+=1200) {
        std::vector<std::complex<float>> block(iq.begin()+i,iq.begin()+i+1200);
        FmMultiplexBlock raw; double rms=0;
        auto run=[&](Demodulator& demod,FmMultiplexBlock* output) {
            return demod.demodulateToAudio(block,rate,476.4625e6,476.4625e6,DemodMode::NFM,
                rms,3000,0,1,75,.96,12500 + (i/1200 % 2)*100,0,48000,
                std::numeric_limits<double>::quiet_NaN(),true,output,476.4625e6);
        };
        REQUIRE(run(tapped,&raw)==run(plain,nullptr));
        REQUIRE(decoder.process(raw.samples,raw.sampleRate,raw.targetHz,raw.epoch,raw.firstSample,raw.discontinuity));
    }
    REQUIRE(decoder.snapshot().frequencyHz==123);
}
