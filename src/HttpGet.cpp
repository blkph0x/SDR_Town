#include "HttpGet.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wininet.h>
#endif

#include <algorithm>

bool httpGetUrl(const std::string& url, std::string* body, std::string* error, int timeoutMs) {
    if (body) body->clear();
    if (url.empty()) {
        if (error) *error = "empty URL";
        return false;
    }
#ifdef _WIN32
    HINTERNET inet = InternetOpenA("SDR-Town/0.2.74", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!inet) {
        if (error) *error = "InternetOpen failed";
        return false;
    }
    const DWORD timeout = static_cast<DWORD>(std::max(1000, timeoutMs));
    InternetSetOptionA(inet, INTERNET_OPTION_CONNECT_TIMEOUT, const_cast<DWORD*>(&timeout), sizeof(timeout));
    InternetSetOptionA(inet, INTERNET_OPTION_RECEIVE_TIMEOUT, const_cast<DWORD*>(&timeout), sizeof(timeout));
    InternetSetOptionA(inet, INTERNET_OPTION_SEND_TIMEOUT, const_cast<DWORD*>(&timeout), sizeof(timeout));

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI;
    if (url.rfind("https://", 0) == 0) flags |= INTERNET_FLAG_SECURE;
    HINTERNET req = InternetOpenUrlA(inet, url.c_str(), "Accept: */*\r\n", static_cast<DWORD>(-1), flags, 0);
    if (!req) {
        if (error) *error = "InternetOpenUrl failed for " + url;
        InternetCloseHandle(inet);
        return false;
    }

    std::string collected;
    collected.reserve(65536);
    char buf[4096];
    DWORD got = 0;
    while (InternetReadFile(req, buf, sizeof(buf), &got) && got > 0) {
        collected.append(buf, buf + got);
        if (collected.size() > 8 * 1024 * 1024) {
            InternetCloseHandle(req);
            InternetCloseHandle(inet);
            if (error) *error = "response too large";
            return false;
        }
    }
    InternetCloseHandle(req);
    InternetCloseHandle(inet);
    if (collected.empty()) {
        if (error) *error = "empty HTTP body from " + url;
        return false;
    }
    if (body) *body = std::move(collected);
    return true;
#else
    (void)timeoutMs;
    if (error) *error = "httpGetUrl is implemented for Windows only";
    return false;
#endif
}
