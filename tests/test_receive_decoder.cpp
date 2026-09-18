#include "ReceiveDecoder.h"
#include "miniaudio.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>
#include <stdexcept>

TEST_CASE("Receive registry exposes only implemented bounded adapters", "[decoder]") {
    std::set<std::string_view> names;
    REQUIRE(receiveDecoderRegistry().size()==3);
    for (const auto& entry:receiveDecoderRegistry()) {
        REQUIRE(names.insert(entry.id).second);
        auto decoder=createReceiveDecoder(entry.id);
        REQUIRE(&decoder->descriptor()==&entry);
        REQUIRE(entry.contractVersion==1);
        REQUIRE(entry.maximumBlockSamples==262144);
        REQUIRE(entry.maximumRateHz>=entry.minimumRateHz);
        REQUIRE(decoderInputName(entry.input)!="unknown");
    }
    REQUIRE_THROWS_AS(createReceiveDecoder("sstv"),std::invalid_argument);
    REQUIRE_THROWS_AS(createReceiveDecoder("p25"),std::invalid_argument);
    REQUIRE_THROWS_AS(createReceiveDecoder("../arbitrary.dll"),std::invalid_argument);
}

TEST_CASE("Receive contract rejects invalid metadata and clears stale tone results", "[decoder]") {
    auto decoder=createReceiveDecoder("ctcss");
    std::vector<float> tone(24000);
    for (size_t n=0;n<tone.size();++n) tone[n]=float(std::sin(2*std::numbers::pi*100*n/8000));
    ReceiveDecoderBlock valid{tone,DecoderInputDomain::FmDiscriminator,8000,100e6,7,1,0,true};
    auto acquired=[&] {
        REQUIRE(bool(decoder->process(valid)));
        REQUIRE(std::get<CtcssSnapshot>(decoder->snapshot()).frequencyHz==100);
    };
    auto rejected=[&](ReceiveDecoderBlock input,DecoderInputError error) {
        acquired(); const auto result=decoder->process(input);
        REQUIRE_FALSE(bool(result)); REQUIRE(result.error==error); REQUIRE_FALSE(result.detail.empty());
        REQUIRE(std::get<CtcssSnapshot>(decoder->snapshot()).frequencyHz==0);
    };
    auto bad=valid; bad.contractVersion=2; rejected(bad,DecoderInputError::ContractVersion);
    bad=valid; bad.domain=DecoderInputDomain::FmMultiplex; rejected(bad,DecoderInputError::InputDomain);
    bad=valid; bad.sampleRateHz=1000; rejected(bad,DecoderInputError::InvalidBlock);
    bad=valid; bad.sampleRateHz=std::numeric_limits<double>::quiet_NaN(); rejected(bad,DecoderInputError::InvalidBlock);
    bad=valid; bad.targetHz=std::numeric_limits<double>::infinity(); rejected(bad,DecoderInputError::InvalidBlock);
    bad=valid; bad.firstSample=std::numeric_limits<uint64_t>::max(); rejected(bad,DecoderInputError::InvalidBlock);
    std::vector<float> oversized(262145); bad=valid; bad.samples=oversized; rejected(bad,DecoderInputError::InvalidBlock);
    acquired(); tone[0]=std::numeric_limits<float>::quiet_NaN();
    const auto invalid=decoder->process(valid);
    REQUIRE(invalid.error==DecoderInputError::BackendRejected);
    REQUIRE(std::get<CtcssSnapshot>(decoder->snapshot()).frequencyHz==0);
}

TEST_CASE("Receive source identity prevents cross-device history inheritance", "[decoder]") {
    auto decoder=createReceiveDecoder("ctcss");
    std::vector<float> tone(24000);
    for (size_t n=0;n<tone.size();++n) tone[n]=float(std::sin(2*std::numbers::pi*100*n/8000));
    REQUIRE(bool(decoder->process({tone,DecoderInputDomain::FmDiscriminator,8000,100e6,1,1,0,true})));
    const auto before=std::get<CtcssSnapshot>(decoder->snapshot());
    REQUIRE(before.frequencyHz==100);
    REQUIRE(bool(decoder->process({std::span(tone.data(),80),DecoderInputDomain::FmDiscriminator,8000,100e6,2,1,24000,false})));
    const auto after=std::get<CtcssSnapshot>(decoder->snapshot());
    REQUIRE(after.frequencyHz==0); REQUIRE(after.resets==before.resets+1); REQUIRE(after.samples==80);
    decoder->reset(); REQUIRE(std::get<CtcssSnapshot>(decoder->snapshot()).status=="Inactive");
}

TEST_CASE("DCS adapter matches native discriminator decoding across partitions", "[decoder]") {
    std::vector<float> data(40000);
    constexpr uint32_t word=0b11101100011100000010011;
    for (size_t n=0;n<data.size();++n) data[n]=((word>>((n*672/(5*8000))%23))&1) ? .2f : -.2f;
    for (size_t chunk:{137u,4096u}) {
        auto adapter=createReceiveDecoder("dcs"); DcsDecoder native;
        for (size_t n=0;n<data.size();n+=chunk) {
            const auto samples=std::span(data.data()+n,std::min(chunk,data.size()-n));
            REQUIRE(bool(adapter->process({samples,DecoderInputDomain::FmDiscriminator,8000,100e6,9,1,n,n==0})));
            REQUIRE(native.process(samples,8000,100e6,1,n,n==0));
            const auto a=std::get<DcsSnapshot>(adapter->snapshot()), b=native.snapshot();
            REQUIRE(a.identities==b.identities); REQUIRE(a.samples==b.samples);
            REQUIRE(a.agreeingPhases==b.agreeingPhases); REQUIRE(a.resets==b.resets);
        }
        REQUIRE_FALSE(std::get<DcsSnapshot>(adapter->snapshot()).identities.empty());
    }
}

#ifdef SDR_TOWN_TEST_RDS_DSP
namespace {
void compare(const RdsMpxSnapshot& a,const RdsMpxSnapshot& b) {
    REQUIRE(a.bits==b.bits); REQUIRE(a.groups==b.groups); REQUIRE(a.samples==b.samples);
    REQUIRE(a.correctedBlocks==b.correctedBlocks); REQUIRE(a.rejectedGroups==b.rejectedGroups);
    REQUIRE(a.resets==b.resets); REQUIRE(a.lastGroupWords==b.lastGroupWords);
    REQUIRE(a.targetHz==b.targetHz); REQUIRE(a.sampleRate==b.sampleRate); REQUIRE(a.status==b.status);
    REQUIRE(a.station.identified==b.station.identified); REQUIRE(a.station.pi==b.station.pi);
    REQUIRE(a.station.pty==b.station.pty); REQUIRE(a.station.trafficProgramme==b.station.trafficProgramme);
    REQUIRE(a.station.trafficAnnouncement==b.station.trafficAnnouncement);
    REQUIRE(a.station.programmeService==b.station.programmeService); REQUIRE(a.station.radioText==b.station.radioText);
    REQUIRE((a.lastGroupMs!=0)==(b.lastGroupMs!=0)); // Wall processing times cannot be identical.
}
}
TEST_CASE("RDS adapter equals native recorded MPX decoder at every block and reset", "[decoder][rds]") {
    ma_decoder reader{}; auto config=ma_decoder_config_init(ma_format_f32,0,0);
    REQUIRE(ma_decoder_init_file(SDR_TOWN_RDS_FIXTURE,&config,&reader)==MA_SUCCESS);
    struct Guard { ma_decoder* d; ~Guard(){ma_decoder_uninit(d);} } guard{&reader};
    REQUIRE(reader.outputChannels==1);
    std::vector<float> data; std::array<float,8192> buffer{};
    for (;;) {
        ma_uint64 count=0; const auto result=ma_decoder_read_pcm_frames(&reader,buffer.data(),buffer.size(),&count);
        REQUIRE((result==MA_SUCCESS || result==MA_AT_END));
        if (!count) break;
        data.insert(data.end(),buffer.begin(),buffer.begin()+count);
        REQUIRE(data.size()<=reader.outputSampleRate*2u);
    }
    for (size_t chunk:{137u,1000u,8192u}) {
        auto adapter=createReceiveDecoder("rds"); RdsMpxDecoder native;
        for (size_t n=0;n<data.size();n+=chunk) {
            auto samples=std::span(data.data()+n,std::min(chunk,data.size()-n));
            REQUIRE(bool(adapter->process({samples,DecoderInputDomain::FmMultiplex,double(reader.outputSampleRate),100e6,1,1,n,n==0})));
            REQUIRE(native.process(samples,reader.outputSampleRate,100e6,1,n,n==0));
            compare(std::get<RdsMpxSnapshot>(adapter->snapshot()),native.snapshot());
        }
        REQUIRE(native.snapshot().groups==2);
        // Same epoch/cursor but different source must behave like explicit reset.
        auto samples=std::span(data.data(),size_t(137));
        REQUIRE(bool(adapter->process({samples,DecoderInputDomain::FmMultiplex,double(reader.outputSampleRate),100e6,2,1,data.size(),false})));
        REQUIRE(native.process(samples,reader.outputSampleRate,100e6,1,data.size(),true));
        compare(std::get<RdsMpxSnapshot>(adapter->snapshot()),native.snapshot());
        // Missing samples, changed rate/frequency/epoch also retain native semantics.
        for (int change=0;change<4;++change) {
            const double changedRate=reader.outputSampleRate==192000 ? 256000 : 192000;
            const double rate=change==1?changedRate:reader.outputSampleRate;
            const double target=change>=2?101e6:100e6;
            const uint64_t epoch=change==3?2:1, first=data.size()+10000+change*137;
            REQUIRE(bool(adapter->process({samples,DecoderInputDomain::FmMultiplex,rate,target,2,epoch,first,false})));
            REQUIRE(native.process(samples,rate,target,epoch,first,false));
            compare(std::get<RdsMpxSnapshot>(adapter->snapshot()),native.snapshot());
        }
    }
}
#endif
