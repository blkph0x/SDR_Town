#include "LocalControlPairing.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <sddl.h>
#include <array>
#include <vector>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")

namespace {
struct Handle { HANDLE h = nullptr; ~Handle() { if(h) CloseHandle(h); } };
struct Key { HKEY h = nullptr; ~Key() { if(h) RegCloseKey(h); } };
struct Blob {
    DATA_BLOB b{};
    ~Blob() { if(b.pbData) { SecureZeroMemory(b.pbData,b.cbData); LocalFree(b.pbData); } }
};
struct PairingLock {
    Handle handle;
    bool held = false;
    ~PairingLock() { if(held) ReleaseMutex(handle.h); }
    bool acquire() {
        Handle process;
        if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&process.h)) return false;
        DWORD bytes = 0;
        GetTokenInformation(process.h,TokenUser,nullptr,0,&bytes);
        if(bytes == 0 || bytes > 65536) return false;
        std::vector<BYTE> data(bytes);
        if(!GetTokenInformation(process.h,TokenUser,data.data(),bytes,&bytes)) return false;
        LPWSTR sid = nullptr;
        if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&sid)) return false;
        const std::wstring name = std::wstring(L"Global\\SDRTownControlPairingV1-") + sid;
        LocalFree(sid);
        handle.h = CreateMutexW(nullptr,FALSE,name.c_str());
        if(!handle.h) return false;
        // Bounded startup coordination only; never a radio/DSP timing gate.
        const auto result = WaitForSingleObject(handle.h,2500);
        held = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        return held;
    }
};
}
#endif

bool localControlPairingToken(std::string& token, bool create, const wchar_t* registryPath)
{
    token.clear();
#ifdef _WIN32
    PairingLock lock;
    if(create && !lock.acquire()) return false;
    Key key;
    const auto opened = create
        ? RegCreateKeyExW(HKEY_CURRENT_USER,registryPath,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&key.h,nullptr)
        : RegOpenKeyExW(HKEY_CURRENT_USER,registryPath,0,KEY_QUERY_VALUE,&key.h);
    if(opened != ERROR_SUCCESS) return false;
    std::array<BYTE,4096> sealed{};
    DWORD size = static_cast<DWORD>(sealed.size());
    const auto read = RegGetValueW(key.h,nullptr,L"Secret",RRF_RT_REG_BINARY,nullptr,sealed.data(),&size);
    if(read == ERROR_SUCCESS) {
        DATA_BLOB input{size,sealed.data()};
        Blob plain;
        if(!CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&plain.b) ||
           plain.b.cbData != 32) return false;
        static constexpr char hex[] = "0123456789abcdef";
        for(DWORD i=0;i<plain.b.cbData;++i) {
            token += hex[plain.b.pbData[i] >> 4]; token += hex[plain.b.pbData[i] & 15];
        }
        return true;
    }
    // Corruption, access denied or oversized blobs fail closed, never rotate
    // behind a live server. Only a genuinely missing value may be provisioned.
    if(!create || read != ERROR_FILE_NOT_FOUND) return false;
    std::array<BYTE,32> random{};
    if(BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        return false;
    DATA_BLOB input{static_cast<DWORD>(random.size()),random.data()};
    Blob encrypted;
    const bool protectedOk = CryptProtectData(&input,L"SDR Town local control",nullptr,nullptr,nullptr,
                                               CRYPTPROTECT_UI_FORBIDDEN,&encrypted.b) != FALSE;
    SecureZeroMemory(random.data(),random.size());
    if(!protectedOk || RegSetValueExW(key.h,L"Secret",0,REG_BINARY,encrypted.b.pbData,encrypted.b.cbData) != ERROR_SUCCESS)
        return false;
    return localControlPairingToken(token,false,registryPath);
#else
    (void)create; (void)registryPath;
    return false;
#endif
}
