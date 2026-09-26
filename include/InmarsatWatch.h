#pragma once

#include "InmarsatPipeline.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
#include <map>
#include <set>
#include <string>
#include <vector>

// DEC-0124: these are user scheduling policies, not radio/protocol constants.
struct InmarsatWatchChannel {
    std::string id, label;
    double frequencyHz = 0;
    int rate = 10500; // Negative rates explicitly select burst data.
    bool enabled = true;
    bool voice() const { return rate == 8400; }
    InmarsatDemodMode mode() const;
};

struct InmarsatWatchConfig {
    static constexpr int kMaxConcurrentChannels = 16; // DEC-0135 resource budget, not RF capacity.
    bool enabled = false;
    std::vector<InmarsatWatchChannel> channels;
    int maxConcurrentChannels = 2; // DEC-0124 measured live-input headroom.
    int dataMinSeconds = 10, dataDwellSeconds = 30, positionTarget = 10;
    int voiceAcquireSeconds = 12, voiceIdleSeconds = 6;
    int refreshSeconds = 180, maxVoiceSeconds = 600;
    void validate() const;
    nlohmann::json toJson() const;
    static InmarsatWatchConfig fromJson(const nlohmann::json&);
};

struct InmarsatWatchGroup {
    bool voice = false;
    double centerHz = 0;
    std::vector<InmarsatWatchChannel> channels;
};
std::vector<InmarsatWatchGroup> planInmarsatWatch(const InmarsatWatchConfig&, double sampleRate);

class InmarsatWatchSchedule {
public:
    InmarsatWatchSchedule(InmarsatWatchConfig, double rate, double now);
    const InmarsatWatchGroup& group() const { return groups_.at(index_); }
    size_t index() const { return index_; }
    size_t groupCount() const { return groups_.size(); }
    void position(uint32_t aes); // Only current-visit, CRC-validated positions.
    void validatedData(const std::string& channelId, double now);
    void speech(double now);
    bool advance(double now); // True requires confirmed retune before more IQ.
    nlohmann::json report(double now) const;
private:
    InmarsatWatchConfig cfg_;
    std::vector<InmarsatWatchGroup> groups_;
    size_t index_ = 0, dataCount_ = 0;
    double entered_ = 0, voiceStarted_ = 0, lastSpeech_ = 0;
    bool heardSpeech_ = false;
    std::set<uint32_t> positions_;
    std::map<std::string,double> dataEvidence_;
    size_t freshDataChannels(double now) const;
    std::string reason_ = "Initial collection", collection_ = "pending";
};

// Stable speaker arbitration: a second active decoder never interleaves PCM.
class InmarsatWatchFocus {
public:
    int select(std::span<const uint8_t> speech, double now, int idleSeconds);
    void reset() {channel_=-1;lastSpeech_=0;}
    int channel() const {return channel_;}
    double lastSpeech() const {return lastSpeech_;}
private:
    int channel_=-1;
    double lastSpeech_=0;
};

struct InmarsatChannelDisplay {
    std::string id;
    double frequencyHz=0;
    int rate=0;
    bool locked=false;
    double ebnoDb=0;
    InmarsatConstellation constellation;
};

// One persistent worker/pipeline per active channel, one input block in flight.
// Session owner waits for all workers before callbacks, retunes or destruction.
class InmarsatWatchSession {
public:
    using MessageSink = InmarsatAero::MessageSink;
    using PcmSink = InmarsatAero::PcmSink;
    InmarsatWatchSession(const InmarsatWatchConfig&, double rate, double now,
                         MessageSink, PcmSink, std::function<void()> flush);
    ~InmarsatWatchSession();
    double centerHz() const;
    bool advance(double now);
    void process(std::span<const std::complex<float>>, uint64_t start, double rate,
                 double center, bool gap, double now);
    nlohmann::json report(double now) const;
    std::vector<InmarsatChannelDisplay> displays() const;
private:
    struct Channel;
    void createChannels();
    InmarsatWatchConfig cfg_;
    InmarsatWatchSchedule schedule_;
    std::vector<std::unique_ptr<Channel>> channels_;
    MessageSink messageSink_;
    PcmSink pcmSink_;
    std::function<void()> flush_;
    InmarsatWatchFocus focus_;
    uint32_t focusAes_ = 0;
    uint64_t switches_ = 0, gaps_ = 0;
    double processingSeconds_ = 0, inputSeconds_ = 0, maxBlockMs_ = 0;
};
