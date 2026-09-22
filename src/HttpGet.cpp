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

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring output(static_cast<size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(),
                            static_cast<int>(value.size()), output.data(), count) != count) {
        return {};
    }
    return output;
}

std::string winHttpError(const std::string& what, DWORD code) {
    return what + " (WinHTTP " + std::to_string(code) + ")";
}

std::string lastWinHttpError(const std::string& what) {
    return winHttpError(what, GetLastError());
}

} // namespace
#endif

bool httpGetUrl(const std::string& url, std::string* body, std::string* error, int timeoutMs) {
    if (body) body->clear();
    if (error) error->clear();
    if (url.empty()) {
        if (error) *error = "empty URL";
        return false;
    }

#ifdef _WIN32
    const std::wstring wideUrl = utf8ToWide(url);
    if (wideUrl.empty()) {
        if (error) *error = "URL is not valid UTF-8";
        return false;
    }

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256]{};
    wchar_t path[2048]{};
    wchar_t extra[2048]{};
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts)) {
        if (error) *error = lastWinHttpError("bad URL: " + url);
        return false;
    }

    std::wstring requestPath(path, parts.dwUrlPathLength);
    requestPath.append(extra, parts.dwExtraInfoLength);
    if (requestPath.empty()) requestPath = L"/";

    const DWORD receiveTimeout = static_cast<DWORD>(std::clamp(timeoutMs, 3000, 30000));
    const DWORD connectTimeout = std::min<DWORD>(receiveTimeout, 5000);
    const DWORD resolveTimeout = std::min<DWORD>(receiveTimeout, 3000);

    DWORD accessType = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
#ifdef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
    // This follows modern Windows proxy discovery more closely than the old
    // DEFAULT_PROXY path, which can differ from a working browser configuration.
    accessType = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
#endif

    HINTERNET session = WinHttpOpen(L"SDR-Town/0.2 (TLE downloader)",
                                    accessType,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS,
                                    0);
#ifdef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
    if (!session && accessType == WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY) {
        session = WinHttpOpen(L"SDR-Town/0.2 (TLE downloader)",
                              WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS,
                              0);
    }
#endif
    if (!session) {
        if (error) *error = lastWinHttpError("WinHttpOpen failed");
        return false;
    }

    WinHttpSetTimeouts(session, resolveTimeout, connectTimeout,
                       receiveTimeout, receiveTimeout);

#ifdef WINHTTP_OPTION_DECOMPRESSION
    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    WinHttpSetOption(session, WINHTTP_OPTION_DECOMPRESSION,
                     &decompression, sizeof(decompression));
#endif

    HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
    if (!connection) {
        if (error) *error = lastWinHttpError("WinHttpConnect failed for " + url);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD flags = 0;
    if (parts.nScheme == INTERNET_SCHEME_HTTPS) flags |= WINHTTP_FLAG_SECURE;
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", requestPath.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        if (error) *error = lastWinHttpError("WinHttpOpenRequest failed for " + url);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    WinHttpAddRequestHeaders(
        request,
        L"Accept: text/plain,*/*\r\nCache-Control: no-cache\r\nConnection: close",
        static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    std::string collected;
    bool ok = false;
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        if (error) *error = lastWinHttpError("send failed for " + url);
    } else if (!WinHttpReceiveResponse(request, nullptr)) {
        if (error) *error = lastWinHttpError("receive failed for " + url);
    } else {
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (!WinHttpQueryHeaders(request,
                                 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                                 WINHTTP_NO_HEADER_INDEX)) {
            if (error) *error = lastWinHttpError("could not read HTTP status from " + url);
        } else if (status < 200 || status >= 300) {
            if (error) *error = "HTTP " + std::to_string(status) + " from " + url;
        } else {
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available)) {
                    if (error) *error = lastWinHttpError("response query failed for " + url);
                    break;
                }
                if (available == 0) {
                    ok = !collected.empty();
                    break;
                }
                if (collected.size() + available > 8U * 1024U * 1024U) {
                    if (error) *error = "response too large from " + url;
                    break;
                }

                std::string chunk(available, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), available, &read)) {
                    if (error) *error = lastWinHttpError("response read failed for " + url);
                    break;
                }
                if (read == 0) {
                    ok = !collected.empty();
                    break;
                }
                collected.append(chunk.data(), read);
            }
        }
    }

    if (ok && body) {
        *body = std::move(collected);
    } else if (!ok && error && error->empty()) {
        *error = "empty HTTP body from " + url;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
#else
    (void)timeoutMs;
    if (error) *error = "httpGetUrl is implemented for Windows only";
    return false;
#endif
}
