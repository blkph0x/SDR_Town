#include "HttpGet.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

#include <algorithm>
#include <string>

#ifdef _WIN32
namespace {

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string lastWinHttpError(const char* what) {
    const DWORD err = GetLastError();
    return std::string(what) + " (WinHTTP " + std::to_string(err) + ")";
}

} // namespace
#endif

bool httpGetUrl(const std::string& url, std::string* body, std::string* error, int timeoutMs) {
    if (body) body->clear();
    if (url.empty()) {
        if (error) *error = "empty URL";
        return false;
    }
#ifdef _WIN32
    const std::wstring wurl = utf8ToWide(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{};
    wchar_t path[2048]{};
    wchar_t extra[1024]{};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 1024;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        if (error) *error = lastWinHttpError("bad URL");
        return false;
    }
    std::wstring fullPath(path, uc.dwUrlPathLength);
    fullPath.append(extra, uc.dwExtraInfoLength);

    const DWORD t = static_cast<DWORD>(std::max(3000, timeoutMs));
    HINTERNET session = WinHttpOpen(L"SDR-Town/0.2.78 (CelesTrak TLE)",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS,
                                    0);
    if (!session) {
        if (error) *error = lastWinHttpError("WinHttpOpen failed");
        return false;
    }
    WinHttpSetTimeouts(session, t, t, t, t);

    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    if (!conn) {
        if (error) *error = lastWinHttpError("WinHttpConnect failed");
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD flags = 0;
    if (uc.nScheme == INTERNET_SCHEME_HTTPS) flags |= WINHTTP_FLAG_SECURE;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", fullPath.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) {
        if (error) *error = lastWinHttpError("WinHttpOpenRequest failed");
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return false;
    }
    WinHttpAddRequestHeaders(req, L"Accept: text/plain,*/*", static_cast<DWORD>(-1),
                             WINHTTP_ADDREQ_FLAG_ADD);

    std::string collected;
    bool ok = false;
    if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
        if (status >= 200 && status < 300) {
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(req, &avail)) break;
                if (avail == 0) {
                    ok = !collected.empty();
                    break;
                }
                if (collected.size() + avail > 8 * 1024 * 1024) {
                    if (error) *error = "response too large";
                    break;
                }
                std::string chunk(avail, '\0');
                DWORD got = 0;
                if (!WinHttpReadData(req, chunk.data(), avail, &got) || got == 0) break;
                collected.append(chunk.data(), got);
            }
            if (ok && body) *body = std::move(collected);
            else if (!ok && error && error->empty())
                *error = "empty HTTP body from " + url;
        } else if (error) {
            *error = "HTTP " + std::to_string(status) + " from " + url;
        }
    } else if (error) {
        *error = lastWinHttpError(("request failed for " + url).c_str());
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return ok;
#else
    (void)timeoutMs;
    if (error) *error = "httpGetUrl is implemented for Windows only";
    return false;
#endif
}
