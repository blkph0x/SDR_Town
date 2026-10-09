#include "ListenDeviceSelection.h"
#include <stdexcept>
#include <iostream>
#include <vector>

struct Radio { std::string stableKey; };
void check(bool ok) { if (!ok) throw std::runtime_error("Listen selection regression"); }
int main() {
    const std::string rsp = "sdrplay|serial:2237050048|ch:0";
    std::vector<Radio> radios{{"rtlsdr|serial:00000001"}, {"rtlsdr|serial:00000003"}, {rsp}};
    check(resolveListenDevice(radios, rsp, 0) == 2);
    // The same choice still works after unplugging the RTLs or a reordered rescan.
    std::vector<Radio> reordered{{rsp}, {"rtlsdr|serial:00000003"}, {"rtlsdr|serial:00000001"}};
    check(resolveListenDevice(reordered, rsp, 0) == 0);
    std::vector<Radio> alone{{rsp}};
    check(resolveListenDevice(alone, rsp, 0) == 0);
    // Unplugged or duplicated chosen radio must never start an unrelated RTL.
    std::vector<Radio> missing{{"rtlsdr|serial:00000001"}};
    check(resolveListenDevice(missing, rsp, 0) == size_t(-1));
    radios.push_back({rsp});
    check(resolveListenDevice(radios, rsp, 0) == size_t(-1));
    check(resolveListenDevice(alone, "", 0) == 0);
    std::cout << "PASS: explicit RSP selection, reorder, standalone, missing, duplicate, legacy default\n";
}
