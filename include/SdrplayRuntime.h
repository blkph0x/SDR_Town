#pragma once

#include <string>
#include <vector>

namespace SdrplayRuntime {
struct Report {
    bool apiLoaded = false;
    bool registered = false;
    std::string apiPath;
    std::string modulePath;
    std::string moduleVersion;
    std::string service;
    std::vector<std::string> errors;
    std::string description() const;
};

// DEC-0122: loads libraries/registry only, never opens or enumerates hardware.
// Failed attempts are retryable. A registered driver is never unloaded/replaced.
Report ensure(const std::string& appDir);
Report ensureCandidates(const std::vector<std::string>& apiCandidates,
                        const std::vector<std::string>& moduleCandidates);
} // namespace SdrplayRuntime
