#pragma once

#include <string>

// Blocking HTTPS GET for TLE / OpenSky from CLI or a worker thread.
// Never call on the Qt GUI thread — use a worker and marshal the result.
bool httpGetUrl(const std::string& url, std::string* body, std::string* error, int timeoutMs = 20000);
