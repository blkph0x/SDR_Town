#pragma once

#include <cstdint>
#include <string>

// Sprint 0: clear Phase-2 trunk TX identity + arm policy.
// No RF emission lives here — config and gates only.

struct P25TxConfig {
    // Explicit arm: TX path is dead until true (and canArm() passes).
    bool armed = false;

    // Subscriber / call identity
    uint32_t unitId = 0;          // RID
    uint32_t talkgroupId = 0;
    uint16_t nac = 0;
    uint32_t wacn = 0;            // 20-bit
    uint16_t systemId = 0;

    // Preferred slot when grant does not yet specify (0 or 1).
    bool tdmaSlotKnown = false;
    uint8_t tdmaSlot = 0;

    // Hardware binding: TX device index in DeviceManager (-1 = none).
    int txDeviceIndex = -1;
    // Optional separate RX/control device (-1 = same as monitor default).
    int rxDeviceIndex = -1;

    // TX RF (applied after grant; 0 = wait for grant uplink).
    double preferredUplinkHz = 0.0;
    double txGainDb = 0.0;

    // Safety
    int maxPttSeconds = 120;
    bool clearOnly = true;        // refuse encrypted grants/service options
    bool dualDevicePreferred = true;

    // Encoder presence (set by host after probing licensed encode path).
    bool ambeEncoderAvailable = false;

    // Human label for logs/UI
    std::string label;

    bool hasIdentity() const noexcept
    {
        return unitId != 0 && talkgroupId != 0 && nac != 0;
    }

    bool hasTrunkIds() const noexcept
    {
        return wacn != 0 && systemId != 0;
    }

    bool hasTxDevice() const noexcept
    {
        return txDeviceIndex >= 0;
    }

    // Hard arm gate for Sprint 0+ (speech encode not required to arm tone/test later).
    bool canArm(bool deviceCanTx) const noexcept
    {
        return hasIdentity() && hasTxDevice() && deviceCanTx && clearOnly;
    }

    // Full speech TX (Sprints 3+): also needs encoder.
    bool canArmSpeech(bool deviceCanTx) const noexcept
    {
        return canArm(deviceCanTx) && ambeEncoderAvailable;
    }
};
