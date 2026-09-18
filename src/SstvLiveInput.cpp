#include "SstvLiveInput.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {constexpr uint32_t reason(SstvInputGap value) {return static_cast<uint32_t>(value);}}

SstvLiveInput::SstvLiveInput():storage_(std::make_unique<Storage>()) {}
SstvLiveInput::~SstvLiveInput()=default;
void SstvLiveInput::invalidate(SstvInputGap reason) {
    if(active_.load()) pending_.fetch_or(static_cast<uint32_t>(reason));
}
void SstvLiveInput::resetLocked(uint32_t reasons) {
    discarded_+=queued_; queued_=0; size_=0; head_=0; identity_=false;
    gap_|=reasons; ++generation_; ++gaps_;
}
void SstvLiveInput::pendingLocked() {
    if(const auto reasons=pending_.exchange(0)) resetLocked(reasons);
}
void SstvLiveInput::start() {
    std::lock_guard lock(mutex_);
    pending_.store(0); resetLocked(reason(SstvInputGap::Start)); active_.store(true);
}
void SstvLiveInput::stop() {
    std::lock_guard lock(mutex_);
    active_.store(false); pendingLocked(); resetLocked(reason(SstvInputGap::Stop));
}
void SstvLiveInput::finish() {
    std::lock_guard lock(mutex_);
    active_.store(false); pendingLocked();
}
SstvPushResult SstvLiveInput::tryPush(const FmMultiplexBlock& block,uint64_t sourceId,DemodMode mode) {
    if(!active_.load()) return SstvPushResult::Inactive;
    if(mode!=DemodMode::NFM || block.samples.empty() || block.samples.size()>SstvInputEvent::maxSamples ||
       !std::isfinite(block.sampleRate) || block.sampleRate<8000 || block.sampleRate>96000 ||
       !std::isfinite(block.targetHz) || block.targetHz<=0 ||
       block.firstSample>std::numeric_limits<uint64_t>::max()-block.samples.size() ||
       !std::all_of(block.samples.begin(),block.samples.end(),[](float x){return std::isfinite(x);})) {
        ++invalid_; rejectedSamples_+=block.samples.size();
        pending_.fetch_or(reason(SstvInputGap::Invalid)); return SstvPushResult::Invalid;
    }
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock()) {
        ++contended_; rejectedSamples_+=block.samples.size();
        pending_.fetch_or(reason(SstvInputGap::Contention)); return SstvPushResult::Contended;
    }
    if(!active_.load()) return SstvPushResult::Inactive;
    pendingLocked();
    uint32_t changed=0;
    if(block.discontinuity || (identity_ && (sourceId!=source_ || block.epoch!=epoch_ ||
       block.sampleRate!=rate_ || block.targetHz!=target_))) changed|=reason(SstvInputGap::Source);
    if(identity_ && block.firstSample!=next_) changed|=reason(SstvInputGap::Position);
    if(changed) resetLocked(changed);
    if(size_==slotCount || queued_+block.samples.size()>size_t(block.sampleRate*2)) resetLocked(reason(SstvInputGap::Overflow));
    auto& destination=storage_->blocks[(head_+size_)%slotCount];
    std::copy(block.samples.begin(),block.samples.end(),destination.samples.begin());
    destination.count=block.samples.size(); destination.gapReasons=0;
    destination.generation=generation_; destination.sourceId=sourceId; destination.epoch=block.epoch;
    destination.firstSample=block.firstSample; destination.sampleRate=block.sampleRate; destination.targetHz=block.targetHz;
    ++size_; queued_+=destination.count; accepted_+=destination.count;
    identity_=true; source_=sourceId; epoch_=block.epoch; rate_=block.sampleRate; target_=block.targetHz;
    next_=block.firstSample+destination.count;
    return SstvPushResult::Accepted;
}
std::optional<SstvInputEvent> SstvLiveInput::pop() {
    std::lock_guard lock(mutex_);
    pendingLocked();
    if(gap_) {
        SstvInputEvent result; result.gapReasons=gap_; result.generation=generation_; gap_=0;
        return result;
    }
    if(!size_) return std::nullopt;
    auto result=storage_->blocks[head_];
    head_=(head_+1)%slotCount; --size_; queued_-=result.count; consumed_+=result.count;
    return result;
}
SstvInputStats SstvLiveInput::stats() const {
    std::lock_guard lock(mutex_);
    return {accepted_,consumed_,discarded_+rejectedSamples_.load(),gaps_,generation_,queued_,size_,invalid_.load(),contended_.load(),active_.load()};
}
