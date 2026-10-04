#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <stdexcept>
#include <set>
#include <vector>
#include <nlohmann/json.hpp>

// DEC-0181: control-plane policy only. The caller serializes operations, including
// validation plus queuing a tune. No driver calls, waits, IQ or audio live here.
class DeviceOwnership {
public:
    enum class Owner { None, Listen, P25, Satcom, Inmarsat, Aircraft, Sstv };
    struct Endpoint { std::string key, domain; };
    struct Token {
        size_t index = size_t(-1);
        uint64_t generation = 0, id = 0;
        Owner owner = Owner::None;
        std::string client;
        explicit operator bool() const { return id != 0; }
        bool operator==(const Token&) const = default;
    };
    using Assignments = std::map<std::string, Owner>;

    static const char* name(Owner owner) {
        switch (owner) {
        case Owner::Listen: return "listen";
        case Owner::P25: return "p25";
        case Owner::Satcom: return "satcom";
        case Owner::Inmarsat: return "inmarsat";
        case Owner::Aircraft: return "aircraft";
        case Owner::Sstv: return "sstv";
        default: return "automatic";
        }
    }
    void bind(std::vector<Endpoint> endpoints) {
        if (!stopping_.empty() || !controls_.empty()) throw std::logic_error("Cannot rebind devices during radio operations");
        endpoints_ = std::move(endpoints);
        leases_.clear();
        ++generation_;
    }
    void append(Endpoint endpoint) { endpoints_.push_back(std::move(endpoint)); }
    bool unique(size_t index) const {
        return index < endpoints_.size() && !endpoints_[index].key.empty() &&
            std::count_if(endpoints_.begin(), endpoints_.end(), [&](const auto& d) {
                return d.key == endpoints_[index].key;
            }) == 1;
    }
    Owner assignment(size_t index) const {
        if (index >= endpoints_.size()) return Owner::None;
        auto it = assignments_.find(endpoints_[index].key);
        return it == assignments_.end() ? Owner::None : it->second;
    }
    const Assignments& assignments() const { return assignments_; }
    bool setAssignments(const Assignments& values, std::string* error) {
        if (!controls_.empty() || !stopping_.empty()) return fail(error, "Wait for radio operations to finish before changing assignments");
        if (values.size() > 128) return fail(error, "Too many device assignments");
        for (const auto& [key, owner] : values)
            if (key.empty() || key.size() > 512 || owner < Owner::Listen || owner > Owner::Sstv)
                return fail(error, "Invalid device assignment");
        for (size_t i = 0; i < endpoints_.size(); ++i) {
            const auto it = values.find(endpoints_[i].key);
            if (it != values.end() && !unique(i))
                return fail(error, "Duplicate device identity; give each SDR a unique serial before assigning it");
            const auto old = assignment(i);
            const auto next = it == values.end() ? Owner::None : it->second;
            if (old != next && leases_.count(i)) return fail(error, "Stop the active workflow before changing its assignment");
            for (size_t j = 0; j < i; ++j) {
                const auto other = values.find(endpoints_[j].key);
                if (sameDomain(i, j) && next != Owner::None && other != values.end() && other->second != next)
                    return fail(error, "Shared SDR hardware cannot be reserved for different workflows");
            }
        }
        assignments_ = values;
        if (error) error->clear();
        return true;
    }
    bool allowed(size_t index, Owner owner, const std::string& client, std::string* error = nullptr) const {
        if (index >= endpoints_.size() || owner < Owner::Listen || owner > Owner::Sstv)
            return fail(error, "Invalid device or workflow");
        for (auto stopping : stopping_)
            if (stopping == index || sameDomain(index, stopping)) return fail(error, "Radio is stopping; wait for it to finish");
        if (!unique(index)) return fail(error, "Device identity is missing or ambiguous; rescan or set unique serials");
        if (controlBusy(index)) return fail(error, "Radio control operation in progress");
        const auto reserved = assignment(index);
        if (reserved != Owner::None && reserved != owner)
            return fail(error, std::string("Device reserved for ") + name(reserved));
        for (const auto& [other, token] : leases_) {
            if (other == index) {
                if (token.owner != owner || token.client != client)
                    return fail(error, std::string("Device in use by ") + name(token.owner));
            } else if (sameDomain(index, other)) {
                return fail(error, "Another tuner of this shared physical SDR is in use");
            }
        }
        // An idle sibling reservation also protects a shared SDRplay domain.
        for (size_t i = 0; i < endpoints_.size(); ++i) {
            if (i == index || !sameDomain(index, i)) continue;
            const auto sibling = assignment(i);
            if (sibling != Owner::None && sibling != owner)
                return fail(error, std::string("Shared SDR hardware reserved for ") + name(sibling));
        }
        if (error) error->clear();
        return true;
    }
    Token claim(size_t index, Owner owner, const std::string& client, std::string* error = nullptr) {
        if (client.empty() || !allowed(index, owner, client, error)) return {};
        auto it = leases_.find(index);
        if (it != leases_.end()) return it->second;
        Token token{index, generation_, ++nextId_, owner, client};
        leases_.emplace(index, token);
        return token;
    }
    bool valid(const Token& token) const {
        const auto it = leases_.find(token.index);
        return token && token.generation == generation_ && it != leases_.end() && it->second == token;
    }
    bool release(const Token& token) {
        if (!valid(token)) return false;
        leases_.erase(token.index);
        return true;
    }
    void invalidate(size_t index) { leases_.erase(index); }
    bool beginStop(size_t index) { return index < endpoints_.size() && stopping_.insert(index).second; }
    void endStop(size_t index) { stopping_.erase(index); }
    bool stopping() const { return !stopping_.empty(); }
    // DEC-0184: these permits pin a physical domain while the caller releases
    // its policy lock for USB I/O. Release/invalidate may retire the lease, but
    // no replacement can enter until the old command actually finishes.
    uint64_t beginControl(size_t index, const Token* token, std::string* error = nullptr) {
        if (!unique(index)) { fail(error, "Radio identity is unavailable or ambiguous"); return 0; }
        if (controlBusy(index)) { fail(error, "Radio control operation in progress"); return 0; }
        for (auto other : stopping_)
            if (other == index || sameDomain(index, other)) { fail(error, "Radio is stopping"); return 0; }
        if (token) {
            if (token->index != index || !valid(*token) || !allowed(index, token->owner, token->client, error)) {
                fail(error, "Device ownership expired or belongs to another session"); return 0;
            }
        } else {
            for (const auto& [other, lease] : leases_) {
                if (other == index && lease.owner == Owner::Listen && lease.client == "legacy-listen") continue;
                if (other == index || sameDomain(index, other)) {
                    fail(error, std::string("Stop the ") + name(lease.owner) + " workflow before changing this radio"); return 0;
                }
            }
        }
        if (error) error->clear();
        const auto id = ++nextControlId_;
        controls_[index] = id;
        return id;
    }
    bool endControl(size_t index, uint64_t id) {
        const auto it = controls_.find(index);
        if (it == controls_.end() || it->second != id) return false;
        controls_.erase(it);
        return true;
    }
    bool controlBusy(size_t index) const {
        if (index >= endpoints_.size()) return false;
        for (const auto& [other, id] : controls_)
            if (other == index || sameDomain(index, other)) return true;
        return false;
    }
    bool controlling() const { return !controls_.empty(); }
    Token current(size_t index) const {
        const auto it = leases_.find(index);
        return it == leases_.end() ? Token{} : it->second;
    }
    static Assignments parse(const nlohmann::json& doc) {
        if (!doc.is_object() || doc.value("schema", 0) != 1 ||
            !doc.contains("assignments") || !doc["assignments"].is_object() || doc["assignments"].size() > 128)
            throw std::runtime_error("Invalid device assignment file");
        Assignments out;
        for (const auto& [key, value] : doc["assignments"].items()) {
            if (key.empty() || key.size() > 512 || !value.is_string())
                throw std::runtime_error("Invalid device assignment entry");
            bool found = false;
            for (auto owner : {Owner::Listen, Owner::P25, Owner::Satcom, Owner::Inmarsat, Owner::Aircraft, Owner::Sstv}) {
                if (value == name(owner)) { out[key] = owner; found = true; break; }
            }
            if (!found) throw std::runtime_error("Unknown device assignment workflow");
        }
        return out;
    }
    static nlohmann::json serialize(const Assignments& values) {
        nlohmann::json map = nlohmann::json::object();
        for (const auto& [key, owner] : values) if (owner != Owner::None) map[key] = name(owner);
        return {{"schema", 1}, {"assignments", map}};
    }
private:
    bool sameDomain(size_t a, size_t b) const {
        return !endpoints_[a].domain.empty() && endpoints_[a].domain == endpoints_[b].domain;
    }
    static bool fail(std::string* error, const std::string& text) {
        if (error) *error = text;
        return false;
    }
    std::vector<Endpoint> endpoints_;
    Assignments assignments_;
    std::map<size_t, Token> leases_;
    std::set<size_t> stopping_;
    std::map<size_t, uint64_t> controls_;
    uint64_t nextControlId_ = 0;
    uint64_t generation_ = 0, nextId_ = 0;
};
