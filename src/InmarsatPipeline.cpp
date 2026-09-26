#include "InmarsatPipeline.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

struct InmarsatPipeline::Native {
    std::unique_ptr<InmarsatAero> aero;
    std::unique_ptr<InmarsatChannelizer> channelizer;
    InmarsatAero::MessageSink messageSink;
    InmarsatAero::PcmSink pcmSink;
    uint64_t validated=0, failed=0, voice=0, pcm=0, rejected=0, corrections=0, repeats=0, mutes=0, speech=0;
    std::vector<InmarsatMessage> positions;
    uint64_t messages=0;
    void reset(int bitRate,double rate,double offset,double channel,bool burst) {
        aero.reset(); channelizer.reset(); positions.clear();
        if(bitRate==0) return;
        channelizer=std::make_unique<InmarsatChannelizer>(rate,offset);
        aero=std::make_unique<InmarsatAero>(bitRate,burst);
        aero->setMessageSink([this,channel](const InmarsatMessage& incoming) {
            auto m=incoming; m.freqHz=channel;
            ++messages;
            if(m.hasPosition) {
                auto it=std::find_if(positions.begin(),positions.end(),[&](const auto& p){return p.aesId==m.aesId;});
                if(it!=positions.end()) *it=m;
                else {if(positions.size()==256) positions.erase(positions.begin()); positions.push_back(m);}
            }
            if(messageSink) messageSink(m);
        });
        aero->setPcmSink([this](std::span<const int16_t> samples,uint32_t aes) {
            if(pcmSink) pcmSink(samples,aes);
        });
    }
};
InmarsatPipeline::InmarsatPipeline():native_(std::make_unique<Native>()) {}
InmarsatPipeline::~InmarsatPipeline()=default;
InmarsatPipeline::InmarsatPipeline(InmarsatPipeline&&) noexcept=default;
InmarsatPipeline& InmarsatPipeline::operator=(InmarsatPipeline&&) noexcept=default;
void InmarsatPipeline::setMessageSink(InmarsatAero::MessageSink sink) {native_->messageSink=std::move(sink);}
void InmarsatPipeline::setPcmSink(InmarsatAero::PcmSink sink) {native_->pcmSink=std::move(sink);}
InmarsatConstellation InmarsatPipeline::constellation() const {
    return native_->aero?native_->aero->constellation():InmarsatConstellation{};
}
InmarsatDemodStats InmarsatPipeline::stats() const {
    auto result=demod_.stats();
    if(native_->aero) {
        const auto a=native_->aero->stats();
        result.locked=a.locked;result.carrierDetected=result.carrierDetected || a.locked;
        result.ebnoDb=a.ebno;
    }
    result.framesOut=native_->validated;
    return result;
}

void InmarsatPipeline::process(const std::complex<float>* iq, size_t count,
    uint64_t start, double rate, double center, double channel,
    InmarsatDemodMode mode, bool discontinuity) {
    if (!iq || count == 0) return;
    if (!std::isfinite(rate) || rate < 8000 || rate > 40e6 ||
        !std::isfinite(center) || !std::isfinite(channel) || center <= 0 || channel <= 0 ||
        std::abs(channel - center) >= rate / 2)
        throw std::runtime_error("Inmarsat channel is outside the recorded IQ passband or rates are invalid");
    const auto begin = std::chrono::steady_clock::now();
    // Validate the whole block before modifying state: NaN must not poison PLL history.
    double power = 0, peak = 0;
    for (size_t i = 0; i < count; ++i) {
        const double re = iq[i].real(), im = iq[i].imag();
        if (!std::isfinite(re) || !std::isfinite(im) || std::abs(re) > 1e6 || std::abs(im) > 1e6)
            throw std::runtime_error("Invalid or unbounded Inmarsat IQ amplitude");
        power += re * re + im * im;
        peak = std::max({peak, std::abs(re), std::abs(im)});
    }
    const bool gap = started_ && (discontinuity || start != nextSample_);
    if (!started_ || gap || rate_ != rate || center_ != center || channel_ != channel || mode_ != mode) {
        const bool burst=mode==InmarsatDemodMode::AeroBurstMsk1200 || mode==InmarsatDemodMode::AeroBurstOqpsk10500;
        const auto probe=mode==InmarsatDemodMode::AeroBurstMsk1200?InmarsatDemodMode::AeroMsk1200:
            mode==InmarsatDemodMode::AeroBurstOqpsk10500?InmarsatDemodMode::AeroOqpsk10500:mode;
        demod_.reset(probe, rate, channel - center);
        const int bps=mode==InmarsatDemodMode::AeroMsk600?600:
            probe==InmarsatDemodMode::AeroMsk1200?1200:mode==InmarsatDemodMode::AeroVoice8400?8400:
            probe==InmarsatDemodMode::AeroOqpsk10500?10500:0;
        if(bps && (rate<16000 || std::abs(channel-center)+6500>rate/2))
            throw std::runtime_error("Aero channel filter must fit entirely inside the IQ passband (minimum 16 kHz IQ)");
        native_->reset(bps,rate,channel-center,channel,burst);
        ++resets_;
        if (gap) ++gaps_;
    }
    started_ = true;
    rate_ = rate; center_ = center; channel_ = channel; mode_ = mode;
    const auto before = demod_.stats();
    demod_.process(iq, count);
    if(native_->aero) {
        const auto before=native_->aero->stats();
        const auto intermediate=native_->channelizer->process({iq,count});
        native_->aero->processIf(intermediate);
        const auto after=native_->aero->stats();
        native_->validated+=after.crcOk-before.crcOk;
        native_->failed+=after.crcBad-before.crcBad;
        native_->voice+=after.voiceWords-before.voiceWords;
        native_->speech+=after.speechFrames-before.speechFrames;
        native_->pcm+=after.pcmSamples-before.pcmSamples;
        native_->rejected+=after.rejectedCFrames-before.rejectedCFrames;
        native_->corrections+=after.codecErrors-before.codecErrors;
        native_->repeats+=after.codecRepeats-before.codecRepeats;
        native_->mutes+=after.codecMutes-before.codecMutes;
    }
    const auto after = demod_.stats();
    symbols_ += after.symbolsOut - before.symbolsOut;
    rawBlocks_ += after.rawBlocksOut - before.rawBlocksOut;
    nextSample_ = start + count;
    samples_ += count;
    ++blocks_;
    peak_ = std::max(peak_, peak);
    sumPower_ += power;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    totalMs_ += ms;
    maxMs_ = std::max(maxMs_, ms);
}

nlohmann::json InmarsatPipeline::report() const {
    const auto s = stats();
    const auto a=native_->aero?native_->aero->stats():InmarsatAeroStats{};
    nlohmann::json positions=nlohmann::json::array();
    for(const auto& p:native_->positions) positions.push_back({{"aesId",p.aesId},
        {"latDeg",p.latDeg},{"lonDeg",p.lonDeg},{"altitudeFt",p.altitudeFt},
        {"callsign",p.callsign},{"registration",p.registration},{"secondsPastHour",p.positionSecondsPastHour}});
    return {{"samples", samples_}, {"blocks", blocks_}, {"nextSample", nextSample_},
        {"resets", resets_}, {"discontinuities", gaps_}, {"rateHz", rate_},
        {"centerHz", center_}, {"channelHz", channel_}, {"offsetHz", channel_ - center_},
        {"mode", static_cast<int>(mode_)}, {"symbols", symbols_}, {"rawBlocks", rawBlocks_},
        {"carrierDetected", s.carrierDetected}, {"quality", s.quality}, {"carrierOffsetHz", s.freqOffsetHz},
        {"processingMs", totalMs_}, {"maxBlockMs", maxMs_}, {"peakComponent", peak_},
        {"rms", samples_ ? std::sqrt(sumPower_ / samples_) : 0},
        {"protocolLock", s.locked}, {"validatedFrames", native_->validated}, {"voiceFrames", native_->voice},
        {"speechFrames",native_->speech},{"messages",native_->messages},
        {"crcFailed",native_->failed},{"rejectedCFrames",native_->rejected},
        {"codecCorrections",native_->corrections},{"codecRepeats",native_->repeats},{"codecMutes",native_->mutes},
        {"positions",positions},{"pcmSamples", native_->pcm},
        {"softBits",a.softBits},{"voiceAesId",a.aes},
        {"voiceActive",a.lastVoiceSample>0 && a.input48k-a.lastVoiceSample<=24000},
        {"protocolDecoderAvailable",bool(native_->aero)}, {"aeroVocoderAvailable", true},
        {"capability", native_->aero?"classic_aero_experimental":"physical_probe_only"}};
}
