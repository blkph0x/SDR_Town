#pragma once
#include "DeviceOwnership.h"
#include <map>
#include <memory>
#include <optional>
#include <vector>

struct Receiver;

// DEC-0182: GUI-host bookkeeping only. Caller serializes; RF ownership remains
// in DeviceManager. A queued old completion must never end a newer takeover.
class ReceiverTakeoverSessions {
public:
    struct SavedReceiver {
        std::weak_ptr<Receiver> receiver;
        bool active = false;
        double frequency = 0;
        int mode = 0;
    };
    struct Session {
        DeviceOwnership::Token token;
        bool tookOverListen = false;
        std::vector<SavedReceiver> receivers;
    };
    Session* find(size_t index) {
        auto it = sessions_.find(index);
        return it == sessions_.end() ? nullptr : &it->second;
    }
    Session* find(const DeviceOwnership::Token& token) {
        auto* session = find(token.index);
        return session && session->token == token ? session : nullptr;
    }
    Session* begin(const DeviceOwnership::Token& token) {
        if (!token) return nullptr;
        if (auto* existing = find(token.index)) return existing->token == token ? existing : nullptr;
        return &sessions_.emplace(token.index, Session{token}).first->second;
    }
    std::optional<Session> take(const DeviceOwnership::Token& token) {
        auto* session = find(token);
        if (!session) return {};
        auto result = std::move(*session);
        sessions_.erase(token.index);
        return result;
    }
    bool empty() const { return sessions_.empty(); }
private:
    std::map<size_t, Session> sessions_;
};
