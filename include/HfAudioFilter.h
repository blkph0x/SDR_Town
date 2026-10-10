#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// Speech-band playback filter. Private analog audio state; never processes
// decoder IQ or P25 AMBE/IMBE PCM (DEC-0209).
class HfAudioFilter {
    struct Biquad {
        double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
        double run(double x){double y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;return y;}
        void configure(double rate,double cutoff,bool high){
            const double w=2*3.141592653589793*cutoff/rate,c=std::cos(w),a=std::sin(w)/std::sqrt(2.0),d=1+a;
            b0=(high ? 1+c : 1-c)/2/d;b1=(high ? -(1+c) : 1-c)/d;b2=b0;a1=-2*c/d;a2=(1-a)/d;z1=z2=0;
        }
    } hp,lp1,lp2;
    double sampleRate=0,blend=0;
public:
    void process(std::vector<float>& audio,double rate,bool enabled){
        if(!std::isfinite(rate)||rate<8000)return;
        if(rate!=sampleRate){sampleRate=rate;hp.configure(rate,200,true);lp1.configure(rate,2800,false);lp2.configure(rate,2800,false);blend=0;}
        if(!enabled && blend==0)return;
        const double step=1/(rate*.01);
        for(float& sample:audio){const double x=std::isfinite(sample)?sample:0;const double filtered=lp2.run(lp1.run(hp.run(x)));blend=std::clamp(blend+(enabled?step:-step),0.0,1.0);sample=static_cast<float>(x+(filtered-x)*blend);}
    }
};
