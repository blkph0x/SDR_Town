#include "DcsDecoder.h"
#include "Demod.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace {
constexpr uint32_t mask=(1u<<23)-1;
std::vector<uint8_t> bitsFor(uint32_t word,size_t count=230) {
    std::vector<uint8_t> bits(count);
    for (size_t n=0;n<count;++n) bits[n]=(word>>(n%23))&1;
    return bits;
}
bool contains(const std::vector<DcsIdentity>& ids,uint16_t code,bool inverted=false) {
    return std::find(ids.begin(),ids.end(),DcsIdentity{code,inverted})!=ids.end();
}
std::vector<float> waveform(uint32_t word,unsigned rate=8000,double error=0,bool impair=false) {
    std::vector<float> samples(rate*3); double shaped=0; uint32_t random=12345;
    for (size_t n=0;n<samples.size();++n) {
        const size_t bit=size_t(n*134.4*(1+error)/rate)%23;
        const double value=((word>>bit)&1) ? .15 : -.15;
        shaped+=(1-std::exp(-2*std::numbers::pi*200/rate))*(value-shaped);
        random^=random<<13; random^=random>>17; random^=random<<5;
        samples[n]=float(shaped + (impair ? .3+.02*double(int32_t(random))/2147483648.+
            .3*std::sin(2*std::numbers::pi*1000*n/rate) : 0));
    }
    return samples;
}
DcsSnapshot run(const std::vector<float>& samples,double rate=8000,size_t chunk=137) {
    DcsDecoder decoder;
    for (size_t n=0;n<samples.size();n+=chunk)
        REQUIRE(decoder.process(std::span(samples.data()+n,std::min(chunk,samples.size()-n)),rate,476.4625e6,1,n,n==0));
    return decoder.snapshot();
}
}
TEST_CASE("DCS algebra matches independent ETSI vectors", "[dcs]") {
    REQUIRE(DcsBitDecoder::codes().size()==105);
    REQUIRE(DcsBitDecoder::encode(0023)==0b11101100011100000010011);
    REQUIRE(DcsBitDecoder::encode(0025)==0b11010110111100000010101);
    REQUIRE(DcsBitDecoder::encode(0026)==0b11001011101100000010110);
    REQUIRE(DcsBitDecoder::encode(0754)==0b01000001111100111101100);
    REQUIRE_THROWS_AS(DcsBitDecoder::encode(01000),std::invalid_argument);
    REQUIRE(dcsLabel({0023,false})=="023N"); REQUIRE(dcsLabel({0754,true})=="754I");
}
TEST_CASE("DCS handles all code rotations polarities and cyclic aliases", "[dcs]") {
    for (auto code:DcsBitDecoder::codes()) for (bool invert:{false,true}) {
        uint32_t word=DcsBitDecoder::encode(code)^(invert?mask:0);
        for (int rotation=0;rotation<23;++rotation) {
            INFO("code="<<code<<" inverted="<<invert<<" rotation="<<rotation);
            DcsBitDecoder decoder; REQUIRE(decoder.process(bitsFor(word)));
            REQUIRE(contains(decoder.identities(),code,invert));
            REQUIRE(decoder.identities()==DcsBitDecoder::aliases(word));
            word=(word>>1)|((word&1)<<22);
        }
    }
    REQUIRE(DcsBitDecoder::aliases(DcsBitDecoder::encode(0023)).size()>1);
}
TEST_CASE("DCS requires repeated words and expires resets switches and rejects corrupt bits", "[dcs]") {
    const auto word=DcsBitDecoder::encode(0023); auto bits=bitsFor(word);
    DcsBitDecoder decoder;
    REQUIRE(decoder.process(std::span(bits.data(),68))); REQUIRE(decoder.identities().empty());
    REQUIRE(decoder.process(std::span(bits.data()+68,1))); REQUIRE(contains(decoder.identities(),0023));
    REQUIRE(decoder.process(std::vector<uint8_t>(69))); REQUIRE(decoder.identities().empty());
    for (int bit=0;bit<23;++bit) {
        decoder.reset(); REQUIRE(decoder.process(bitsFor(word^(1u<<bit)))); REQUIRE(decoder.identities().empty());
    }
    decoder.reset(); for (auto bit:bits) REQUIRE(decoder.process(std::span(&bit,1)));
    REQUIRE(contains(decoder.identities(),0023));
    REQUIRE(decoder.process(bitsFor(DcsBitDecoder::encode(0754)))); REQUIRE(contains(decoder.identities(),0754));
    REQUIRE_FALSE(contains(decoder.identities(),0023));
    REQUIRE_FALSE(decoder.process(std::vector<uint8_t>{2})); REQUIRE(decoder.identities().empty());
}
TEST_CASE("DCS discriminator detects all common codes with physical inversion", "[dcs]") {
    for (auto code:DcsBitDecoder::codes()) for (bool invert:{false,true}) {
        INFO("code="<<code<<" invert="<<invert);
        REQUIRE(contains(run(waveform(DcsBitDecoder::encode(code)^(invert?mask:0))).identities,code,invert));
    }
}
TEST_CASE("DCS discriminator tolerates measured shaping speech DC and baud error", "[dcs]") {
    for (unsigned rate:{8000u,48000u,96000u}) for (double error:{-.001,0.,.001}) {
        INFO("rate="<<rate<<" error="<<error);
        const auto samples=waveform(0b11101100011100000010011,rate,error,true);
        REQUIRE(contains(run(samples,rate,137).identities,0023));
        REQUIRE(run(samples,rate,4096).identities==run(samples,rate,137).identities);
    }
}
TEST_CASE("DCS rejects noise and clears state on input discontinuity", "[dcs]") {
    DcsDecoder decoder; auto input=waveform(DcsBitDecoder::encode(0023));
    REQUIRE(decoder.process(input,8000,1e8,1,0,true)); REQUIRE_FALSE(decoder.snapshot().identities.empty());
    REQUIRE(decoder.process(std::span(input.data(),80),8000,1e8,1,input.size()+1,false));
    REQUIRE(decoder.snapshot().identities.empty()); REQUIRE(decoder.snapshot().resets==2);
    REQUIRE(decoder.process(input,8000,1e8,2,0,false)); REQUIRE_FALSE(decoder.snapshot().identities.empty());
    REQUIRE(decoder.process(std::span(input.data(),80),8000,2e8,2,input.size(),false));
    REQUIRE(decoder.snapshot().identities.empty());
    input[0]=std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(decoder.process(input,8000,2e8,2,0,true)); REQUIRE(decoder.snapshot().identities.empty());
    REQUIRE(run(std::vector<float>(24000)).identities.empty());
    uint32_t random=9876; std::vector<float> block(8000); decoder.reset();
    for (uint64_t second=0;second<120;++second) {
        for (auto& value:block) { random^=random<<13; random^=random>>17; random^=random<<5; value=float(int32_t(random))/2147483648.f; }
        REQUIRE(decoder.process(block,8000,1e8,1,second*8000,second==0)); REQUIRE(decoder.snapshot().identities.empty());
    }
}

TEST_CASE("DCS does not mistake subaudible sine tones for repeated codewords", "[dcs]") {
    for (double frequency:{67.,100.,123.,134.4,250.3,1000.}) {
        std::vector<float> samples(8000*5);
        for (size_t n=0;n<samples.size();++n) samples[n]=float(std::sin(2*std::numbers::pi*frequency*n/8000));
        REQUIRE(run(samples).identities.empty());
    }
}

TEST_CASE("DCS receives raw NFM before squelch without changing speaker PCM", "[dcs][demod]") {
    constexpr unsigned rate=48000;
    const auto data=waveform(0b11101100011100000010011,rate);
    std::vector<std::complex<float>> iq(data.size()); double phase=0;
    for (size_t n=0;n<iq.size();++n) {
        phase+=2*std::numbers::pi*2500*data[n]/rate;
        iq[n]={float(std::cos(phase)),float(std::sin(phase))};
    }
    Demodulator tapped,plain; DcsDecoder decoder;
    for (size_t n=0;n<iq.size();n+=1200) {
        std::vector<std::complex<float>> chunk(iq.begin()+n,iq.begin()+n+1200);
        FmMultiplexBlock raw; double rms=0;
        auto audio=[&](Demodulator& demod,FmMultiplexBlock* output) {
            return demod.demodulateToAudio(chunk,rate,100e6,100e6,DemodMode::NFM,rms,
                3000,0,1,75,.96,12500,0,48000,std::numeric_limits<double>::quiet_NaN(),true,output,100e6);
        };
        REQUIRE(audio(tapped,&raw)==audio(plain,nullptr));
        REQUIRE(decoder.process(raw.samples,raw.sampleRate,raw.targetHz,raw.epoch,raw.firstSample,raw.discontinuity));
    }
    REQUIRE(contains(decoder.snapshot().identities,0023));
}
