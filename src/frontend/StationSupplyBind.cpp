#include "frontend/StationSupply.h"

#include <windows.h>

#include <string>

namespace {
std::wstring portPath(const std::string& port) {
    const std::wstring wide(port.begin(), port.end());
    if (wide.rfind(L"\\\\.\\", 0) == 0) return wide;
    return L"\\\\.\\" + wide;
}

bool serialSupply(const std::string& port, const std::string& line, std::string* reply, std::string* error) {
    const HANDLE handle = CreateFileW(portPath(port).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                      OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error) *error = "Supply port did not open";
        return false;
    }
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 500;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 500;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!SetCommTimeouts(handle, &timeouts) || !GetCommState(handle, &dcb)) {
        CloseHandle(handle);
        if (error) *error = "Supply port did not accept 9600 8N1";
        return false;
    }
    dcb.BaudRate = CBR_9600;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = 1;
    if (!SetCommState(handle, &dcb)) {
        CloseHandle(handle);
        if (error) *error = "Supply port did not accept 9600 8N1";
        return false;
    }
    DWORD written = 0;
    if (!WriteFile(handle, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) ||
        written != line.size()) {
        CloseHandle(handle);
        if (error) *error = "Supply command was not written";
        return false;
    }
    std::string got;
    char buffer[64];
    while (got.find('\n') == std::string::npos && got.size() < 64) {
        DWORD read = 0;
        if (!ReadFile(handle, buffer, sizeof(buffer), &read, nullptr)) break;
        if (read == 0) break;
        got.append(buffer, buffer + read);
    }
    CloseHandle(handle);
    if (reply) *reply = got;
    return true;
}

struct InstallSupply {
    InstallSupply() { setExternalSupplyIo(serialSupply); }
};

InstallSupply installSupply;
}
