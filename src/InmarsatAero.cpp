#include "InmarsatAero.h"
#include "InmarsatAdsc.h"
#include "AeroCodec.h"
#include "aerol.h"
#include "oqpskdemodulator.h"
#include "mskdemodulator.h"
#include "burstmskdemodulator.h"
#include "burstoqpskdemodulator.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <stdexcept>

struct InmarsatAero::Impl {
    AeroL frames;
    std::unique_ptr<OqpskDemodulator> oqpsk;
    std::unique_ptr<MskDemodulator> msk;
    std::unique_ptr<BurstMskDemodulator> burstMsk;
    std::unique_ptr<BurstOqpskDemodulator> burstOqpsk;
    std::unique_ptr<SdrAeroCodec, decltype(&sdr_aero_destroy)> codec{sdr_aero_create(), sdr_aero_destroy};
    std::array<int16_t, 960> pending{};
    size_t pendingCount=0;
    int rate;
    InmarsatAeroStats stats;
    InmarsatConstellation scatter;
    uint64_t scatterSample = 0;
    MessageSink messageSink;
    PcmSink pcmSink;
    explicit Impl(int bps,bool burst):rate(bps) {
        if (!codec) throw std::bad_alloc();
        if (bps!=600 && bps!=1200 && bps!=8400 && bps!=10500)
            throw std::invalid_argument("Unsupported Classic Aero bit rate");
        if(burst && bps!=1200 && bps!=10500) throw std::invalid_argument("Unsupported Aero burst bit rate");
        frames.setSettings(bps, burst);
        QObject::connect(&frames, &AeroL::CrcResult, &frames, [this](bool ok) {
            if(ok) ++stats.crcOk; else ++stats.crcBad;
        });
        QObject::connect(&frames, &AeroL::DataCarrierDetect, &frames, [this](bool locked) {
            stats.locked=locked;
            if(oqpsk) oqpsk->DCDstatSlot(locked);
            if(msk) msk->DCDstatSlot(locked);
        });
        QObject::connect(&frames, &AeroL::ValidatedSignalUnit, &frames, [this](const QByteArray& su) {
            if(su.size()!=12) return;
            const auto u=[&](int i){return uint32_t(uint8_t(su[i]));};
            const auto type=u(0), aes=(u(1)<<16)|(u(2)<<8)|u(3);
            if(rate==8400 && (type==0x30 || type==0x60) && aes) {
                if(stats.aes!=aes) sdr_aero_reset(codec.get());
                stats.aes=aes;
            }
            if(type>=0x31 && type<=0x34 && rate!=8400 && aes) {
                InmarsatMessage m;
                m.kind=InmarsatMsgKind::CAssign; m.aesId=aes; m.gesId=u(4);
                m.voiceRxHz=1510e6+double(((u(6)&0x7f)<<8)|u(7))*2500;
                m.voiceTxHz=1611.5e6+double(((u(8)&0x7f)<<8)|u(9))*2500;
                m.validated=true; m.label="C-ASSIGN";
                m.text="CRC-valid Aero voice assignment";
                if(messageSink) messageSink(m);
            }
        });
        QObject::connect(&frames, &AeroL::ACARSsignal, &frames, [this](ACARSItem& item) {
            if(!item.valid || item.nonacars) return;
            InmarsatMessage m;
            m.kind=InmarsatMsgKind::Acars; m.validated=true;
            m.aesId=item.isuitem.AESID; m.gesId=item.isuitem.GESID;
            m.label=item.LABEL.toStdString(); m.text=item.message.toStdString();
            m.registration=item.PLANEREG.toStdString();
            const auto position=InmarsatAdsc::parse(item.message.toStdString());
            if(position && (!position->airframeId || position->airframeId==m.aesId)) {
                m.hasPosition=true; m.latDeg=position->latitude; m.lonDeg=position->longitude;
                m.altitudeFt=position->altitudeFt; m.positionSecondsPastHour=position->secondsPastHour;
                m.registration=position->registration; m.callsign=position->callsign;
                ++stats.positions;
            }
            if(messageSink) messageSink(m);
        });
        QObject::connect(&frames, &AeroL::CFrame, &frames, [this](const QByteArray& words, int validUnits) {
            ++stats.cFrames;
            // A CRC-valid subchannel in this frame proves framing. Carrier/DCD
            // alone cannot release voice after a lost UW or source discontinuity.
            if(validUnits==0 || words.size()!=300) {
                ++stats.rejectedCFrames;
                sdr_aero_reset(codec.get());
                return;
            }
            std::array<int16_t,4000> pcm{};
            bool hasSpeech=false;
            for(int i=0;i<25;++i) {
                char flags[64]{};
                const int errors=sdr_aero_decode(codec.get(),
                    reinterpret_cast<const uint8_t*>(words.constData())+i*12, pcm.data()+i*160, flags);
                if(errors<0) throw std::runtime_error("Aero codec rejected a framed word");
                stats.codecErrors+=errors;
                if(std::strchr(flags,'R')) ++stats.codecRepeats;
                if(std::strchr(flags,'M') || std::strchr(flags,'E') || std::strchr(flags,'T')) ++stats.codecMutes;
                else hasSpeech=true;
                ++stats.voiceWords;
            }
            stats.pcmSamples+=pcm.size();
            if(hasSpeech) {stats.lastVoiceSample=stats.input48k;++stats.speechFrames;}
            if(pcmSink) pcmSink(pcm,stats.aes);
        });
        auto soft=[this](const QVector<short>& bits) {
            stats.softBits+=bits.size(); frames.processDemodulatedSoftBits(bits);
        };
        // DEC-0127: passive modem feedback, after symbol recovery. Direct local
        // callbacks stay on this modem's owner; the UI sees only a bounded copy.
        auto points=[this](const QVector<cpx_type>& input) {
            scatter.points.clear();
            for(const auto& p:input) {
                // Upstream scatter buffers start zero-filled; omit that padding.
                if(std::isfinite(p.real()) && std::isfinite(p.imag()) && p!=cpx_type{})
                    scatter.points.emplace_back(float(p.real()),float(p.imag()));
                if(scatter.points.size()==300) break;
            }
            ++scatter.sequence;scatterSample=stats.input48k;
        };
        if(burst && bps==10500) {
            burstOqpsk=std::make_unique<BurstOqpskDemodulator>(nullptr);
            BurstOqpskDemodulator::Settings settings;
            settings.fb=bps;settings.Fs=48000;settings.freq_center=8000;settings.lockingbw=10500;
            burstOqpsk->setSettings(settings);burstOqpsk->setSQL(false);burstOqpsk->setAFC(true);burstOqpsk->setCPUReduce(false);
            burstOqpsk->setScatterPointType(BurstOqpskDemodulator::SPT_constellation);
            QObject::connect(burstOqpsk.get(),&BurstOqpskDemodulator::ScatterPoints,&frames,points);
            QObject::connect(burstOqpsk.get(),&BurstOqpskDemodulator::processDemodulatedSoftBits,&frames,soft);
        } else if(burst) {
            burstMsk=std::make_unique<BurstMskDemodulator>(nullptr);
            BurstMskDemodulator::Settings settings;
            settings.fb=bps;settings.Fs=48000;settings.freq_center=8000;settings.lockingbw=1800;
            burstMsk->setSettings(settings);burstMsk->setSQL(false);burstMsk->setAFC(true);burstMsk->setCPUReduce(false);
            burstMsk->setScatterPointType(BurstMskDemodulator::SPT_constellation);
            QObject::connect(burstMsk.get(),&BurstMskDemodulator::ScatterPoints,&frames,points);
            QObject::connect(burstMsk.get(),&BurstMskDemodulator::processDemodulatedSoftBits,&frames,soft);
        } else if(bps>=8400) {
            oqpsk=std::make_unique<OqpskDemodulator>(nullptr);
            OqpskDemodulator::Settings settings;
            settings.fb=bps; settings.Fs=48000; settings.freq_center=8000;
            settings.lockingbw=bps; settings.signalthreshold=0.65;
            oqpsk->setSettings(settings); oqpsk->setSQL(false); oqpsk->setAFC(true);
            oqpsk->setCPUReduce(false); oqpsk->setScatterPointType(OqpskDemodulator::SPT_constellation);
            QObject::connect(oqpsk.get(),&OqpskDemodulator::ScatterPoints,&frames,points);
            QObject::connect(oqpsk.get(),&OqpskDemodulator::processDemodulatedSoftBits,&frames,soft);
            QObject::connect(oqpsk.get(),&OqpskDemodulator::MSESignal,&frames,[this](double v){stats.mse=v;});
            QObject::connect(oqpsk.get(),&OqpskDemodulator::EbNoMeasurmentSignal,&frames,[this](double v){stats.ebno=v;});
        } else {
            msk=std::make_unique<MskDemodulator>(nullptr);
            MskDemodulator::Settings settings;
            settings.fb=bps; settings.Fs=48000; settings.freq_center=8000; settings.lockingbw=bps*1.5;
            msk->setSettings(settings); msk->setSQL(false); msk->setAFC(true); msk->setCPUReduce(false);
            msk->setScatterPointType(MskDemodulator::SPT_constellation);
            QObject::connect(msk.get(),&MskDemodulator::ScatterPoints,&frames,points);
            QObject::connect(msk.get(),&MskDemodulator::processDemodulatedSoftBits,&frames,soft);
        }
    }
    void consume() {
        const auto* data=reinterpret_cast<const char*>(pending.data());
        if(burstMsk) burstMsk->writeData(data,pending.size()*sizeof(int16_t));
        else if(burstOqpsk) burstOqpsk->writeData(data,pending.size()*sizeof(int16_t));
        else if(oqpsk) oqpsk->writeData(data,pending.size()*sizeof(int16_t));
        else msk->writeData(data,pending.size()*sizeof(int16_t));
        stats.input48k+=pending.size();
        if(stats.input48k%48000==0) frames.advanceSecond();
        pendingCount=0;
    }
};
InmarsatAero::InmarsatAero(int bps,bool burst):impl_(std::make_unique<Impl>(bps,burst)) {}
InmarsatAero::~InmarsatAero()=default;
void InmarsatAero::setMessageSink(MessageSink sink) {impl_->messageSink=std::move(sink);}
void InmarsatAero::setPcmSink(PcmSink sink) {impl_->pcmSink=std::move(sink);}
InmarsatAeroStats InmarsatAero::stats() const {return impl_->stats;}
InmarsatConstellation InmarsatAero::constellation() const {
    auto result=impl_->scatter;
    result.ageSamples=impl_->stats.input48k-impl_->scatterSample;
    // DEC-0127: one second without fresh symbols is stale visualization, not
    // permission to modify decoder lock or audio. Burst pauses may be longer.
    if(result.ageSamples>48000) result.points.clear();
    return result;
}
void InmarsatAero::processIf(std::span<const int16_t> input) {
    for(auto sample:input) {
        impl_->pending[impl_->pendingCount++]=sample;
        if(impl_->pendingCount==impl_->pending.size()) impl_->consume();
    }
}

namespace {
constexpr double pi=std::numbers::pi;
constexpr int taps=65;
std::array<double,taps> lowpass(double cutoff, double fraction=0) {
    std::array<double,taps> h{};
    double sum=0;
    for(int i=0;i<taps;++i) {
        const double x=i-(taps-1)/2.0-fraction;
        const double w=0.42-0.5*std::cos(2*pi*i/(taps-1))+0.08*std::cos(4*pi*i/(taps-1));
        h[i]=w*(std::abs(x)<1e-12?2*cutoff:std::sin(2*pi*cutoff*x)/(pi*x)); sum+=h[i];
    }
    for(auto& v:h) v/=sum;
    return h;
}
struct HalfRate {
    std::array<std::complex<float>,taps> history{};
    std::array<double,taps> h=lowpass(0.20);
    size_t cursor=0; bool phase=false;
    bool push(std::complex<float> x,std::complex<float>& y) {
        history[cursor]=x; cursor=(cursor+1)%taps; phase=!phase;
        if(phase) return false;
        std::complex<double> sum=0;
        for(size_t i=0;i<taps;++i) sum+=std::complex<double>(history[(cursor+i)%taps])*h[i];
        y=std::complex<float>(sum); return true;
    }
};
}
struct InmarsatChannelizer::Impl {
    double inputRate, offset, phase=0, ifPhase=0, rate, next=32;
    std::vector<HalfRate> stages;
    std::vector<std::complex<float>> queue;
    std::array<std::array<double,taps>,1024> bank;
    uint64_t base=0;
    Impl(double r,double o):inputRate(r),offset(o),rate(r) {
        if(!std::isfinite(r) || r<16000 || r>40e6 || !std::isfinite(o))
            throw std::invalid_argument("Aero IQ needs a sample rate of at least 16 kHz");
        while(rate>96000) {stages.emplace_back();rate/=2;}
        // 6.5 kHz baseband cutoff fits the 8 kHz real IF without mirror overlap;
        // widest Aero occupied bandwidth is 10.5 kHz (roll-off 1, 5250 symbols/s).
        for(size_t i=0;i<bank.size();++i) bank[i]=lowpass(std::min(6500.0,rate*0.4)/rate,double(i)/bank.size());
    }
};
InmarsatChannelizer::InmarsatChannelizer(double rate,double offset):impl_(std::make_unique<Impl>(rate,offset)) {}
InmarsatChannelizer::~InmarsatChannelizer()=default;
std::vector<int16_t> InmarsatChannelizer::process(std::span<const std::complex<float>> input) {
    auto& s=*impl_;
    for(auto x:input) {
        x*=std::complex<float>(std::cos(s.phase),std::sin(s.phase));
        s.phase=std::remainder(s.phase-2*pi*s.offset/s.inputRate,2*pi);
        bool have=true;
        for(auto& stage:s.stages) {std::complex<float> y; if(!stage.push(x,y)){have=false;break;} x=y;}
        if(have) s.queue.push_back(x);
    }
    std::vector<int16_t> out;
    while(s.next+32<s.base+s.queue.size()) {
        const auto center=uint64_t(s.next);
        const auto phase=std::min<size_t>(1023,size_t((s.next-center)*1024));
        std::complex<double> x=0;
        for(size_t i=0;i<taps;++i) x+=std::complex<double>(s.queue[center-32+i-s.base])*s.bank[phase][i];
        const double real=(x*std::polar(1.0,s.ifPhase)).real();
        out.push_back(int16_t(std::lrint(std::clamp(real,-1.0,32767.0/32768)*32768)));
        s.ifPhase=std::remainder(s.ifPhase+2*pi*8000/48000,2*pi);
        s.next+=s.rate/48000;
    }
    const auto keep=uint64_t(s.next)-32;
    const auto remove=std::min<size_t>(s.queue.size(),keep-s.base);
    s.queue.erase(s.queue.begin(),s.queue.begin()+remove); s.base+=remove;
    return out;
}
