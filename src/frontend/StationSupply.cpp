#include "frontend/StationSupply.h"

namespace {
ExternalSupplyIo& supplyIo() {
    static ExternalSupplyIo io;
    return io;
}

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\r' || text[begin] == '\n')) ++begin;
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\r' || text[end - 1] == '\n')) --end;
    return text.substr(begin, end - begin);
}
}

std::string externalSupplyLine(int voltageV, bool tone22kHz, std::string* error) {
    if (voltageV == 0) return "OFF\n";
    if (voltageV != 13 && voltageV != 18) {
        if (error) *error = "External supply voltage must be 13 V, 18 V, or off";
        return {};
    }
    std::string line = std::to_string(voltageV);
    if (tone22kHz) line += " TONE";
    line += '\n';
    return line;
}

bool externalSupplyReplyOk(const std::string& reply) {
    return trim(reply) == "OK";
}

void setExternalSupplyIo(ExternalSupplyIo io) {
    supplyIo() = std::move(io);
}

bool commandExternalSupply(const std::string& port, int voltageV, bool tone22kHz, std::string* error) {
    if (port.empty()) {
        if (error) *error = "External Bias-T needs a supply port";
        return false;
    }
    const std::string line = externalSupplyLine(voltageV, tone22kHz, error);
    if (line.empty()) return false;
    if (!supplyIo()) {
        if (error) *error = "No supply port is bound";
        return false;
    }
    std::string reply;
    if (!supplyIo()(port, line, &reply, error)) return false;
    if (!externalSupplyReplyOk(reply)) {
        if (error) *error = "Supply did not acknowledge";
        return false;
    }
    return true;
}
