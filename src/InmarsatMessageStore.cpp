#include "InmarsatMessageStore.h"
#include <cmath>

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
    if(msg.validated && msg.hasPosition && msg.aesId>0 && msg.aesId<=0xffffff &&
       std::isfinite(msg.unixTime) && std::isfinite(msg.latDeg) && std::isfinite(msg.lonDeg) &&
       std::abs(msg.latDeg)<=90 && std::abs(msg.lonDeg)<=180) {
        const auto existing=positions_.find(msg.aesId);
        if(existing!=positions_.end()) {
            if(msg.unixTime>=existing->second.unixTime)existing->second=msg;
        } else {
            if(positions_.size()==kMaxPositions) {
                const auto oldest=std::min_element(positions_.begin(),positions_.end(),
                    [](const auto& a,const auto& b){return a.second.unixTime<b.second.unixTime;});
                if(msg.unixTime>=oldest->second.unixTime) {
                    positions_.erase(oldest);positions_.emplace(msg.aesId,msg);
                }
            } else positions_.emplace(msg.aesId,msg);
        }
    }
    msgs_.push_back(std::move(msg));
    while (msgs_.size() > kMax) msgs_.pop_front();
}

std::vector<InmarsatMessage> InmarsatMessageStore::positions() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<InmarsatMessage> result;result.reserve(positions_.size());
    for(const auto& [id,message]:positions_)result.push_back(message);
    return result;
}

std::vector<InmarsatMessage> InmarsatMessageStore::recent(size_t limit) const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<InmarsatMessage> out;
    if (msgs_.empty() || limit == 0) return out;
    const size_t n = std::min(limit, msgs_.size());
    out.assign(msgs_.end() - static_cast<std::ptrdiff_t>(n), msgs_.end());
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
