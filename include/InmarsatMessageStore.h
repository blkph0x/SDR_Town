#pragma once

#include <nlohmann/json.hpp>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

enum class InmarsatMsgKind {
    Acars = 0,
    Su = 1,
    CAssign = 2,
    Egc = 3,
    Voice = 4,
    Status = 5
};

struct InmarsatMessage {
    InmarsatMsgKind kind = InmarsatMsgKind::Status;
    double unixTime = 0.0;
    double freqHz = 0.0;
    uint32_t aesId = 0;
    uint8_t gesId = 0;
    std::string icaoHex;
    std::string label;
    std::string text;
    double latDeg = 0.0;
    double lonDeg = 0.0;
    bool hasPosition = false;
    double voiceRxHz = 0.0;
    double voiceTxHz = 0.0;

    nlohmann::json toJson() const;
    static const char* kindName(InmarsatMsgKind k);
};

class InmarsatMessageStore {
public:
    static InmarsatMessageStore& instance();

    void push(InmarsatMessage msg);
    std::vector<InmarsatMessage> recent(size_t limit = 100) const;
    nlohmann::json recentJson(size_t limit = 100, size_t offset = 0) const;
    void clear();

private:
    InmarsatMessageStore() = default;
    mutable std::mutex mutex_;
    std::deque<InmarsatMessage> msgs_;
    static constexpr size_t kMax = 500;
};
