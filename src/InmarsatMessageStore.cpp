#include "InmarsatMessageStore.h"

#include <algorithm>
#include <chrono>

namespace {

double unixNow() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

const char* InmarsatMessage::kindName(InmarsatMsgKind k) {
    switch (k) {
    case InmarsatMsgKind::Acars: return "acars";
    case InmarsatMsgKind::Su: return "su";
    case InmarsatMsgKind::CAssign: return "cassign";
    case InmarsatMsgKind::Egc: return "egc";
    case InmarsatMsgKind::Voice: return "voice";
    case InmarsatMsgKind::Status:
    default: return "status";
    }
}

nlohmann::json InmarsatMessage::toJson() const {
    nlohmann::json j;
    j["kind"] = kindName(kind);
    j["unixTime"] = unixTime;
    j["freqHz"] = freqHz;
    j["freqMHz"] = freqHz / 1e6;
    j["aesId"] = aesId;
    j["gesId"] = gesId;
    j["icaoHex"] = icaoHex;
    j["label"] = label;
    j["text"] = text;
    j["hasPosition"] = hasPosition;
    j["validated"] = validated;
    j["registration"] = registration;
    j["callsign"] = callsign;
    if (hasPosition) {
        j["latDeg"] = latDeg;
        j["lonDeg"] = lonDeg;
        j["altitudeFt"] = altitudeFt;
        j["secondsPastHour"] = positionSecondsPastHour;
    }
    if (voiceRxHz > 0) j["voiceRxHz"] = voiceRxHz;
    if (voiceTxHz > 0) j["voiceTxHz"] = voiceTxHz;
    return j;
}

InmarsatMessageStore& InmarsatMessageStore::instance() {
    static InmarsatMessageStore s;
    return s;
}

void InmarsatMessageStore::push(InmarsatMessage msg) {
    if (msg.unixTime <= 0.0) msg.unixTime = unixNow();
    std::lock_guard<std::mutex> lk(mutex_);
    // DEC-0133: the chronological message ring is intentionally small, but a
    // busy Aero channel must not evict the last validated map fix. Keep one
    // separately bounded, latest-first-generation record per aircraft.
    if (msg.validated && msg.hasPosition && msg.aesId != 0) {
        const auto existing = std::find_if(positions_.begin(), positions_.end(),
            [&](const InmarsatMessage& position) { return position.aesId == msg.aesId; });
        if (existing != positions_.end()) positions_.erase(existing);
        positions_.push_back(msg);
        while (positions_.size() > kMaxPositions) positions_.pop_front();
    }
    msgs_.push_back(std::move(msg));
    while (msgs_.size() > kMax) msgs_.pop_front();
}

std::vector<InmarsatMessage> InmarsatMessageStore::recent(size_t limit) const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<InmarsatMessage> out;
    if (msgs_.empty() || limit == 0) return out;
    const size_t n = std::min(limit, msgs_.size());
    out.assign(msgs_.end() - static_cast<std::ptrdiff_t>(n), msgs_.end());
    return out;
}

std::vector<InmarsatMessage> InmarsatMessageStore::recentPositions(size_t limit) const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<InmarsatMessage> out;
    if (positions_.empty() || limit == 0) return out;
    const size_t n = std::min(limit, positions_.size());
    out.assign(positions_.end() - static_cast<std::ptrdiff_t>(n), positions_.end());
    return out;
}

nlohmann::json InmarsatMessageStore::recentJson(size_t limit, size_t offset) const {
    std::lock_guard<std::mutex> lk(mutex_);
    nlohmann::json arr = nlohmann::json::array();
    if (msgs_.empty()) return {{"ok", true}, {"messages", arr}, {"total", 0}};
    const size_t total = msgs_.size();
    size_t start = 0;
    if (offset < total) start = total - 1 - offset;
    else return {{"ok", true}, {"messages", arr}, {"total", total}};
    size_t count = 0;
    for (size_t i = start + 1; i-- > 0 && count < limit;) {
        arr.push_back(msgs_[i].toJson());
        ++count;
        if (i == 0) break;
    }
    return {{"ok", true}, {"messages", arr}, {"total", total}};
}

void InmarsatMessageStore::clear() {
    std::lock_guard<std::mutex> lk(mutex_);
    msgs_.clear();
    positions_.clear();
}
