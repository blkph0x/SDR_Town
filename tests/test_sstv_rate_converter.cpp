#include "SstvRateConverter.h"
#include "miniaudio.h"
#include <catch2/catch_test_macros.hpp>
#include <QFile>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace {
std::vector<float> tone(double rate,double seconds,double frequency=1900) {
    std::vector<float> samples(size_t(rate*seconds));
    for(size_t i=0;i<samples.size();++i)
        samples[i]=float(0.5*std::sin(2*std::numbers::pi*frequency*double(i)/rate));
    return samples;
}
std::vector<float> convert(const std::vector<float>& samples,double rate,size_t chunk) {
    SstvRateConverter converter; converter.start(rate);
    std::vector<float> result;
    for(size_t i=0;i<samples.size();i+=chunk) {
        const auto out=converter.process(std::span(samples.data()+i,std::min(chunk,samples.size()-i)));
        result.insert(result.end(),out.begin(),out.end());
    }
    return result;
}
}

TEST_CASE("SSTV resampler preserves exact samples across caller partitions","[sstv][sstv-rate]") {
    for(const double rate:{8000.,32000.,44100.,2048000./43.,48000.,96000.}) {
        CAPTURE(rate);
        const auto samples=tone(rate,0.25);
        const auto reference=convert(samples,rate,8192);
        for(const size_t chunk:{size_t{1},size_t{137},size_t{4096}}) {
            CAPTURE(chunk);
            REQUIRE(convert(samples,rate,chunk)==reference);
        }
        REQUIRE(std::abs(double(reference.size())-double(samples.size())*48000/rate)<=6);
    }
}

TEST_CASE("SSTV resampler bypass and reset retain no previous stream","[sstv][sstv-rate]") {
    SstvRateConverter converter;
    REQUIRE_THROWS(converter.process(tone(8000,.01)));
    const auto input=tone(48000,.01);
    converter.start(48000);
    REQUIRE(converter.process(input)==input);
    REQUIRE(converter.process({}).empty());
    converter.start(32000); converter.process(input);
    converter.reset(); REQUIRE(converter.effectiveInputRate()==0);
    REQUIRE_THROWS(converter.process(input));
    converter.start(32000);
    SstvRateConverter fresh; fresh.start(32000);
    REQUIRE(converter.process(input)==fresh.process(input));
}

TEST_CASE("SSTV resampler invalid input invalidates state","[sstv][sstv-rate]") {
    SstvRateConverter converter;
    for(const double rate:{0.,7999.,96001.,std::numeric_limits<double>::quiet_NaN()}) {
        converter.start(32000); REQUIRE_THROWS(converter.start(rate));
        REQUIRE(converter.effectiveInputRate()==0);
    }
    for(const float value:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        converter.start(48000);
        const std::vector<float> input{value}; REQUIRE_THROWS(converter.process(input));
        REQUIRE_THROWS(converter.process(std::vector<float>{0}));
    }
    converter.start(32000); REQUIRE_THROWS(converter.process(std::vector<float>(8193)));
    REQUIRE(converter.effectiveInputRate()==0);
}

TEST_CASE("SSTV fractional conversion has bounded session drift","[sstv][sstv-rate]") {
    for(const double rate:{8000.00005,2048000./43.,96000.}) {
        SstvRateConverter converter; converter.start(rate);
        const auto effective=converter.effectiveInputRate();
        REQUIRE(std::abs(effective-rate)<=0.000050001);
        REQUIRE(std::abs(48000*360*(rate/effective-1))<0.109);
        const size_t total=size_t(rate*360);
        const std::vector<float> zeros(8192);
        size_t count=0;
        for(size_t i=0;i<total;i+=zeros.size())
            count+=converter.process(std::span(zeros.data(),std::min(zeros.size(),total-i))).size();
        REQUIRE(std::abs(double(count)-double(total)*48000/effective)<=6);
    }
}

TEST_CASE("SSTV resampler retains in-band tone frequency and level","[sstv][sstv-rate]") {
    for(const double rate:{8000.,2048000./43.,96000.}) {
        for(const double frequency:{1200.,1500.,1900.,2300.}) {
            CAPTURE(rate,frequency);
            const auto out=convert(tone(rate,1,frequency),rate,137);
            // Discard startup latency; quadrature projection is insensitive to phase.
            const size_t begin=4800,count=38400;
            REQUIRE(out.size()>=begin+count);
            double sine=0,cosine=0;
            for(size_t i=begin;i<begin+count;++i) {
                const double angle=2*std::numbers::pi*frequency*double(i)/48000;
                sine+=out[i]*std::sin(angle); cosine+=out[i]*std::cos(angle);
            }
            const double amplitude=2*std::hypot(sine,cosine)/count;
            REQUIRE(amplitude>0.35); REQUIRE(amplitude<0.51);
        }
    }
}

TEST_CASE("SSTV resampler exports an independent recording for image qualification","[sstv-rate-recording]") {
    const auto input=qEnvironmentVariable("SDR_TOWN_SSTV_RATE_INPUT");
    if(input.isEmpty()) SKIP("Fixture supplied by scripts/test_sstv_rate.py");
    const auto output=qEnvironmentVariable("SDR_TOWN_SSTV_RATE_OUTPUT");
    REQUIRE_FALSE(output.isEmpty());
    ma_decoder decoder{};
    const auto config=ma_decoder_config_init(ma_format_f32,0,0);
    REQUIRE(ma_decoder_init_file_w(input.toStdWString().c_str(),&config,&decoder)==MA_SUCCESS);
    struct Guard {ma_decoder* decoder; ~Guard(){ma_decoder_uninit(decoder);}} guard{&decoder};
    REQUIRE(decoder.outputChannels==1);
    SstvRateConverter converter; converter.start(decoder.outputSampleRate);
    QFile file(output); REQUIRE(file.open(QIODevice::WriteOnly|QIODevice::NewOnly));
    std::vector<float> chunk(137);
    uint64_t total=0;
    for(;;) {
        ma_uint64 read=0;
        const auto result=ma_decoder_read_pcm_frames(&decoder,chunk.data(),chunk.size(),&read);
        REQUIRE((result==MA_SUCCESS || result==MA_AT_END));
        if(!read) break;
        total+=read; REQUIRE(total<=uint64_t{decoder.outputSampleRate}*360);
        const auto converted=converter.process(std::span(chunk.data(),size_t(read)));
        std::vector<qint16> pcm; pcm.reserve(converted.size());
        for(float sample:converted)
            pcm.push_back(qToLittleEndian(qint16(std::lround(std::clamp(double(sample),-1.,32767./32768.)*32768))));
        const auto bytes=qint64(pcm.size()*sizeof(qint16));
        REQUIRE(file.write(reinterpret_cast<const char*>(pcm.data()),bytes)==bytes);
    }
    REQUIRE(total>0);
}
