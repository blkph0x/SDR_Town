#pragma once
#include <string>

// DEC-0221: only the host creates credentials; companions only read. The path
// argument permits an isolated registry fixture without touching user pairing.
bool localControlPairingToken(std::string& token, bool create,
    const wchar_t* registryPath = L"Software\\SDR_Town\\ControlPairingV1");
