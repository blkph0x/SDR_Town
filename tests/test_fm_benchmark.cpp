#define _USE_MATH_DEFINES
#include "Demod.h"
#include "FmDiagnostics.h"
#include "NfmInputDecimator.h"
#include "WfmRetainedFirPrototype.h"
#include "RdsMpxDecoder.h"
#include "miniaudio.h"
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
Result demod(const std::vector<std::complex<float>>& iq,double rate,bool wide,double bandwidth=0) {
    Demodulator d;Result result;std::vector<float> all;
    const auto before=fmDiagnostics::snapshot(wide);
    for(size_t at=0;at<iq.size();) {
        const size_t n=std::min(size_t(8192),iq.size()-at);
        std::vector<std::complex<float>> block(iq.begin()+at,iq.begin()+at+n);
        double level=-100;
        const auto start=std::chrono::steady_clock::now();
        auto audio=d.demodulateToAudio(block,rate,100e6,100e6,
            wide?DemodMode::WFM:DemodMode::NFM,level,wide?15000:3000,
            -200,1,75,.96,bandwidth>0?bandwidth:(wide?180000:12500),0,48000,-30);
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

TEST_CASE("WFM bandwidth and high-deviation image characterization", "[.wfm-image-sweep]") {
    const double rate=GENERATE(2048000.0,2400000.0,10000000.0);
    const double bandwidth=GENERATE(150000.0,180000.0,220000.0);
    const double deviation=GENERATE(50000.0,75000.0);
    const int side=GENERATE(-1,1);
    const double fold=GENERATE(-30000.0,30000.0);
    const double target=std::max(192000.0,bandwidth*1.1);
    const double actualRate=rate/std::llround(rate/target);
    const double offset=side*actualRate+fold;
    std::vector<std::complex<float>> clean(size_t(rate*.2)),mixed(clean.size());
    double p=0,q=0;
    for(size_t i=0;i<clean.size();++i) {
        p=std::remainder(p+2*M_PI*deviation*std::sin(2*M_PI*900*i/rate)/rate,2*M_PI);
        q=std::remainder(q+2*M_PI*(offset+deviation*std::sin(2*M_PI*1700*i/rate))/rate,2*M_PI);
        clean[i]=std::polar(float(.5/101),float(p));
        mixed[i]=clean[i]+std::polar(float(50./101),float(q));
    }
    const auto reference=demod(clean,rate,true,bandwidth),test=demod(mixed,rate,true,bandwidth);
    std::vector<float> difference(test.tail.size());
    for(size_t i=0;i<difference.size();++i)difference[i]=test.tail[i]-reference.tail[i];
    const double wanted=toneAmplitude(reference.tail,900);
    REQUIRE(wanted>1e-6);
    REQUIRE(test.counts[fmDiagnostics::LookaheadReads]==0);
    REQUIRE(test.counts[fmDiagnostics::PhaseRepairs]==0);
    const double error=db(rms(difference)/rms(reference.tail));
    REQUIRE(std::isfinite(error));
    nlohmann::json row{{"sampleRate",rate},{"bandwidthHz",bandwidth},
        {"deviationHz",deviation},{"side",side},{"foldHz",fold},
        {"actualIqRate",actualRate},{"offsetHz",offset},{"blockerExcessDb",40},
        {"wantedGainDb",db(toneAmplitude(test.tail,900)/wanted)},
        {"differenceRelativeDb",error},{"realtimeRatio",test.elapsedUs/200000},
        {"audioSamples",test.counts[fmDiagnostics::AudioSamples]}};
    std::cout << "WFM_SWEEP " << row.dump() << '\n';
}

TEST_CASE("Retained WFM prototype equals full convolution across partitions", "[wfm][retained]") {
    const size_t factor=GENERATE(1u,7u,12u,51u);
    const size_t length=GENERATE(1u,65u,2049u);
    std::mt19937 random(152); std::uniform_real_distribution<float> value(-1,1);
    std::vector<float> taps(length);for(auto& t:taps)t=value(random);
    std::vector<std::complex<float>> input(7001);for(auto& s:input)s={value(random),value(random)};
    WfmSpeechFir full;auto all=input;full.process(all,taps);
    std::vector<std::complex<float>> expected;
    for(size_t i=0;i<all.size();i+=factor)expected.push_back(all[i]);
    WfmRetainedFirPrototype prototype(taps,factor);
    REQUIRE(prototype.process(input)==expected);
    prototype.reset();std::vector<std::complex<float>> split;
    size_t step=0;
    for(size_t at=0;at<input.size();) {
        const size_t sizes[]{1,0,2,7,819,31};const auto n=std::min(sizes[step++%6],input.size()-at);
        const auto out=prototype.process(std::span(input).subspan(at,n));
        split.insert(split.end(),out.begin(),out.end());at+=n;
    }
    REQUIRE(split==expected);
}

namespace {
std::vector<float> prototypeWfmTaps(double rate) {
    const size_t half=size_t(std::ceil(rate*.0001024)); // DEC-0152: qualified candidate delay.
    const double beta=.1102*(80-8.7);
    std::vector<float> taps(half*2+1);double sum=0;
    for(size_t i=0;i<taps.size();++i) {
        const double m=double(i)-half,x=m/half,fc=90000/rate;
        const double window=std::cyl_bessel_i(0,beta*std::sqrt(std::max(0.,1-x*x)))/std::cyl_bessel_i(0,beta);
        taps[i]=float(window*(m==0?2*fc:std::sin(2*M_PI*fc*m)/(M_PI*m)));sum+=taps[i];
    }
    for(auto& t:taps)t=float(t/sum);
    return taps;
}
}

TEST_CASE("Retained WFM composite preserves PCM and multiplex partition timing", "[wfm][retained]") {
    const double rate=GENERATE(2400000.0,10000000.0);
    const auto taps=prototypeWfmTaps(rate);
    const size_t factor=size_t(std::llround(rate/198000));
    std::vector<std::complex<float>> input(size_t(rate*.04));double phase=0;
    for(size_t i=0;i<input.size();++i) {
        const double t=i/rate;
        // Composite components exercise pilot/subcarrier paths, not encoded RDS messages.
        const double mpx=.5*std::sin(2*M_PI*900*t)+.1*std::sin(2*M_PI*19000*t)
            +.2*std::sin(2*M_PI*1200*t)*std::cos(2*M_PI*38000*t)
            +.03*std::cos(2*M_PI*57000*t);
        phase=std::remainder(phase+2*M_PI*75000*mpx/rate,2*M_PI);
        input[i]=std::polar(.1f,float(phase));
    }
    auto run=[&](bool split) {
        WfmRetainedFirPrototype filter(taps,factor);Demodulator d;
        std::pair<std::vector<float>,std::vector<float>> result;size_t step=0;
        for(size_t at=0;at<input.size();) {
            const size_t sizes[]{1,2,7,8192,113};
            const size_t n=std::min(split?sizes[step++%5]:input.size(),input.size()-at);
            auto iq=filter.process(std::span(input).subspan(at,n));at+=n;
            if(iq.empty())continue;
            FmMultiplexBlock mpx;double level=-100;
            const auto pcm=d.demodulateToAudio(iq,rate/factor,100e6,100e6,
                DemodMode::WFM,level,15000,-200,1,75,.96,180000,0,48000,-30,true,&mpx);
            REQUIRE(mpx.firstSample==result.second.size());
            REQUIRE(mpx.sampleRate>=128000);REQUIRE(mpx.sampleRate<=384000);
            result.first.insert(result.first.end(),pcm.begin(),pcm.end());
            result.second.insert(result.second.end(),mpx.samples.begin(),mpx.samples.end());
        }
        return result;
    };
    const auto whole=run(false),split=run(true);
    REQUIRE(whole.first.size()==split.first.size());
    REQUIRE(whole.second==split.second);
    double maximum=0;for(size_t i=0;i<whole.first.size();++i)
        maximum=std::max(maximum,double(std::abs(whole.first[i]-split.first[i])));
    REQUIRE(maximum<1e-5);
}

TEST_CASE("Retained WFM sharper FIR actual PCM and cost", "[.wfm-retained-benchmark]") {
    const double rate=GENERATE(2400000.0,10000000.0);
    const double deviation=GENERATE(50000.0,75000.0);
    const int side=GENERATE(-1,1);
    const size_t factor=size_t(std::llround(rate/198000));
    const double reducedRate=rate/factor,offset=side*(reducedRate-30000);
    const size_t half=size_t(std::ceil(rate*.0001024));
    const auto taps=prototypeWfmTaps(rate);
    std::vector<std::complex<float>> clean(size_t(rate*.2)),mixed(clean.size());double p=0,q=0;
    for(size_t i=0;i<clean.size();++i) {
        p=std::remainder(p+2*M_PI*deviation*std::sin(2*M_PI*900*i/rate)/rate,2*M_PI);
        q=std::remainder(q+2*M_PI*(offset+deviation*std::sin(2*M_PI*1700*i/rate))/rate,2*M_PI);
        clean[i]=std::polar(float(.5/101),float(p));mixed[i]=clean[i]+std::polar(float(50./101),float(q));
    }
    auto run=[&](const auto& input) {
        WfmRetainedFirPrototype filter(taps,factor);std::vector<std::complex<float>> output;
        const auto start=std::chrono::steady_clock::now();
        for(size_t at=0;at<input.size();at+=8192) {
            auto block=filter.process(std::span(input).subspan(at,std::min(size_t(8192),input.size()-at)));
            output.insert(output.end(),block.begin(),block.end());
        }
        const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
        auto result=demod(output,reducedRate,true,180000);result.elapsedUs+=us;return result;
    };
    const auto a=run(clean),b=run(mixed);std::vector<float> error(a.tail.size());
    for(size_t i=0;i<error.size();++i)error[i]=b.tail[i]-a.tail[i];
    const double difference=db(rms(error)/rms(a.tail));
    REQUIRE(std::isfinite(difference));
    if(deviation==50000) REQUIRE(difference < -40);
    std::cout<<"WFM_RETAINED "<<nlohmann::json{{"rate",rate},{"deviation",deviation},{"side",side},
        {"taps",taps.size()},{"differenceDb",difference},{"gainDb",db(toneAmplitude(b.tail,900)/toneAmplitude(a.tail,900))},
        {"realtimeRatio",b.elapsedUs/200000},{"delayUs",half/rate*1e6}}.dump()<<'\n';
}

TEST_CASE("Retained WFM stationary power matches full filtered power", "[wfm][retained][power]") {
    const double rate=GENERATE(2400000.0,10000000.0);
    const size_t factor=size_t(std::llround(rate/198000));
    auto taps=prototypeWfmTaps(rate);
    std::mt19937 random(153);std::normal_distribution<float> noise(0,.01f);
    std::vector<std::complex<float>> iq(size_t(rate*.02));
    for(size_t i=0;i<iq.size();++i)iq[i]=std::polar(.1f,float(2*M_PI*1000*i/rate))
        +std::complex<float>(noise(random),noise(random));
    WfmRetainedFirPrototype retained(taps,factor);
    const auto sparse=retained.process(iq);
    WfmSpeechFir full;full.process(iq,taps);
    double a=0,b=0;size_t na=0,nb=0;
    for(size_t i=taps.size()*2;i<iq.size();++i){a+=std::norm(iq[i]);++na;}
    for(size_t i=0;i<sparse.size();++i)if(i*factor>=taps.size()*2){b+=std::norm(sparse[i]);++nb;}
    const double difference=10*std::log10((b/nb)/(a/na));
    INFO("stationary full/retained level delta dB="<<difference);
    REQUIRE(std::abs(difference)<.1);
}

#ifdef SDR_TOWN_TEST_RDS_DSP
TEST_CASE("Retained WFM direct multiplex delivers CRC valid recorded RDS via RF", "[wfm][retained][rds-rf]") {
    const double rate=GENERATE(2400000.0,10000000.0);
    ma_decoder file{};auto config=ma_decoder_config_init(ma_format_f32,1,0);
    REQUIRE(ma_decoder_init_file(SDR_TOWN_RDS_FIXTURE,&config,&file)==MA_SUCCESS);
    struct Guard {ma_decoder* p;~Guard(){ma_decoder_uninit(p);}} guard{&file};
    std::vector<float> recording(file.outputSampleRate*2);ma_uint64 frames=0;
    const auto readResult=ma_decoder_read_pcm_frames(&file,recording.data(),recording.size(),&frames);
    REQUIRE((readResult==MA_SUCCESS || readResult==MA_AT_END));
    REQUIRE(frames>0);recording.resize(size_t(frames));
    const size_t factor=size_t(std::llround(rate/198000));
    WfmRetainedFirPrototype filter(prototypeWfmTaps(rate),factor);
    Demodulator demod,baselineDemod;RdsMpxDecoder rds,baselineRds,firstStageRds;double phase=0;
    std::complex<float> previous{1,0};uint64_t firstStageCount=0;
    const size_t signalCount=size_t((recording.size()-1)*rate/file.outputSampleRate);
    // Drain both causal FIR supports; capture end must not discard delayed RDS bits.
    const size_t count=signalCount+prototypeWfmTaps(rate).size()-1+320*factor;
    for(size_t at=0;at<count;at+=8192) {
        std::vector<std::complex<float>> iq(std::min(size_t(8192),count-at));
        for(size_t i=0;i<iq.size();++i) {
            const double position=(at+i)*double(file.outputSampleRate)/rate;
            const size_t index=size_t(position);const double f=position-index;
            const double sample=index+1<recording.size()?recording[index]*(1-f)+recording[index+1]*f:0;
            phase=std::remainder(phase+2*M_PI*75000*sample/rate,2*M_PI);
            iq[i]=std::polar(.1f,float(phase));
        }
        double baselineLevel=-100;FmMultiplexBlock baselineMpx;
        baselineDemod.demodulateToAudio(iq,rate,100e6,100e6,DemodMode::WFM,
            baselineLevel,15000,-200,1,75,.96,180000,0,48000,-30,true,&baselineMpx);
        REQUIRE(baselineRds.process(baselineMpx.samples,baselineMpx.sampleRate,baselineMpx.targetHz,
            baselineMpx.epoch,baselineMpx.firstSample,baselineMpx.discontinuity));
        auto filtered=filter.process(iq);if(filtered.empty())continue;
        std::vector<float> directMpx;directMpx.reserve(filtered.size());
        for(const auto sample:filtered) {
            directMpx.push_back(std::arg(sample*std::conj(previous))*float((rate/factor)/(2*M_PI*75000)));
            previous=sample;
        }
        REQUIRE(firstStageRds.process(directMpx,rate/factor,100e6,1,firstStageCount,firstStageCount==0));
        firstStageCount+=directMpx.size();
        double level=-100;FmMultiplexBlock mpx;
        demod.demodulateToAudio(filtered,rate/factor,100e6,100e6,DemodMode::WFM,
            level,15000,-200,1,75,.96,180000,0,48000,-30,true,&mpx);
        REQUIRE(rds.process(mpx.samples,mpx.sampleRate,mpx.targetHz,mpx.epoch,mpx.firstSample,mpx.discontinuity));
    }
    // DEC-0153: do not route data through the speech cascade's second RF FIR.
    const auto result=firstStageRds.snapshot();
    INFO("rate="<<rate<<" direct groups="<<result.groups<<" PI="<<result.lastGroupWords[0]
        <<" current-path groups="<<baselineRds.snapshot().groups<<" double-filter groups="<<rds.snapshot().groups);
    REQUIRE(baselineRds.snapshot().groups>=2);
    REQUIRE(result.groups>=2);
    REQUIRE(result.groups>=baselineRds.snapshot().groups);
    REQUIRE(result.lastGroupWords[0]==0x6201);
    REQUIRE(((result.lastGroupWords[1]>>5)&31)==14);
}
#endif
