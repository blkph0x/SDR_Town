#define _USE_MATH_DEFINES
#include "Demod.h"
#include "FmDiagnostics.h"
#include "NfmInputDecimator.h"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>
#include <random>

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

TEST_CASE("NFM candidate first-stage response meets measured design targets", "[nfm][input-filter]") {
    const double rate=GENERATE(320000.0,384000.0,2048000.0,2400000.0,10000000.0);
    const size_t factor=size_t(std::llround(rate/192000));
    NfmInputDecimator fir;fir.configure(factor);
    auto response=[&](double frequency) {
        std::complex<double> h{};
        for(size_t i=0;i<fir.taps().size();++i)
            h+=double(fir.taps()[i])*std::polar(1.0,-2*std::numbers::pi*frequency*i/rate);
        return std::abs(h);
    };
    double ripple=0,stop=0;
    for(int i=0;i<=100;++i)ripple=std::max(ripple,std::abs(db(response(12500.*i/100))));
    const double start=.75*rate/factor;
    for(int i=0;i<=4096;++i)stop=std::max(stop,response(start+(rate/2-start)*i/4096));
    INFO("rate=" << rate << " ripple=" << ripple << " stop=" << db(stop));
    REQUIRE(ripple<.1);
    REQUIRE(db(stop)<-80);
    const double image=rate/factor+6250;
    const double box=std::abs(std::sin(std::numbers::pi*image*factor/rate)/(factor*std::sin(std::numbers::pi*image/rate)));
    INFO("NFM first-stage rate=" << rate << " passband ripple dB=" << ripple << " stop dB=" << db(stop)
         << " boxcar image dB=" << db(box) << " double-boxcar image dB=" << db(box*box));
}

TEST_CASE("NFM input FIR matches direct convolution and arbitrary partitions", "[nfm][input-filter]") {
    const size_t factor=GENERATE(2u,11u,13u,52u);
    NfmInputDecimator whole,split;whole.configure(factor);split.configure(factor);
    std::mt19937 random(148);std::uniform_real_distribution<float> value(-1,1);
    std::vector<std::complex<float>> input(4001);
    for(auto& s:input)s={value(random),value(random)};
    const auto result=whole.process(input);
    std::vector<std::complex<float>> parts,reference;
    for(size_t at=0;at<input.size();) {
        const size_t sizes[]{1,7,29,1000};const auto n=std::min(sizes[at%4],input.size()-at);
        const auto block=split.process(std::span(input).subspan(at,n));
        parts.insert(parts.end(),block.begin(),block.end());at+=n;
    }
    for(size_t n=factor-1;n<input.size();n+=factor) {
        std::complex<float> sum{};
        for(size_t k=0;k<whole.taps().size() && k<=n;++k)sum+=whole.taps()[k]*input[n-k];
        reference.push_back(sum);
    }
    REQUIRE(result==reference);REQUIRE(parts==reference);
    REQUIRE(result.size()==input.size()/factor);
    split.reset();REQUIRE(split.process(input)==reference);
    REQUIRE(split.delaySamples()==8*factor);
}

TEST_CASE("NFM rejects strong blockers across first and second decimation images", "[nfm][image-blocker]") {
    const double rate=GENERATE(2400000.0,10000000.0);
    const int image=GENERATE(1,2);
    const double fold=GENERATE(-6250.0,-1500.0,0.0,1500.0,6250.0);
    const double offset=image*rate/std::llround(rate/192000)+fold;
    std::vector<std::complex<float>> clean(size_t(rate*.2)),mixed(clean.size());
    double p=0,q=0;
    for(size_t i=0;i<clean.size();++i) {
        p=std::remainder(p+2*M_PI*1800*std::sin(2*M_PI*900*i/rate)/rate,2*M_PI);
        q=std::remainder(q+2*M_PI*(offset+1800*std::sin(2*M_PI*1700*i/rate))/rate,2*M_PI);
        clean[i]=std::polar(float(.5/101),float(p));
        mixed[i]=clean[i]+std::polar(float(50./101),float(q));
    }
    const auto a=demod(clean,rate,false),b=demod(mixed,rate,false);
    std::vector<float> difference(a.tail.size());
    for(size_t i=0;i<difference.size();++i)difference[i]=b.tail[i]-a.tail[i];
    const double error=db(rms(difference)/rms(a.tail));
    INFO("rate=" << rate << " image=" << image << " folded=" << fold << " error dB=" << error);
    REQUIRE(std::abs(db(toneAmplitude(b.tail,900)/toneAmplitude(a.tail,900)))<.1);
    REQUIRE(error<-40); // DEC-0148: 40dB margin for the tested +40dB blocker.
}

TEST_CASE("WFM FIR matches original ordered ring convolution exactly", "[wfm][fir]") {
    const size_t count=GENERATE(1u,2u,31u,321u);
    std::mt19937 random(147);
    std::uniform_real_distribution<float> value(-1,1);
    std::vector<float> taps(count);
    for(auto& t:taps)t=value(random);
    WfmSpeechFir fir;
    std::vector<std::complex<float>> history(count);
    size_t write=0;
    for(size_t chunk:{1u,2u,3u,4u,7u,8192u,17u}) {
        std::vector<std::complex<float>> actual(chunk),expected;
        for(auto& s:actual)s={value(random),value(random)};
        expected=actual;
        for(auto& s:expected) {
            history[write]=s;size_t at=write;std::complex<float> sum{};
            for(float t:taps) {sum+=t*history[at];at=at==0?count-1:at-1;}
            s=sum;if(++write==count)write=0;
        }
        fir.process(actual,taps);
        REQUIRE(actual==expected);
    }
    fir.reset();
    std::vector<std::complex<float>> impulse(count+8);impulse[0]={1,0};
    fir.process(impulse,taps);
    for(size_t i=0;i<impulse.size();++i)
        REQUIRE(impulse[i]==std::complex<float>(i<count?taps[i]:0,0));
    std::vector<std::complex<float>> next(5,{1,1});
    fir.process(next,std::vector<float>{2});
    REQUIRE(next==std::vector<std::complex<float>>(5,{2,2}));
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
