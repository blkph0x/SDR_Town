#include "SdrTownControlClient.h"
#include "LocalControlPairing.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <chrono>
#include <charconv>
#include <cctype>
#include <nlohmann/json.hpp>

#pragma comment(lib, "ws2_32.lib")

namespace {

constexpr uint16_t kDefaultPort = 8765;
constexpr uint32_t kDefaultTimeoutMs = 2500;

struct ResolvedConfig {
    std::string host = "127.0.0.1";
    uint16_t port = kDefaultPort;
    std::string token;
    uint32_t timeoutMs = kDefaultTimeoutMs;
};

struct WsaSession {
    WsaSession() {
        WSADATA data{};
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WsaSession() {
        if (ok) WSACleanup();
    }
    bool ok = false;
};

ResolvedConfig resolveConfig(const SdrTownControlConfig* config)
{
    ResolvedConfig out;
    if (config) {
        if (config->host && config->host[0]) out.host = config->host;
        if (config->port != 0) out.port = config->port;
        if (config->token && config->token[0]) out.token = config->token;
        if (config->timeoutMs != 0) out.timeoutMs = config->timeoutMs;
    }
    // Never send an automatically loaded local credential to an arbitrary host
    // or DNS name. Explicit caller credentials retain their existing precedence.
    if (out.token.empty() && out.host == "127.0.0.1") localControlPairingToken(out.token, false);
    return out;
}

std::string jsonEscape(const char* value)
{
    std::string out;
    if (!value) return out;
    for (const unsigned char ch : std::string(value)) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 32) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(ch));
                    out += buf;
                } else {
                    out += static_cast<char>(ch);
                }
        }
    }
    return out;
}

void copyResponse(const std::string& response, char* out, size_t outBytes)
{
    if (!out || outBytes == 0) return;
    const size_t n = std::min(outBytes - 1, response.size());
    std::memcpy(out, response.data(), n);
    out[n] = '\0';
}

bool responseOk(const std::string& body)
{
    const auto parsed = nlohmann::json::parse(body, nullptr, false);
    return parsed.is_object() && parsed.contains("ok") && parsed["ok"].is_boolean() && parsed["ok"].get<bool>();
}

bool sendAll(SOCKET socket, const char* data, int length)
{
    int sent = 0;
    while (sent < length) {
        const int n = send(socket, data + sent, length - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool httpTokenChar(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
        || (c != 0 && std::strchr("!#$%&'*+-.^_`|~", c));
}

bool httpFieldChar(unsigned char c)
{
    return c == '\t' || (c >= 32 && c != 127);
}

bool parseHttpStatus(std::string_view line, int& status)
{
    if (line.size() < 13 || (line.substr(0, 9) != "HTTP/1.1 " && line.substr(0, 9) != "HTTP/1.0 ")
        || line[12] != ' ' || !std::all_of(line.begin() + 9, line.begin() + 12,
            [](unsigned char c) { return c >= '0' && c <= '9'; })
        || !std::all_of(line.begin() + 13, line.end(), httpFieldChar)) return false;
    status = (line[9] - '0') * 100 + (line[10] - '0') * 10 + line[11] - '0';
    return status >= 100 && status <= 599;
}

int requestJson(const SdrTownControlConfig* config,
                const char* method,
                const char* path,
                const std::string& body,
                char* responseJson,
                size_t responseJsonBytes)
{
    if (!method || !path || path[0] != '/' || !responseJson || responseJsonBytes < 128) return SDRTOWN_CONTROL_BAD_ARGUMENT;
    const ResolvedConfig cfg = resolveConfig(config);
    WsaSession wsa;
    if (!wsa.ok) {
        copyResponse("{\"ok\":false,\"error\":\"WSAStartup failed\"}", responseJson, responseJsonBytes);
        return SDRTOWN_CONTROL_SOCKET_ERROR;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* result = nullptr;
    const std::string portText = std::to_string(cfg.port);
    if (getaddrinfo(cfg.host.c_str(), portText.c_str(), &hints, &result) != 0 || !result) {
        copyResponse("{\"ok\":false,\"error\":\"could not resolve SDR Town control host\"}",
                     responseJson, responseJsonBytes);
        return SDRTOWN_CONTROL_SOCKET_ERROR;
    }

    SOCKET socket = INVALID_SOCKET;
    for (addrinfo* it = result; it; it = it->ai_next) {
        socket = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (socket == INVALID_SOCKET) continue;
        DWORD timeout = cfg.timeoutMs;
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        if (::connect(socket, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0) break;
        closesocket(socket);
        socket = INVALID_SOCKET;
    }
    freeaddrinfo(result);
    if (socket == INVALID_SOCKET) {
        copyResponse("{\"ok\":false,\"error\":\"SDR Town control server is not reachable\"}",
                     responseJson, responseJsonBytes);
        return SDRTOWN_CONTROL_SOCKET_ERROR;
    }

    std::ostringstream request;
    request << method << " " << path << " HTTP/1.1\r\n"
            << "Host: " << cfg.host << ":" << cfg.port << "\r\n"
            << "Connection: close\r\n"
            << "Content-Type: application/json\r\n";
    if (!cfg.token.empty()) request << "Authorization: Bearer " << cfg.token << "\r\n";
    request << "Content-Length: " << body.size() << "\r\n\r\n" << body;

    const std::string requestText = request.str();
    if (!sendAll(socket, requestText.data(), static_cast<int>(requestText.size()))) {
        closesocket(socket);
        copyResponse("{\"ok\":false,\"error\":\"send failed\"}", responseJson, responseJsonBytes);
        return SDRTOWN_CONTROL_SOCKET_ERROR;
    }

    std::string response;
    constexpr size_t maxHeaderBytes = 16384;
    constexpr size_t maxBodyBytes = 262144;
    size_t expectedBytes = 0, headerBytes = 0;
    int httpStatus = 0;
    bool headerParsed = false, invalid = false, tooLarge = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::clamp(cfg.timeoutMs, 100u, 30000u));
    char buf[4096];
    int n = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        DWORD remaining = static_cast<DWORD>(std::max<int64_t>(1, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count()));
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&remaining), sizeof(remaining));
        n = recv(socket, buf, sizeof(buf), 0);
        if (n <= 0) break;
        if (std::chrono::steady_clock::now() >= deadline) { invalid = true; break; }
        if (static_cast<size_t>(n) > maxHeaderBytes + maxBodyBytes - response.size()) { tooLarge = true; break; }
        response.append(buf, static_cast<size_t>(n));
        if (!headerParsed) {
            const auto end = response.find("\r\n\r\n");
            if (end == std::string::npos) {
                if (response.size() > maxHeaderBytes) { invalid = true; break; }
                continue;
            }
            if (end + 4 > maxHeaderBytes) { invalid = true; break; }
            headerBytes = end + 4;
            const auto statusEnd = response.find("\r\n");
            if (!parseHttpStatus(std::string_view(response.data(), statusEnd), httpStatus)) { invalid = true; break; }
            bool haveLength = false;
            for (size_t begin = statusEnd + 2; begin < end;) {
                const auto finish = response.find("\r\n", begin);
                const auto colon = response.find(':', begin);
                if (finish > end || colon == std::string::npos || colon <= begin || colon >= finish
                    || !std::all_of(response.begin() + begin, response.begin() + colon, httpTokenChar)
                    || !std::all_of(response.begin() + colon + 1, response.begin() + finish, httpFieldChar)) {
                    invalid = true; break;
                }
                auto key = response.substr(begin, colon - begin);
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return char(std::tolower(c)); });
                if (key == "transfer-encoding") { invalid = true; break; }
                if (key == "content-length") {
                    if (haveLength) { invalid = true; break; }
                    auto first = colon + 1;
                    auto last = finish;
                    while (first < last && (response[first] == ' ' || response[first] == '\t')) ++first;
                    while (last > first && (response[last - 1] == ' ' || response[last - 1] == '\t')) --last;
                    size_t length = 0;
                    const auto result = std::from_chars(response.data() + first, response.data() + last, length);
                    if (result.ec != std::errc{} || result.ptr != response.data() + last) { invalid = true; break; }
                    if (length > maxBodyBytes || length >= responseJsonBytes) { tooLarge = true; break; }
                    expectedBytes = headerBytes + length;
                    haveLength = true;
                }
                begin = finish + 2;
            }
            if (invalid || tooLarge) break;
            if (!haveLength) { invalid = true; break; }
            headerParsed = true;
        }
        if (response.size() >= expectedBytes) break;
    }
    closesocket(socket);

    if (tooLarge || invalid || !headerParsed || response.size() != expectedBytes) {
        copyResponse(tooLarge
            ? "{\"ok\":false,\"errorCode\":\"response_too_large\",\"error\":\"Response exceeds the buffer; use a paged view\"}"
            : "{\"ok\":false,\"errorCode\":\"incomplete_response\",\"error\":\"Invalid, incomplete or timed-out HTTP response\"}", responseJson, responseJsonBytes);
        return SDRTOWN_CONTROL_PROTOCOL_ERROR;
    }

    const size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        copyResponse("{\"ok\":false,\"error\":\"invalid HTTP response\"}", responseJson, responseJsonBytes);
        return SDRTOWN_CONTROL_PROTOCOL_ERROR;
    }
    const std::string bodyText = response.substr(headerEnd + 4);
    const auto parsed = nlohmann::json::parse(bodyText, nullptr, false);
    if (!parsed.is_object()) {
        copyResponse("{\"ok\":false,\"errorCode\":\"invalid_json\",\"error\":\"Invalid control response\"}", responseJson, responseJsonBytes);
        return SDRTOWN_CONTROL_PROTOCOL_ERROR;
    }
    copyResponse(bodyText, responseJson, responseJsonBytes);
    if (httpStatus < 200 || httpStatus >= 300) return SDRTOWN_CONTROL_HTTP_ERROR;
    return responseOk(bodyText) ? SDRTOWN_CONTROL_OK : SDRTOWN_CONTROL_PROTOCOL_ERROR;
}

} // namespace

SDRTOWN_CONTROL_API const char* SdrTownControl_Version(void)
{
    return "SdrTownControl/1";
}

SDRTOWN_CONTROL_API int SdrTownControl_Health(const SdrTownControlConfig* config,
                                              char* responseJson,
                                              size_t responseJsonBytes)
{
    return requestJson(config, "GET", "/v1/health", "{}", responseJson, responseJsonBytes);
}

SDRTOWN_CONTROL_API int SdrTownControl_Status(const SdrTownControlConfig* config,
                                              char* responseJson,
                                              size_t responseJsonBytes)
{
    return requestJson(config, "GET", "/v1/status", "{}", responseJson, responseJsonBytes);
}

SDRTOWN_CONTROL_API int SdrTownControl_Tune(const SdrTownControlConfig* config,
                                            const SdrTownTuneRequest* request,
                                            char* responseJson,
                                            size_t responseJsonBytes)
{
    if (!request || !std::isfinite(request->frequencyHz) || request->frequencyHz <= 0.0) {
        copyResponse("{\"ok\":false,\"error\":\"frequencyHz is required\"}", responseJson, responseJsonBytes);
        return SDRTOWN_CONTROL_BAD_ARGUMENT;
    }
    std::ostringstream body;
    body << "{\"frequencyHz\":" << std::fixed << request->frequencyHz
         << ",\"startDevice\":" << (request->startDevice ? "true" : "false");
    if (request->mode && request->mode[0]) {
        body << ",\"mode\":\"" << jsonEscape(request->mode) << "\"";
    }
    if (std::isfinite(request->bandwidthHz) && request->bandwidthHz > 0.0) {
        body << ",\"bandwidthHz\":" << request->bandwidthHz;
    }
    if (std::isfinite(request->lpfHz) && request->lpfHz > 0.0) {
        body << ",\"lpfHz\":" << request->lpfHz;
    }
    if (request->audioLpfEnabled == 0 || request->audioLpfEnabled == 1) {
        body << ",\"audioLpfEnabled\":" << (request->audioLpfEnabled == 1 ? "true" : "false");
    }
    if (std::isfinite(request->rfGainDb) && request->rfGainDb >= 0.0) {
        body << ",\"rfGainDb\":" << request->rfGainDb;
    }
    if (std::isfinite(request->squelchDb)) {
        body << ",\"squelchDb\":" << request->squelchDb;
    }
    body << ",\"p25AutoFollow\":" << (request->p25AutoFollow ? "true" : "false")
         << ",\"force\":" << (request->force ? "true" : "false") << "}";
    return requestJson(config, "POST", "/v1/tune", body.str(), responseJson, responseJsonBytes);
}

SDRTOWN_CONTROL_API int SdrTownControl_SetMode(const SdrTownControlConfig* config,
                                               const char* mode,
                                               char* responseJson,
                                               size_t responseJsonBytes)
{
    if (!mode || !mode[0]) {
        copyResponse("{\"ok\":false,\"error\":\"mode is required\"}", responseJson, responseJsonBytes);
        return SDRTOWN_CONTROL_BAD_ARGUMENT;
    }
    // Website / FUBAR mode taps are intentional — force leaves a live P25 follow.
    std::string body = std::string("{\"mode\":\"") + jsonEscape(mode) + "\",\"force\":true}";
    return requestJson(config, "POST", "/v1/mode", body, responseJson, responseJsonBytes);
}

SDRTOWN_CONTROL_API int SdrTownControl_SetRfGain(const SdrTownControlConfig* config,
                                                 double rfGainDb,
                                                 char* responseJson,
                                                 size_t responseJsonBytes)
{
    if (!std::isfinite(rfGainDb) || rfGainDb < 0.0 || rfGainDb > 120.0) {
        copyResponse("{\"ok\":false,\"error\":\"rfGainDb is out of range\"}",
                     responseJson, responseJsonBytes);
        return SDRTOWN_CONTROL_BAD_ARGUMENT;
    }
    std::ostringstream body;
    body << "{\"rfGainDb\":" << rfGainDb << "}";
    return requestJson(config, "POST", "/v1/rf-gain", body.str(), responseJson, responseJsonBytes);
}

SDRTOWN_CONTROL_API int SdrTownControl_SetVolume(const SdrTownControlConfig* config,
                                                 double volume,
                                                 char* responseJson,
                                                 size_t responseJsonBytes)
{
    if (!std::isfinite(volume) || volume < 0.0 || volume > 1.0) {
        copyResponse("{\"ok\":false,\"error\":\"volume is out of range\"}",
                     responseJson, responseJsonBytes);
        return SDRTOWN_CONTROL_BAD_ARGUMENT;
    }
    std::ostringstream body;
    body << "{\"volume\":" << volume << "}";
    return requestJson(config, "POST", "/v1/volume", body.str(), responseJson, responseJsonBytes);
}

SDRTOWN_CONTROL_API int SdrTownControl_SetDirectSampling(const SdrTownControlConfig* config,
                                                         int mode,
                                                         char* responseJson,
                                                         size_t responseJsonBytes)
{
    if (mode < 0 || mode > 2) {
        copyResponse("{\"ok\":false,\"error\":\"directSampling must be 0, 1, or 2\"}",
                     responseJson, responseJsonBytes);
        return SDRTOWN_CONTROL_BAD_ARGUMENT;
    }
    std::ostringstream body;
    body << "{\"directSampling\":" << mode << "}";
    return requestJson(config, "POST", "/v1/direct-sampling", body.str(), responseJson,
                       responseJsonBytes);
}

SDRTOWN_CONTROL_API int SdrTownControl_Request(const SdrTownControlConfig* config,
                                               const char* method,
                                               const char* path,
                                               const char* bodyJson,
                                               char* responseJson,
                                               size_t responseJsonBytes)
{
    if (!method || !path) {
        copyResponse("{\"ok\":false,\"error\":\"method and path are required\"}",
                     responseJson, responseJsonBytes);
        return SDRTOWN_CONTROL_BAD_ARGUMENT;
    }
    return requestJson(config, method, path, bodyJson ? bodyJson : "{}",
                       responseJson, responseJsonBytes);
}

SDRTOWN_CONTROL_API int SdrTownControl_StartP25Control(const SdrTownControlConfig* config,
                                                       double controlFrequencyHz,
                                                       int autoFollow,
                                                       char* responseJson,
                                                       size_t responseJsonBytes)
{
    if (!std::isfinite(controlFrequencyHz) || controlFrequencyHz <= 0.0) {
        copyResponse("{\"ok\":false,\"error\":\"controlFrequencyHz is required\"}",
                     responseJson, responseJsonBytes);
        return SDRTOWN_CONTROL_BAD_ARGUMENT;
    }
    std::ostringstream body;
    body << "{\"frequencyHz\":" << std::fixed << controlFrequencyHz
         << ",\"autoFollow\":" << (autoFollow ? "true" : "false") << "}";
    return requestJson(config, "POST", "/v1/p25/control", body.str(), responseJson, responseJsonBytes);
}
