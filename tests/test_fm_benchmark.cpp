#define _USE_MATH_DEFINES
#include "Demod.h"
#include "FmDiagnostics.h"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
// DEC-0146: integer-period projection; this is tone amplitude, not SINAD.
double toneAmplitude(const std::vector<float>& samples, double hz) {
    double re=0,im=0;
    for(size_t i=0;i<samples.size();++i) {
        const double phase=2*M_PI*hz*i/48000;
        re+=samples[i]*std::cos(phase);im+=samples[i]*std::sin(phase);
    }
    return 2*std::hypot(re,im)/samples.size();
}
double rms(const std::vector<float>& samples) {
    double sum=0;for(float s:samples)sum+=double(s)*s;
    return std::sqrt(sum/samples.size());
}
double db(double amplitude) {return 20*std::log10(std::max(amplitude,1e-15));}
struct Result {
    std::vector<float> tail;
    double elapsedUs=0;
    fmDiagnostics::Snapshot counts{};
};
Result demod(const std::vector<std::complex<float>>& iq,double rate,bool wide) {
    Demodulator d;Result result;std::vector<float> all;
    const auto before=fmDiagnostics::snapshot(wide);
    for(size_t at=0;at<iq.size();) {
        const size_t n=std::min(size_t(8192),iq.size()-at);
        std::vector<std::complex<float>> block(iq.begin()+at,iq.begin()+at+n);
        double level=-100;
        const auto start=std::chrono::steady_clock::now();
        auto audio=d.demodulateToAudio(block,rate,100e6,100e6,
            wide?DemodMode::WFM:DemodMode::NFM,level,wide?15000:3000,
            -200,1,75,.96,wide?180000:12500,0,48000,-30);
        result.elapsedUs+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
        all.insert(all.end(),audio.begin(),audio.end());at+=n;
    }
    const auto after=fmDiagnostics::snapshot(wide);
    for(size_t i=0;i<fmDiagnostics::MaxBlockUs;++i)result.counts[i]=after[i]-before[i];
    REQUIRE(all.size()>=4800);
    result.tail.assign(all.end()-4800,all.end());
    return result;
}
}

TEST_CASE("FM benchmark measures independent tones and RMS", "[fm][measurement]") {
    std::vector<float> tone(4800);
    for(size_t i=0;i<tone.size();++i)
        tone[i]=float(.3*std::sin(2*M_PI*900*i/48000)+.1*std::cos(2*M_PI*1700*i/48000));
    REQUIRE(std::abs(toneAmplitude(tone,900)-.3)<1e-7);
    REQUIRE(std::abs(toneAmplitude(tone,1700)-.1)<1e-7);
    REQUIRE(toneAmplitude(tone,2300)<1e-7);
    REQUIRE(std::abs(rms(tone)-std::sqrt(.05))<1e-7);
    REQUIRE(std::abs(db(.1)+20)<1e-12);
}

TEST_CASE("FM two-signal interference and cost report", "[.fm-benchmark]") {
    const bool wide=GENERATE(false,true);
    const double rate=GENERATE(2048000.0,2400000.0,10000000.0);
    const bool image=GENERATE(false,true);
    const double excessDb=GENERATE(0.0,20.0,40.0);
    const int factor=int(std::llround(rate/(wide?198000.0:192000.0)));
    const double offset=image?rate/factor+1500:(wide?400000:25000);
    const double ratio=std::pow(10.0,excessDb/20), amplitude=.5/(1+ratio);
    const double deviation=wide?50000:1800;
    std::vector<std::complex<float>> clean(size_t(rate*.2)),mixed(clean.size());
    double wantedPhase=0,blockerPhase=0;
    for(size_t i=0;i<clean.size();++i) {
        wantedPhase=std::remainder(wantedPhase+2*M_PI*deviation*std::sin(2*M_PI*900*i/rate)/rate,2*M_PI);
        blockerPhase=std::remainder(blockerPhase+2*M_PI*(offset+deviation*std::sin(2*M_PI*1700*i/rate))/rate,2*M_PI);
        clean[i]=std::polar(float(amplitude),float(wantedPhase));
        mixed[i]=clean[i]+std::polar(float(amplitude*ratio),float(blockerPhase));
    }
    const auto reference=demod(clean,rate,wide), test=demod(mixed,rate,wide);
    std::vector<float> difference(test.tail.size());
    for(size_t i=0;i<difference.size();++i)difference[i]=test.tail[i]-reference.tail[i];
    const double wanted=toneAmplitude(reference.tail,900);
    REQUIRE(wanted>1e-6);
    REQUIRE(test.counts[fmDiagnostics::LookaheadReads]==0);
    REQUIRE(test.counts[fmDiagnostics::PhaseRepairs]==0);
    nlohmann::json row{
        {"mode",wide?"WFM":"NFM"},{"sampleRate",rate},{"offsetHz",offset},
        {"placement",image?"first_image":"adjacent"},{"blockerExcessDb",excessDb},
        {"wantedGainDb",db(toneAmplitude(test.tail,900)/wanted)},
        {"blockerAudioRelativeDb",db(toneAmplitude(test.tail,1700)/wanted)},
        {"differenceRelativeDb",db(rms(difference)/rms(reference.tail))},
        {"processingUs",test.elapsedUs},{"realtimeRatio",test.elapsedUs/200000},
        {"inputSamples",test.counts[fmDiagnostics::InputSamples]},
        {"audioSamples",test.counts[fmDiagnostics::AudioSamples]},
        {"overBudgetBlocks",test.counts[fmDiagnostics::OverBudgetBlocks]},
        {"channelizerUs",test.counts[fmDiagnostics::ChannelizerUs]},
        {"discriminatorUs",test.counts[fmDiagnostics::DiscriminatorUs]},
        {"resamplerUs",test.counts[fmDiagnostics::ResamplerUs]},
        {"postAudioUs",test.counts[fmDiagnostics::PostAudioUs]}};
    std::cout << "FM_BENCH " << row.dump() << '\n';
}
