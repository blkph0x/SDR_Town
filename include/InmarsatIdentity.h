#pragma once
#include <cstdint>
#include <string>
#include "InmarsatMessageStore.h"
#include <optional>
#include <span>

// DEC-0164: Classic Aero AES is ICAO (ICAO Doc 9925 III 6.3.1).
// Call only after Classic Aero framing/CRC validation, not for arbitrary EGC IDs.
inline std::string inmarsatClassicIcao(uint32_t aes) {
    if (!aes || aes >= 0xffffff) return {};
    std::string result(6, '0');
    for (int i = 5; i >= 0; --i) { result[i] = "0123456789ABCDEF"[aes & 15]; aes >>= 4; }
    return result;
}

inline std::optional<InmarsatMessage> inmarsatClassicVoiceIdentity(std::span<const uint8_t> su) {
    if(su.size()!=12 || (su[0]!=0x30 && su[0]!=0x60))return {};
    const uint32_t aes=uint32_t(su[1])<<16|uint32_t(su[2])<<8|su[3];
    const auto icao=inmarsatClassicIcao(aes);if(icao.empty())return {};
    InmarsatMessage m;m.kind=InmarsatMsgKind::Su;m.aesId=aes;m.gesId=su[4];
    m.validated=true;m.classicAeroIdentity=true;m.icaoHex=icao;
    m.label="C-IDENTITY";m.text="CRC-valid Classic Aero call identity";
    return m;
}
