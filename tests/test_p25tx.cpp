#include <catch2/catch_all.hpp>

#include "P25TxSession.h"

TEST_CASE("P25 TX arm requires identity and TX device", "[p25][tx]")
{
    P25TxConfig cfg;
    REQUIRE_FALSE(cfg.canArm(true));
    cfg.unitId = 0x123456;
    cfg.talkgroupId = 30302;
    cfg.nac = 0x2df;
    REQUIRE_FALSE(cfg.canArm(true));
    cfg.txDeviceIndex = 0;
    REQUIRE(cfg.canArm(true));
    REQUIRE_FALSE(cfg.canArm(false));
    REQUIRE_FALSE(cfg.canArmSpeech(true));
    cfg.ambeEncoderAvailable = true;
    REQUIRE(cfg.canArmSpeech(true));
}

TEST_CASE("P25 TX PTT ignored until armed", "[p25][tx]")
{
    P25TxSnapshot snap;
    snap.state = P25TxState::Idle;
    snap.deviceCanTx = true;
    snap.config.unitId = 1;
    snap.config.talkgroupId = 30302;
    snap.config.nac = 0x2df;
    snap.config.txDeviceIndex = 0;

    auto ignored = evaluateP25Tx(snap, P25TxEvent::PttPress);
    REQUIRE(ignored.nextState == P25TxState::Idle);
    REQUIRE_FALSE(ignored.changed);

    auto armed = evaluateP25Tx(snap, P25TxEvent::Arm);
    REQUIRE(armed.nextState == P25TxState::Armed);
    REQUIRE(armed.changed);
}

TEST_CASE("P25 TX PTT press moves to request/wait grant", "[p25][tx]")
{
    P25TxSnapshot snap;
    snap.state = P25TxState::Armed;
    snap.deviceCanTx = true;
    snap.config.unitId = 1;
    snap.config.talkgroupId = 30302;
    snap.config.nac = 0x2df;
    snap.config.txDeviceIndex = 0;
    snap.nowMs = 1000;
    snap.stateEnteredMs = 1000;

    auto press = evaluateP25Tx(snap, P25TxEvent::PttPress);
    REQUIRE(press.nextState == P25TxState::Requesting);
    REQUIRE(press.emitChannelRequest);

    snap.state = P25TxState::Requesting;
    auto wait = evaluateP25Tx(snap, P25TxEvent::None);
    REQUIRE(wait.nextState == P25TxState::WaitGrant);
}

TEST_CASE("P25 TX encrypted grant is refused", "[p25][tx]")
{
    P25TxSnapshot snap;
    snap.state = P25TxState::WaitGrant;
    snap.deviceCanTx = true;
    snap.config.unitId = 1;
    snap.config.talkgroupId = 30302;
    snap.config.nac = 0x2df;
    snap.config.txDeviceIndex = 0;
    snap.config.clearOnly = true;
    snap.grantValid = true;
    snap.grantEncrypted = true;
    snap.grantTalkgroupId = 30302;

    auto denied = evaluateP25Tx(snap, P25TxEvent::EncryptedGrant);
    REQUIRE(denied.nextState == P25TxState::Error);
    REQUIRE(denied.stopVoiceTx);
}

TEST_CASE("P25 TX clear grant proceeds to voice stub", "[p25][tx]")
{
    P25TxSnapshot snap;
    snap.state = P25TxState::WaitGrant;
    snap.deviceCanTx = true;
    snap.config.unitId = 1;
    snap.config.talkgroupId = 30302;
    snap.config.nac = 0x2df;
    snap.config.txDeviceIndex = 0;
    snap.grantValid = true;
    snap.grantEncrypted = false;
    snap.grantTalkgroupId = 30302;
    snap.grantUplinkHz = 418.875e6;

    auto grant = evaluateP25Tx(snap, P25TxEvent::GrantReceived);
    REQUIRE(grant.nextState == P25TxState::TuningUplink);
    REQUIRE(grant.tuneUplink);

    snap.state = P25TxState::TuningUplink;
    auto tuned = evaluateP25Tx(snap, P25TxEvent::TuneComplete);
    REQUIRE(tuned.nextState == P25TxState::VoiceActive);
    REQUIRE(tuned.startVoiceTx);

    snap.state = P25TxState::VoiceActive;
    auto release = evaluateP25Tx(snap, P25TxEvent::PttRelease);
    REQUIRE(release.nextState == P25TxState::Hang);
    REQUIRE(release.stopVoiceTx);

    snap.state = P25TxState::Hang;
    snap.stateEnteredMs = 5000;
    snap.nowMs = 5600;
    snap.hangMs = 500;
    auto done = evaluateP25Tx(snap, P25TxEvent::None);
    REQUIRE(done.nextState == P25TxState::Armed);
    REQUIRE(done.returnToControl);
}

TEST_CASE("P25 TX disarm aborts voice", "[p25][tx]")
{
    P25TxSnapshot snap;
    snap.state = P25TxState::VoiceActive;
    auto d = evaluateP25Tx(snap, P25TxEvent::Disarm);
    REQUIRE(d.nextState == P25TxState::Idle);
    REQUIRE(d.stopVoiceTx);
}
