#include "InmarsatWatch.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <exception>

InmarsatDemodMode InmarsatWatchChannel::mode() const {
    if(rate == -1200) return InmarsatDemodMode::AeroBurstMsk1200;
    if(rate == -10500) return InmarsatDemodMode::AeroBurstOqpsk10500;
    return InmarsatDemod::modeFromBaud(rate, false);
}

void InmarsatWatchConfig::validate() const {
    if(channels.size()>32) throw std::invalid_argument("Watch list is limited to 32 channels");
    if(maxConcurrentChannels<1 || maxConcurrentChannels>kMaxConcurrentChannels)
        throw std::invalid_argument("Concurrent decoders must be 1 to " + std::to_string(kMaxConcurrentChannels));
    std::set<std::string> ids;
    std::set<std::pair<double,int>> frequencies;
    for(const auto& c:channels) {
        if(c.id.empty() || c.id.size()>64 || c.label.size()>100 || !ids.insert(c.id).second)
            throw std::invalid_argument("Watch channel IDs must be unique and labels bounded");
        if(!std::isfinite(c.frequencyHz) || c.frequencyHz<1e6 || c.frequencyHz>100e9)
            throw std::invalid_argument("Invalid watch frequency");
        if(c.rate!=600 && c.rate!=1200 && c.rate!=8400 && c.rate!=10500 && c.rate!=-1200 && c.rate!=-10500)
            throw std::invalid_argument("Watch supports Classic Aero data and voice rates only");
        if(!frequencies.insert({c.frequencyHz,c.rate}).second)
            throw std::invalid_argument("That frequency and decoder are already saved");
    }
    if(dataMinSeconds<1 || dataDwellSeconds<dataMinSeconds || dataDwellSeconds>600 ||
       positionTarget<1 || positionTarget>256 || voiceAcquireSeconds<1 || voiceAcquireSeconds>120 ||
       voiceIdleSeconds<1 || voiceIdleSeconds>120 || refreshSeconds<10 || refreshSeconds>3600 ||
       maxVoiceSeconds<refreshSeconds || maxVoiceSeconds>7200)
        throw std::invalid_argument("Invalid watch timing: maximum voice visit must cover refresh interval");
}

nlohmann::json InmarsatWatchConfig::toJson() const {
    nlohmann::json j={{"enabled",enabled},{"simultaneousInBand",simultaneousInBand},{"maxConcurrentChannels",maxConcurrentChannels},{"dataMinSeconds",dataMinSeconds},
        {"dataDwellSeconds",dataDwellSeconds},{"positionTarget",positionTarget},
        {"voiceAcquireSeconds",voiceAcquireSeconds},{"voiceIdleSeconds",voiceIdleSeconds},
        {"refreshSeconds",refreshSeconds},{"maxVoiceSeconds",maxVoiceSeconds},
        {"channels",nlohmann::json::array()}};
    for(const auto& c:channels) j["channels"].push_back({{"id",c.id},{"label",c.label},
        {"frequencyHz",c.frequencyHz},{"rate",c.rate},{"enabled",c.enabled}});
    return j;
}

InmarsatWatchConfig InmarsatWatchConfig::fromJson(const nlohmann::json& j) {
    InmarsatWatchConfig c;
    if(!j.is_object()) throw std::invalid_argument("Invalid saved watch settings");
    c.enabled=j.value("enabled",false);
    c.simultaneousInBand=j.value("simultaneousInBand",c.simultaneousInBand);
    c.maxConcurrentChannels=j.value("maxConcurrentChannels",c.maxConcurrentChannels);
    c.dataMinSeconds=j.value("dataMinSeconds",c.dataMinSeconds);
    c.dataDwellSeconds=j.value("dataDwellSeconds",c.dataDwellSeconds);
    c.positionTarget=j.value("positionTarget",c.positionTarget);
    c.voiceAcquireSeconds=j.value("voiceAcquireSeconds",c.voiceAcquireSeconds);
    c.voiceIdleSeconds=j.value("voiceIdleSeconds",c.voiceIdleSeconds);
    c.refreshSeconds=j.value("refreshSeconds",c.refreshSeconds);
    c.maxVoiceSeconds=j.value("maxVoiceSeconds",c.maxVoiceSeconds);
    if(j.contains("channels")) {
        if(!j.at("channels").is_array() || j.at("channels").size()>32)
            throw std::invalid_argument("Invalid saved watch channel list");
        for(const auto& v:j.at("channels")) c.channels.push_back({v.at("id").get<std::string>(),
            v.value("label",std::string{}),v.at("frequencyHz").get<double>(),
            v.at("rate").get<int>(),v.value("enabled",true)});
    }
    c.validate();
    return c;
}

std::vector<InmarsatWatchGroup> planInmarsatWatch(const InmarsatWatchConfig& cfg,double rate) {
    cfg.validate();
    if(!std::isfinite(rate) || rate<16000 || rate>40e6)
        throw std::invalid_argument("Unsupported watch sample rate");
    std::vector<InmarsatWatchGroup> result;
    // Match the native 6.5 kHz channelizer margin. Reserve outer 10% of RF bandwidth.
    const double half=rate*0.45-6500;
    std::vector<InmarsatWatchChannel> enabled;
    for(const auto& c:cfg.channels)if(c.enabled)enabled.push_back(c);
    std::stable_sort(enabled.begin(),enabled.end(),[](const auto& a,const auto& b){return a.frequencyHz<b.frequencyHz;});
    const bool mixed=std::any_of(enabled.begin(),enabled.end(),[](const auto& c){return c.voice();}) &&
        std::any_of(enabled.begin(),enabled.end(),[](const auto& c){return !c.voice();});
    const bool simultaneous=cfg.simultaneousInBand && mixed &&
        enabled.size()<=size_t(cfg.maxConcurrentChannels) &&
        enabled.back().frequencyHz-enabled.front().frequencyHz<=2*half;
    for(bool voice:{false,true}) {
        if(simultaneous && voice)break;
        std::vector<InmarsatWatchChannel> sorted;
        for(const auto& c:enabled) if(simultaneous || c.voice()==voice) sorted.push_back(c);
        std::stable_sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.frequencyHz<b.frequencyHz;});
        for(size_t i=0;i<sorted.size();) {
            const size_t first=i++;
            while(i<sorted.size() && i-first<size_t(cfg.maxConcurrentChannels) && sorted[i].frequencyHz-sorted[first].frequencyHz<=2*half) ++i;
            InmarsatWatchGroup g;
            g.voice=voice;g.simultaneous=simultaneous;g.channels.assign(sorted.begin()+first,sorted.begin()+i);
            const double lo=sorted[i-1].frequencyHz-half, hi=sorted[first].frequencyHz+half;
            // InmarScope voice_ops.cpp offsets by min(200 kHz, Fs/4) to avoid
            // tuner DC. Fit the entire group, preferring this over an RF edge.
            const double preferred=std::clamp((lo+hi)/2-std::min(200000.0,rate*.25),lo,hi);
            std::vector<double> candidates{preferred,lo,hi};
            for(size_t j=first+1;j<i;++j) candidates.push_back((sorted[j-1].frequencyHz+sorted[j].frequencyHz)/2);
            g.centerHz=(lo+hi)/2;
            double best=-1;
            for(double center:candidates) {
                if(center<lo || center>hi || center<1e6) continue;
                double distance=rate;
                for(const auto& c:g.channels) distance=std::min(distance,std::abs(c.frequencyHz-center));
                if(distance>best){best=distance;g.centerHz=center;}
                if(center==preferred && distance>=6500)break;
            }
            result.push_back(std::move(g));
        }
    }
    if(result.empty()) throw std::invalid_argument("Enable at least one saved Aero watch channel");
    return result;
}

InmarsatWatchSchedule::InmarsatWatchSchedule(InmarsatWatchConfig cfg,double rate,double now)
    :cfg_(std::move(cfg)),groups_(planInmarsatWatch(cfg_,rate)),entered_(now),voiceStarted_(now),lastSpeech_(now) {
    dataCount_=std::count_if(groups_.begin(),groups_.end(),[](const auto& g){return !g.voice;});
    if(group().simultaneous){reason_="Continuous in-band reception";collection_="continuous";}
}
void InmarsatWatchSchedule::position(uint32_t aes) {
    if(!group().voice && aes && positions_.size()<256) positions_.insert(aes);
}
void InmarsatWatchSchedule::validatedData(const std::string& id,double now) {
    if(group().voice || !std::isfinite(now))return;
    if(std::any_of(group().channels.begin(),group().channels.end(),[&](const auto& c){return !c.voice() && c.id==id;}))
        dataEvidence_[id]=now;
}
size_t InmarsatWatchSchedule::freshDataChannels(double now) const {
    size_t count=0;
    for(const auto& c:group().channels) {
        if(c.voice())continue;
        const auto it=dataEvidence_.find(c.id);
        if(it!=dataEvidence_.end() && now>=it->second && now-it->second<=cfg_.dataDwellSeconds)++count;
    }
    return count;
}
void InmarsatWatchSchedule::speech(double now) { if(group().voice){lastSpeech_=now;heardSpeech_=true;} }
bool InmarsatWatchSchedule::advance(double now) {
    if(group().simultaneous)return false; // DEC-0136: both roles are already receiving.
    size_t next=index_;
    if(!group().voice) {
        // DEC-0135: map population alone cannot prove most current channels work.
        const bool target=positions_.size()>=size_t(cfg_.positionTarget) &&
            freshDataChannels(now)*2>group().channels.size();
        if(now-entered_<(target?cfg_.dataMinSeconds:cfg_.dataDwellSeconds)) return false;
        next=index_+1;
        if(next==dataCount_) {
            collection_=target?"target reached":positions_.empty()?"no positions decoded":"partial refresh";
            reason_=collection_; voiceStarted_=now;
        } else reason_="Next data group";
        if(next==groups_.size()) next=0;
    } else {
        const bool idle=heardSpeech_?now-lastSpeech_>=cfg_.voiceIdleSeconds:now-entered_>=cfg_.voiceAcquireSeconds;
        const bool overdue=dataCount_>0 && now-voiceStarted_>=cfg_.maxVoiceSeconds;
        if(!idle && !overdue) return false;
        if(dataCount_>0 && (overdue || now-voiceStarted_>=cfg_.refreshSeconds || index_+1==groups_.size())) {
            next=0;reason_=overdue?"Maximum voice visit; refreshing positions":"Voice idle; refreshing positions";
        } else {
            next=index_+1;
            if(next==groups_.size()) next=dataCount_;
            reason_="Voice idle; next voice group";
        }
    }
    if(next==0) {positions_.clear(); if(dataCount_) collection_="refreshing";}
    entered_=now;lastSpeech_=now;heardSpeech_=false;dataEvidence_.clear();
    if(next==index_) return false; // Single-role/single-group: no unnecessary DSP reset.
    index_=next;
    return true;
}
nlohmann::json InmarsatWatchSchedule::report(double now) const {
    const size_t dataChannels=std::count_if(group().channels.begin(),group().channels.end(),[](const auto& c){return !c.voice();});
    return {{"phase",group().simultaneous?"data + voice":group().voice?"voice":"positions"},
        {"simultaneous",group().simultaneous},{"group",index_+1},{"groups",groups_.size()},
        {"visitPositions",positions_.size()},{"positionTarget",cfg_.positionTarget},{"collection",collection_},
        {"reason",reason_},{"groupSeconds",std::max(0.0,now-entered_)},
        {"refreshDue",group().voice && dataCount_>0 && now-voiceStarted_>=cfg_.refreshSeconds},
        {"validatedDataChannels",group().voice?size_t{0}:freshDataChannels(now)},
        {"dataChannels",dataChannels},
        {"dataReady",dataChannels>0 && positions_.size()>=size_t(cfg_.positionTarget) &&
            freshDataChannels(now)*2>dataChannels}};
}

struct InmarsatWatchSession::Channel {
    InmarsatWatchChannel config;
    nlohmann::json report=nlohmann::json::object();
    InmarsatChannelDisplay display;
    std::vector<InmarsatMessage> messages;
    std::vector<int16_t> pcm;
    uint32_t aes=0;
    uint64_t speechFrames=0;
    uint64_t validatedFrames=0;
    bool speech=false;
    struct Input {
        std::span<const std::complex<float>> iq;
        uint64_t start=0;
        double rate=0,center=0;
        bool gap=false;
    } input;
    std::mutex mutex;
    std::condition_variable ready,finished;
    bool pending=false,stopping=false;
    std::exception_ptr error;
    std::thread worker;

    explicit Channel(InmarsatWatchChannel cfg):config(std::move(cfg)),worker([this]{run();}) {}
    ~Channel() {
        {std::lock_guard lock(mutex);stopping=true;}
        ready.notify_one();
        if(worker.joinable())worker.join();
    }
    void submit(Input job) {
        std::lock_guard lock(mutex);
        if(pending)throw std::logic_error("Aero worker already has an IQ block");
        input=job;error=nullptr;pending=true;ready.notify_one();
    }
    std::exception_ptr wait() {
        std::unique_lock lock(mutex);
        finished.wait(lock,[&]{return !pending;});
        return error;
    }
    void run() {
        // Construct AND destroy modem QObjects here. No QObject crosses owners.
        std::unique_ptr<InmarsatPipeline> pipeline;
        for(;;) {
            Input job;
            {std::unique_lock lock(mutex);ready.wait(lock,[&]{return pending||stopping;});
                if(stopping&&!pending)break;
                job=input;}
            try {
                if(!pipeline) {
                    pipeline=std::make_unique<InmarsatPipeline>();
                    pipeline->setMessageSink([this](const InmarsatMessage& m){messages.push_back(m);});
                    pipeline->setPcmSink([this](std::span<const int16_t> block,uint32_t source){
                        if(aes!=source)pcm.clear();
                        aes=source;pcm.insert(pcm.end(),block.begin(),block.end());
                    });
                }
                pcm.clear();messages.clear();
                pipeline->process(job.iq.data(),job.iq.size(),job.start,job.rate,job.center,
                                  config.frequencyHz,config.mode(),job.gap);
                report=pipeline->report();
                const auto stats=pipeline->stats();
                display={config.id,config.frequencyHz,config.rate,stats.locked,stats.ebnoDb,pipeline->constellation()};
            } catch(...) {error=std::current_exception();}
            {std::lock_guard lock(mutex);pending=false;}
            finished.notify_one();
        }
    }
};

int InmarsatWatchFocus::select(std::span<const uint8_t> speech,double now,int idleSeconds) {
    if(channel_>=int(speech.size()))reset();
    if(channel_>=0 && speech[channel_])lastSpeech_=now;
    if(channel_<0 || now-lastSpeech_>=idleSeconds) {
        for(size_t i=0;i<speech.size();++i) if(speech[i]) {
            channel_=int(i);lastSpeech_=now;break;
        }
    }
    return channel_;
}
InmarsatWatchSession::InmarsatWatchSession(const InmarsatWatchConfig& cfg,double rate,double now,
        MessageSink message,PcmSink pcm,std::function<void()> flush)
    :cfg_(cfg),schedule_(cfg,rate,now),messageSink_(std::move(message)),pcmSink_(std::move(pcm)),flush_(std::move(flush)) {createChannels();}
InmarsatWatchSession::~InmarsatWatchSession()=default;
double InmarsatWatchSession::centerHz() const {return schedule_.group().centerHz;}
void InmarsatWatchSession::createChannels() {
    channels_.clear();focus_.reset();focusAes_=0;
    for(const auto& config:schedule_.group().channels) {
        channels_.push_back(std::make_unique<Channel>(config));
    }
}
bool InmarsatWatchSession::advance(double now) {
    if(!schedule_.advance(now)) return false;
    ++switches_;if(flush_)flush_();createChannels();return true;
}
void InmarsatWatchSession::process(std::span<const std::complex<float>> iq,uint64_t start,double rate,
        double center,bool gap,double now) {
    const auto begin=std::chrono::steady_clock::now();
    if(gap) {++gaps_;focus_.reset();focusAes_=0;if(flush_)flush_();}
    std::vector<uint8_t> activity;activity.reserve(channels_.size());
    for(auto& c:channels_) c->submit({iq,start,rate,center,gap});
    // Drain even failed peers before propagating an error: IQ is borrowed until
    // this barrier. Results/callbacks keep configured order, not completion order.
    std::exception_ptr failure;
    for(auto& c:channels_) {auto error=c->wait();if(error&&!failure)failure=error;}
    if(failure)std::rethrow_exception(failure);
    for(auto& c:channels_) {
        const auto validated=c->report.value("validatedFrames",uint64_t{0});
        if(!c->config.voice() && validated>c->validatedFrames)
            schedule_.validatedData(c->config.id,now);
        c->validatedFrames=validated;
        for(const auto& m:c->messages) {
            if(m.validated && m.hasPosition)schedule_.position(m.aesId);
            if(messageSink_)messageSink_(m);
        }
        const auto speech=c->report.value("speechFrames",uint64_t{0});
        c->speech=c->config.voice() && speech>c->speechFrames;c->speechFrames=speech;
        activity.push_back(c->speech);
        if(c->speech)schedule_.speech(now);
    }
    const int previous=focus_.channel();
    const int focus=focus_.select(activity,now,cfg_.voiceIdleSeconds);
    if(previous!=focus){if(flush_)flush_();focusAes_=0;}
    if(focus>=0) {
        const auto& c=*channels_[focus];
        if(!c.pcm.empty()) {
            if(focusAes_!=c.aes){if(flush_)flush_();focusAes_=c.aes;}
            if(pcmSink_)pcmSink_(c.pcm,c.aes);
        }
    }
    const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    processingSeconds_+=elapsed;inputSeconds_+=iq.size()/rate;
    maxBlockMs_=std::max(maxBlockMs_,elapsed*1000);
}
nlohmann::json InmarsatWatchSession::report(double now) const {
    const int focus=focus_.channel();
    nlohmann::json r=focus>=0?channels_[focus]->report:channels_.front()->report;
    r["watch"]=schedule_.report(now);r["watch"]["switches"]=switches_;r["watch"]["iqGaps"]=gaps_;
    r["watch"]["centerHz"]=centerHz();r["watch"]["channels"]=nlohmann::json::array();
    for(const auto& c:channels_) r["watch"]["channels"].push_back({{"id",c->config.id},
        {"frequencyHz",c->config.frequencyHz},{"rate",c->config.rate},{"decoder",c->report}});
    r["watch"]["speakerChannel"]=focus>=0?channels_[focus]->config.id:std::string{};
    r["watch"]["processingSeconds"]=processingSeconds_;r["watch"]["inputSeconds"]=inputSeconds_;
    r["watch"]["loadRatio"]=inputSeconds_>0?processingSeconds_/inputSeconds_:0;
    r["watch"]["maxBlockMs"]=maxBlockMs_;
    r["watch"]["workerCount"]=channels_.size();
    r["watch"]["maxPendingBlocksPerChannel"]=1;
    r["voiceActive"]=focus>=0 && now-focus_.lastSpeech()<0.5 && r.value("voiceActive",false);
    return r;
}

std::vector<InmarsatChannelDisplay> InmarsatWatchSession::displays() const {
    std::vector<InmarsatChannelDisplay> result;
    for(const auto& c:channels_)result.push_back(c->display);
    return result;
}
