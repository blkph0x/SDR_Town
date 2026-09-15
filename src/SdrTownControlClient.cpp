#include "SdrTownControlClient.h"

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
    return body.find("\"ok\":true") != std::string::npos ||
           body.find("\"ok\": true") != std::string::npos;
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

int requestJson(const SdrTownControlConfig* config,
                const char* method,
                const char* path,
                const std::string& body,
                char* responseJson,
                size_t responseJsonBytes)
{
    if (!method || !path || path[0] != '/') return SDRTOWN_CONTROL_BAD_ARGUMENT;
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
    char buf[4096];
    int n = 0;
    while ((n = recv(socket, buf, sizeof(buf), 0)) > 0) {
        response.append(buf, static_cast<size_t>(n));
        if (response.size() > 262144) break;
    }
    closesocket(socket);

    const size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        copyResponse("{\"ok\":false,\"error\":\"invalid HTTP response\"}", responseJson, responseJsonBytes);
        return SDRTOWN_CONTROL_PROTOCOL_ERROR;
    }
    const std::string statusLine = response.substr(0, response.find("\r\n"));
    int httpStatus = 0;
    std::sscanf(statusLine.c_str(), "HTTP/%*s %d", &httpStatus);
    const std::string bodyText = response.substr(headerEnd + 4);
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
    body << ",\"p25AutoFollow\":" << (request->p25AutoFollow ? "true" : "false") << "}";
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
    std::string body = std::string("{\"mode\":\"") + jsonEscape(mode) + "\"}";
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
