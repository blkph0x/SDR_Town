#include "SstvReceiverFeed.h"
#include <stdexcept>

namespace {
// DEC-0125: keep new try-lock publishers out until waiting control work finishes.
struct ControlAdmission {
    std::atomic<unsigned>& pending;
    explicit ControlAdmission(std::atomic<unsigned>& count):pending(count){++pending;}
    ~ControlAdmission(){--pending;}
};
}

std::optional<SstvInputStats> SstvReceiverFeed::stats() const {
    ControlAdmission admission(controlPending_);
    std::lock_guard lock(mutex_);
    if(!input_) return std::nullopt;
    return input_->stats();
}

std::shared_ptr<SstvLiveInput> SstvReceiverFeed::attach() {
    auto input=std::make_shared<SstvLiveInput>();
    input->start();
    ControlAdmission admission(controlPending_);
    std::lock_guard lock(mutex_);
    if(input_) throw std::runtime_error("SSTV receiver already has a live session");
    missed_=false; input_=input; attached_=true;
    return input;
}
void SstvReceiverFeed::detach(const std::shared_ptr<SstvLiveInput>& input) {
    ControlAdmission admission(controlPending_);
    std::lock_guard lock(mutex_);
    if(!input_ || input_!=input) return;
    attached_=false; input_->stop(); input_.reset(); missed_=false;
}
void SstvReceiverFeed::finish(const std::shared_ptr<SstvLiveInput>& input) {
    ControlAdmission admission(controlPending_);
    std::lock_guard lock(mutex_);
    if(!input_ || input_!=input) return;
    attached_=false;
    if(missed_.exchange(false)) input_->invalidate(SstvInputGap::Contention);
    input_->finish();
}
void SstvReceiverFeed::publish(const FmMultiplexBlock& block, uint64_t sourceId,
                               DemodMode mode) {
    if(!attached_.load()) return;
    if(controlPending_.load()) {missed_=true;return;}
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock() || controlPending_.load()) {missed_=true;return;}
    if(!input_) return;
    if(missed_.exchange(false)) input_->invalidate(SstvInputGap::Contention);
    input_->tryPush(block,sourceId,mode);
}
void SstvReceiverFeed::discontinuity() {
    if(!attached_.load()) return;
    if(controlPending_.load()) {missed_=true;return;}
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock() || controlPending_.load()) {missed_=true;return;}
    if(input_) input_->invalidate(SstvInputGap::Source);
}
