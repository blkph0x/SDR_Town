#pragma once
#include <stdexcept>
#include <string>

// Named controller identifiers also form settings/file keys (DEC-0187/0189).
// Empty is reserved for the backward-compatible default controller.
inline std::string normalizedWorkflowSessionId(std::string id) {
    if (id.size() > 64) throw std::invalid_argument("Session name exceeds 64 characters");
    for (auto& c : id) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            throw std::invalid_argument("Session names use letters, digits, underscores or hyphens");
    }
    return id;
}
