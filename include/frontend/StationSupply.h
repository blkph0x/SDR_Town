#pragma once

#include <functional>
#include <string>

// Line protocol for an external 13/18 V supply. An OK reply is the supply's
// acknowledgement, not a measured voltage.
std::string externalSupplyLine(int voltageV, bool tone22kHz, std::string* error);
bool externalSupplyReplyOk(const std::string& reply);

// port, command line, reply, error. The application installs the serial binding.
using ExternalSupplyIo = std::function<bool(const std::string& port, const std::string& line,
                                            std::string* reply, std::string* error)>;
void setExternalSupplyIo(ExternalSupplyIo io);
bool commandExternalSupply(const std::string& port, int voltageV, bool tone22kHz, std::string* error);
