#include "RdsDecoder.h"
#include "Demod.h"
#include "RdsMpxDecoder.h"
#include "RdsMpxFile.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>

namespace {
// Test-side polynomial encoder, independent of redsea's parity-check matrix.
// IEC 62106 polynomial and offset words, also documented in redsea block_sync.cc.
uint32_t encode(uint16_t data, uint16_t offset) {
    uint32_t word = uint32_t{data} << 10, remainder = word;
    for (int i = 25; i >= 10; --i)
        if (remainder & (uint32_t{1} << i)) remainder ^= 0x5b9u << (i-10);
    return word | (remainder ^ offset);
}
void group(RdsDecoder& decoder, uint16_t pi, uint16_t b, uint16_t c, uint16_t d) {
    const std::array<uint16_t,4> words{pi,b,c,d};
    const std::array<uint16_t,4> offsets{0xfc,0x198,static_cast<uint16_t>((b&0x800)?0x350:0x168),0x1b4};
    for (unsigned i=0;i<4;++i) {
        const auto word=encode(words[i],offsets[i]);
        for (int bit=25;bit>=0;--bit) decoder.pushBit(((word>>bit)&1)!=0);
    }
}
void identify(RdsDecoder& d, uint16_t pi=0x22e1) {
    for (int i=0;i<6;++i) group(d,pi,0x0400,0,0x2020);
}
}

TEST_CASE("RDS matches redsea independent bit fixture and resets", "[rds]") {
    // Fixture from redsea test/components-bits.cc, same pinned revision;
    // upstream license retained in external/redsea-block/LICENSE.
    const std::string bits = "00100010111000010111001100"
        "00100101100000111100111110" "00100000011001011011010011"
        "01101001001000000110111110";
    RdsDecoder decoder;
    unsigned count=0;
    for (int repeat=0;repeat<5;++repeat) for(char b:bits) {
        if (auto event=decoder.pushBit(b=='1')) {
            REQUIRE(event->words == std::array<uint16_t,4>{0x22e1,0x2583,0x2065,0x6920});
            ++count;
        }
    }
    REQUIRE(count >= 3);
    REQUIRE(decoder.station().identified);
    REQUIRE(decoder.station().pi == 0x22e1);
    decoder.reset();
    REQUIRE_FALSE(decoder.station().identified);
    REQUIRE(decoder.station().radioText.empty());
    REQUIRE(decoder.rejectedGroups()==0);
}

TEST_CASE("RDS publishes repeated complete PS and never mixes stations", "[rds]") {
    RdsDecoder decoder; identify(decoder);
    const std::string name="SDR TOWN";
    for (int pass=0;pass<2;++pass) {
        for(unsigned seg=0;seg<4;++seg)
            group(decoder,0x22e1,static_cast<uint16_t>(0x400|seg),0,
                static_cast<uint16_t>((name[2*seg]<<8)|name[2*seg+1]));
        if (!pass) REQUIRE(decoder.station().programmeService.empty());
    }
    REQUIRE(decoder.station().programmeService==name);
    group(decoder,0x1234,0x400,0,0x4142);
    REQUIRE_FALSE(decoder.station().identified);
    REQUIRE(decoder.station().programmeService.empty());
    identify(decoder,0x1234);
    REQUIRE(decoder.station().pi==0x1234);
}

TEST_CASE("RDS redsea FEC corrects short errors and rejects long corruption", "[rds]") {
    const std::string good = "00100010111000010111001100"
        "00100101100000111100111110" "00100000011001011011010011"
        "01101001001000000110111110";
    for (bool correctable : {true,false}) {
        RdsDecoder decoder;
        for (int i=0;i<5;++i) for(char b:good) decoder.pushBit(b=='1');
        auto broken=good;
        for (unsigned bit : {1u,2u}) broken[bit] = broken[bit]=='1'?'0':'1';
        if (!correctable)
            for (unsigned bit : {9u,10u}) broken[bit] = broken[bit]=='1'?'0':'1';
        std::optional<RdsGroupEvent> result;
        for(char b:broken) if(auto event=decoder.pushBit(b=='1')) result=event;
        if(correctable) {
            REQUIRE(result.has_value());
            REQUIRE(result->words[0]==0x22e1);
            REQUIRE(result->correctedBlocks==1);
        } else {
            REQUIRE_FALSE(result.has_value());
            REQUIRE(decoder.rejectedGroups()>0);
        }
    }
}

TEST_CASE("RDS text needs contiguous segments and clears on AB or version change", "[rds]") {
    RdsDecoder decoder; identify(decoder);
    group(decoder,0x22e1,0x2401,0x4f0d,0x2020); // O CR, segment 1 first
    REQUIRE(decoder.station().radioText.empty());
    group(decoder,0x22e1,0x2400,0x4845,0x4c4c);
    REQUIRE(decoder.station().radioText=="HELLO");
    group(decoder,0x22e1,0x2411,0x4f0d,0x2020); // A/B flips
    REQUIRE(decoder.station().radioText.empty());
    group(decoder,0x22e1,0x2c10,0x22e1,0x410d); // version B, one segment
    REQUIRE(decoder.station().radioText=="A");
    group(decoder,0x22e1,0x2c10,0x22e1,0x4243);
    REQUIRE(decoder.station().radioText.empty());
    group(decoder,0x22e1,0x2c11,0x22e1,0x0d20);
    REQUIRE(decoder.station().radioText=="BC");
    group(decoder,0x22e1,0x2c10,0x9999,0x420d); // wrong repeated PI
    REQUIRE(decoder.station().radioText=="BC");
    REQUIRE(decoder.rejectedGroups()>0);
}

TEST_CASE("RDS noise cannot identify station or publish text", "[rds]") {
    RdsDecoder decoder;
    uint32_t state=0x193a6;
    unsigned complete=0;
    for(unsigned i=0;i<100000;++i) {
        state ^= state<<13; state ^= state>>17; state ^= state<<5;
        if(decoder.pushBit((state&1)!=0)) ++complete;
    }
    REQUIRE_FALSE(decoder.station().identified);
    REQUIRE(decoder.station().programmeService.empty());
    REQUIRE(complete==0);
}

TEST_CASE("WFM multiplex tap preserves audio and exposes reset provenance", "[rds][demod]") {
    constexpr double rate=384000, pi=3.141592653589793;
    std::vector<std::complex<float>> iq(7680);
    double phase=0;
    for(size_t i=0;i<iq.size();++i) {
        phase += 2*pi*75000/rate * (0.2*std::sin(2*pi*1000*i/rate)+0.03*std::sin(2*pi*57000*i/rate));
        iq[i]={static_cast<float>(std::cos(phase)),static_cast<float>(std::sin(phase))};
    }
    Demodulator tapped, plain;
    FmMultiplexBlock mpx;
    auto run=[&](Demodulator& d,FmMultiplexBlock* tap,DemodMode mode=DemodMode::WFM) {
        double rms;
        return d.demodulateToAudio(iq,rate,100e6,100e6,mode,rms,3000,-200,1,75,.96,
            180000,0,48000,std::numeric_limits<double>::quiet_NaN(),true,tap);
    };
    REQUIRE(run(tapped,&mpx)==run(plain,nullptr));
    REQUIRE(mpx.sampleRate==192000);
    REQUIRE(mpx.samples.size()==3840);
    REQUIRE(mpx.discontinuity);
    REQUIRE(mpx.firstSample==0);
    const auto epoch=mpx.epoch;
    double re=0,im=0;
    for(size_t i=200;i<mpx.samples.size();++i) {
        re += mpx.samples[i]*std::cos(2*pi*57000*i/mpx.sampleRate);
        im += mpx.samples[i]*std::sin(2*pi*57000*i/mpx.sampleRate);
    }
    REQUIRE(std::hypot(re,im)/(mpx.samples.size()-200) > 0.005);
    REQUIRE(run(tapped,&mpx)==run(plain,nullptr));
    REQUIRE_FALSE(mpx.discontinuity);
    REQUIRE(mpx.firstSample==3840);
    REQUIRE(mpx.epoch==epoch);
    tapped.resetMultiplexState();
    REQUIRE(run(tapped,&mpx)==run(plain,nullptr));
    REQUIRE(mpx.discontinuity);
    REQUIRE(mpx.epoch!=epoch);
    tapped.resetState(); plain.resetState();
    REQUIRE(run(tapped,&mpx)==run(plain,nullptr));
    REQUIRE(mpx.discontinuity);
    REQUIRE(mpx.epoch>epoch);
    run(tapped,&mpx,DemodMode::NFM);
    REQUIRE_FALSE(mpx.samples.empty());
    REQUIRE(mpx.discontinuity);
    run(tapped,&mpx);
    REQUIRE(mpx.discontinuity);
    run(tapped,nullptr);
    run(tapped,&mpx);
    REQUIRE(mpx.discontinuity);
    iq.pop_back(); // DEC-0079: decimation phase survives an odd-sized block.
    run(tapped,&mpx);
    const auto beforeGap = mpx.epoch;
    run(tapped,&mpx);
    REQUIRE_FALSE(mpx.discontinuity);
    REQUIRE(mpx.epoch==beforeGap);
}

TEST_CASE("WFM multiplex timing and samples are independent of IQ chunk size", "[rds][mpx-partition]") {
    constexpr double rate=384000, pi=3.141592653589793;
    std::vector<std::complex<float>> iq(15360);
    double phase=0;
    for(size_t i=0;i<iq.size();++i) {
        phase += 2*pi*75000/rate*(0.2*std::sin(2*pi*1000*i/rate)+0.03*std::sin(2*pi*57000*i/rate));
        iq[i]={static_cast<float>(std::cos(phase)),static_cast<float>(std::sin(phase))};
    }
    auto decode=[&](size_t chunk) {
        Demodulator demod;
        std::vector<float> samples;
        for(size_t begin=0;begin<iq.size();begin+=chunk) {
            std::vector<std::complex<float>> part(iq.begin()+begin,iq.begin()+std::min(begin+chunk,iq.size()));
            FmMultiplexBlock mpx; double rms;
            demod.demodulateToAudio(part,rate,100e6,100e6,DemodMode::WFM,rms,15000,-200,1,75,.96,
                180000,0,48000,std::numeric_limits<double>::quiet_NaN(),true,&mpx);
            if(begin) REQUIRE_FALSE(mpx.discontinuity);
            samples.insert(samples.end(),mpx.samples.begin(),mpx.samples.end());
        }
        return samples;
    };
    const auto whole=decode(iq.size());
    for(size_t chunk:{137u,1000u}) {
        const auto pieces=decode(chunk);
        REQUIRE(pieces.size()==whole.size());
        double maxError=0;
        for(size_t i=0;i<whole.size();++i) maxError=std::max(maxError,std::abs(double(pieces[i]-whole[i])));
        INFO("chunk="<<chunk<<" maxError="<<maxError);
        REQUIRE(maxError < 1e-5);
    }
}

#ifdef SDR_TOWN_TEST_RDS_DSP
TEST_CASE("RDS MPX recorded reference decodes through shipped DLL at arbitrary boundaries", "[rds][mpx]") {
    const auto reference=decodeRdsMpxFile(SDR_TOWN_RDS_FIXTURE);
    INFO(reference.status << " groups=" << reference.groups << " PI=" << reference.station.pi);
    // Upstream's 0.7-second fixture promises two groups, insufficient for our
    // three-group identity confirmation. Check the actual CRC-valid payload.
    REQUIRE(reference.groups==2);
    REQUIRE_FALSE(reference.station.identified);
    REQUIRE(reference.lastGroupWords[0]==0x6201);
    REQUIRE(((reference.lastGroupWords[1] >> 5) & 31)==14);
    for(size_t chunk:{137u,1000u,4096u}) {
        const auto test=decodeRdsMpxFile(SDR_TOWN_RDS_FIXTURE,chunk);
        REQUIRE(test.bits==reference.bits);
        REQUIRE(test.groups==reference.groups);
        REQUIRE(test.correctedBlocks==reference.correctedBlocks);
        REQUIRE(test.lastGroupWords==reference.lastGroupWords);
        REQUIRE(test.station.pi==reference.station.pi);
        REQUIRE(test.station.radioText==reference.station.radioText);
    }
}

TEST_CASE("RDS MPX rejects bad input and clears identity on a discontinuity", "[rds][mpx]") {
    RdsMpxDecoder decoder;
    std::vector<float> silence(8192);
    REQUIRE_FALSE(decoder.process(silence,48000,100e6,1,0,true));
    REQUIRE(decoder.process(silence,192000,100e6,1,0,true));
    REQUIRE_FALSE(decoder.snapshot().station.identified);
    const auto first=decoder.snapshot().resets;
    REQUIRE(decoder.process(silence,192000,100e6,1,16384,false)); // Missing block
    REQUIRE(decoder.snapshot().resets==first+1);
    silence[4]=std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(decoder.process(silence,192000,100e6,1,24576,false));
    REQUIRE(decoder.snapshot().status=="Invalid MPX block");
    decoder.reset();
    REQUIRE(decoder.snapshot().status=="Disabled");
    REQUIRE(decoder.snapshot().resets==first+1);
}
#endif
