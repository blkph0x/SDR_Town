#include "LocalControlPairing.h"
#include <catch2/catch_test_macros.hpp>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <thread>
#include <array>

TEST_CASE("Local pairing is persistent protected bounded and fails closed", "[control-pairing]") {
    struct Fixture {
        std::wstring path = L"Software\\SDR_Town\\PairingTests\\" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(GetTickCount64());
        ~Fixture() { RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str()); }
    } fixture;
    std::string first, second;
    CHECK_FALSE(localControlPairingToken(first,false,fixture.path.c_str()));
    bool a=false,b=false;
    std::thread one([&]{a=localControlPairingToken(first,true,fixture.path.c_str());});
    std::thread two([&]{b=localControlPairingToken(second,true,fixture.path.c_str());});
    one.join(); two.join();
    REQUIRE(a); REQUIRE(b);
    REQUIRE(first.size()==64);
    CHECK(bool(first==second));
    std::string reread;
    REQUIRE(localControlPairingToken(reread,false,fixture.path.c_str()));
    CHECK(bool(first==reread));
    std::array<BYTE,4096> stored{};
    DWORD length=static_cast<DWORD>(stored.size());
    REQUIRE(RegGetValueW(HKEY_CURRENT_USER,fixture.path.c_str(),L"Secret",RRF_RT_REG_BINARY,nullptr,stored.data(),&length)==ERROR_SUCCESS);
    const std::string sealed(reinterpret_cast<char*>(stored.data()),length);
    CHECK(sealed.find(first)==std::string::npos);
    HKEY key=nullptr;
    REQUIRE(RegOpenKeyExW(HKEY_CURRENT_USER,fixture.path.c_str(),0,KEY_SET_VALUE,&key)==ERROR_SUCCESS);
    const BYTE corrupt=0;
    const auto write=RegSetValueExW(key,L"Secret",0,REG_BINARY,&corrupt,1);
    RegCloseKey(key);
    REQUIRE(write==ERROR_SUCCESS);
    CHECK_FALSE(localControlPairingToken(reread,false,fixture.path.c_str()));
    CHECK_FALSE(localControlPairingToken(reread,true,fixture.path.c_str()));
    CHECK(reread.empty());
    length=static_cast<DWORD>(stored.size());
    REQUIRE(RegGetValueW(HKEY_CURRENT_USER,fixture.path.c_str(),L"Secret",RRF_RT_REG_BINARY,nullptr,stored.data(),&length)==ERROR_SUCCESS);
    CHECK(length==1);
}
#endif
