#include "InmarsatPipeline.h"
#include "InmarsatDiagnosticRecording.h"
#include "InmarsatVoiceEvidence.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

struct InmarsatPipeline::Native {
    ~Native() {InmarsatDiagnosticRecording::release(this);}
    std::unique_ptr<InmarsatAero> aero;
    std::unique_ptr<InmarsatChannelizer> channelizer;
    InmarsatAero::MessageSink messageSink;
    InmarsatAero::PcmSink pcmSink;
    uint64_t validated=0, failed=0, voice=0, pcm=0, rejected=0, corrections=0, repeats=0, mutes=0, speech=0;
    InmarsatMessageStore aircraft;
    uint64_t messages=0;
    uint64_t applicationDecoded=0,applicationInvalid=0,applicationUnsupported=0,applicationControl=0;
    uint64_t codecFailures=0,codecAttemptedWords=0,invalidCFrames=0,identityChanges=0,unidentifiedSpeech=0,positionMismatches=0;
    void reset(int bitRate,double rate,double offset,double channel,bool burst) {
        aero.reset(); channelizer.reset(); aircraft.clear();
        if(bitRate==0) return;
        channelizer=std::make_unique<InmarsatChannelizer>(rate,offset);
        aero=std::make_unique<InmarsatAero>(bitRate,burst);
        aero->setMessageSink([this,channel](const InmarsatMessage& incoming) {
            auto m=incoming; m.freqHz=channel;
            ++messages;
            // This private store supplies map identity, not a second message log.
            // Avoid retaining raw/expanded payloads in each of up to 32 workers.
            auto identity=m;
            std::string{}.swap(identity.text);std::string{}.swap(identity.applicationText);
            aircraft.push(std::move(identity));
            if(messageSink) messageSink(m);
        });
        aero->setPcmSink([this](std::span<const int16_t> samples,uint32_t aes) {
            InmarsatDiagnosticRecording::pcm(this,samples);
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
    auto result=probeEnabled_?demod_.stats():InmarsatDemodStats{};
    if(native_->aero) {
        const auto a=native_->aero->stats();
        result.locked=a.locked;result.carrierDetected=result.carrierDetected || a.locked;
        result.ebnoDb=a.ebno;
    }
    result.framesOut=native_->validated;
    return result;
}

void InmarsatPipeline::rejectInput(const char* reason,uint64_t start,size_t count) {
    ++rejectedBlocks_;++consecutiveRejected_;lastInputRejected_=true;lastError_=reason;
    lastRejectedStart_=start;lastRejectedCount_=count;
    if(lastError_=="invalid_iq")++invalidAmplitude_;
    else if(lastError_=="aero_passband")++invalidAeroPassband_;
    else ++invalidGeometry_;
    InmarsatDiagnosticRecording::release(native_.get());
}
void InmarsatPipeline::process(const std::complex<float>* iq, size_t count,
    uint64_t start, double rate, double center, double channel,
    InmarsatDemodMode mode, bool discontinuity) {
    if (count == 0) return;
    if (!iq) {rejectInput("null_input",start,count);return;}
    if (!std::isfinite(rate) || rate < 8000 || rate > 40e6 ||
        !std::isfinite(center) || !std::isfinite(channel) || center <= 0 || channel <= 0 ||
        std::abs(channel - center) >= rate / 2) {
        rejectInput("invalid_geometry",start,count);return;
    }
    const bool burst=mode==InmarsatDemodMode::AeroBurstMsk1200 || mode==InmarsatDemodMode::AeroBurstOqpsk10500;
    const auto probe=mode==InmarsatDemodMode::AeroBurstMsk1200?InmarsatDemodMode::AeroMsk1200:
        mode==InmarsatDemodMode::AeroBurstOqpsk10500?InmarsatDemodMode::AeroOqpsk10500:mode;
    const int bps=mode==InmarsatDemodMode::AeroMsk600?600:
        probe==InmarsatDemodMode::AeroMsk1200?1200:mode==InmarsatDemodMode::AeroVoice8400?8400:
        probe==InmarsatDemodMode::AeroOqpsk10500?10500:0;
    if(bps && (rate<16000 || std::abs(channel-center)+6500>rate/2)) {
        rejectInput("aero_passband",start,count);return;
    }
    const auto begin = std::chrono::steady_clock::now();
    const auto elapsed=[](auto from,auto to){return std::chrono::duration<double,std::milli>(to-from).count();};
    // Validate the whole block before modifying state: NaN must not poison PLL history.
    double power = 0, peak = 0;
    for (size_t i = 0; i < count; ++i) {
        const double re = iq[i].real(), im = iq[i].imag();
        if (!std::isfinite(re) || !std::isfinite(im) || std::abs(re) > 1e6 || std::abs(im) > 1e6) {
            rejectInput("invalid_iq",start,count);return;
        }
        power += re * re + im * im;
        peak = std::max({peak, std::abs(re), std::abs(im)});
    }
    const auto validatedAt=std::chrono::steady_clock::now();
    validationMs_+=elapsed(begin,validatedAt);
    lastInputRejected_=false;consecutiveRejected_=0;
    const bool gap = started_ && (discontinuity || start != nextSample_);
    const bool recordingReset=gap || (started_ && (rate_!=rate || center_!=center || channel_!=channel || mode_!=mode));
    if (!started_ || gap || rate_ != rate || center_ != center || channel_ != channel || mode_ != mode) {
        demod_.reset(probe, rate, channel - center);
        probeReset_=false;
        native_->reset(bps,rate,channel-center,channel,burst);
        ++resets_;
        if (gap) ++gaps_;
    }
    started_ = true;
    rate_ = rate; center_ = center; channel_ = channel; mode_ = mode;
    if(probeEnabled_ && probeReset_) {demod_.reset(probe,rate,channel-center);probeReset_=false;}
    const auto before = demod_.stats();
    const auto setupAt=std::chrono::steady_clock::now();
    setupMs_+=elapsed(validatedAt,setupAt);
    if(probeEnabled_)demod_.process(iq, count);
    else ++probeSkippedBlocks_;
    const auto probeAt=std::chrono::steady_clock::now();
    probeMs_+=elapsed(setupAt,probeAt);
    if(native_->aero) {
        const auto before=native_->aero->stats();
        const auto intermediate=native_->channelizer->process({iq,count});
        const auto channelizedAt=std::chrono::steady_clock::now();
        channelizerMs_+=elapsed(probeAt,channelizedAt);
        InmarsatDiagnosticRecording::begin(native_.get(),channel,static_cast<int>(mode),recordingReset,intermediate);
        InmarsatDiagnosticRecording::iq(native_.get(),{iq,count},rate,center,start);
        InmarsatDiagnosticRecording::counters(native_.get(),before);
        native_->aero->processIf(intermediate);
        InmarsatDiagnosticRecording::counters(native_.get(),native_->aero->stats());
        InmarsatDiagnosticRecording::end(native_.get());
        modemMs_+=elapsed(channelizedAt,std::chrono::steady_clock::now());
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
        native_->codecFailures+=after.codecFailures-before.codecFailures;
        native_->codecAttemptedWords+=after.codecAttemptedWords-before.codecAttemptedWords;
        native_->invalidCFrames+=after.invalidCFrames-before.invalidCFrames;
        native_->identityChanges+=after.identityChanges-before.identityChanges;
        native_->unidentifiedSpeech+=after.unidentifiedSpeechFrames-before.unidentifiedSpeechFrames;
        native_->positionMismatches+=after.positionIdentityMismatches-before.positionIdentityMismatches;
        native_->applicationDecoded+=after.applicationDecoded-before.applicationDecoded;
        native_->applicationInvalid+=after.applicationInvalid-before.applicationInvalid;
        native_->applicationUnsupported+=after.applicationUnsupported-before.applicationUnsupported;
        native_->applicationControl+=after.applicationControl-before.applicationControl;
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
    lastBlockMs_=ms;lastInputMs_=1000.0*count/rate;
    inputSeconds_+=double(count)/rate;
    if(ms>lastInputMs_)++overBudgetBlocks_;
}

nlohmann::json InmarsatPipeline::report() const {
    const auto s = stats();
    const auto a=native_->aero?native_->aero->stats():InmarsatAeroStats{};
    nlohmann::json positions=nlohmann::json::array();
    for(const auto& p:native_->aircraft.positions()) positions.push_back({{"aesId",p.aesId},{"icaoHex",p.icaoHex},
        {"latDeg",p.latDeg},{"lonDeg",p.lonDeg},{"altitudeFt",p.altitudeFt},
        {"callsign",p.callsign},{"registration",p.registration},{"secondsPastHour",p.positionSecondsPastHour}});
    return {{"samples", samples_}, {"blocks", blocks_}, {"nextSample", nextSample_},
        {"inputRejected",rejectedBlocks_},{"lastInputRejected",lastInputRejected_},{"lastError",lastError_},
        {"consecutiveRejected",consecutiveRejected_},{"lastRejectedStart",lastRejectedStart_},{"lastRejectedCount",lastRejectedCount_},
        {"invalidGeometry",invalidGeometry_},{"invalidAmplitude",invalidAmplitude_},{"invalidAeroPassband",invalidAeroPassband_},
        {"probeEnabled",probeEnabled_},{"probeSkippedBlocks",probeSkippedBlocks_},
        {"codecFailures",native_->codecFailures},{"codecAttemptedWords",native_->codecAttemptedWords},
        {"invalidCFrames",native_->invalidCFrames},{"identityChanges",native_->identityChanges},
        {"unidentifiedSpeechFrames",native_->unidentifiedSpeech},{"positionIdentityMismatches",native_->positionMismatches},
        {"applicationDecoded",native_->applicationDecoded},{"applicationInvalid",native_->applicationInvalid},
        {"applicationUnsupported",native_->applicationUnsupported},{"applicationControl",native_->applicationControl},
        {"resets", resets_}, {"discontinuities", gaps_}, {"rateHz", rate_},
        {"centerHz", center_}, {"channelHz", channel_}, {"offsetHz", channel_ - center_},
        {"mode", static_cast<int>(mode_)}, {"symbols", symbols_}, {"rawBlocks", rawBlocks_},
        {"carrierDetected", s.carrierDetected}, {"quality", s.quality}, {"carrierOffsetHz", s.freqOffsetHz},
        {"processingMs", totalMs_}, {"maxBlockMs", maxMs_}, {"peakComponent", peak_},
        {"validationMs",validationMs_},{"setupMs",setupMs_},{"probeMs",probeMs_},
        {"channelizerMs",channelizerMs_},{"modemMs",modemMs_},
        {"inputSeconds",inputSeconds_},{"loadRatio",inputSeconds_>0?totalMs_/(1000*inputSeconds_):0},
        {"lastBlockMs",lastBlockMs_},{"lastInputMs",lastInputMs_},{"overBudgetBlocks",overBudgetBlocks_},
        {"rms", samples_ ? std::sqrt(sumPower_ / samples_) : 0},
        {"protocolLock", s.locked}, {"validatedFrames", native_->validated}, {"voiceFrames", native_->voice},
        {"speechFrames",native_->speech},{"messages",native_->messages},
        {"crcFailed",native_->failed},{"rejectedCFrames",native_->rejected},
        {"codecCorrections",native_->corrections},{"codecRepeats",native_->repeats},{"codecMutes",native_->mutes},
        {"positions",positions},{"pcmSamples", native_->pcm},
        {"softBits",a.softBits},{"voiceAesId",inmarsatVoiceAes(a)},
        {"speechActive",!lastInputRejected_ && inmarsatSpeechActive(a)},
        {"voiceActive",!lastInputRejected_ && inmarsatVoiceActive(a)},
        {"protocolDecoderAvailable",bool(native_->aero)}, {"aeroVocoderAvailable", a.codecAvailable},
        {"capability", native_->aero?"classic_aero_experimental":"physical_probe_only"}};
}
