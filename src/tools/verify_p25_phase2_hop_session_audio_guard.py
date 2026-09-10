from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
# DEC-0040: search all orchestration TUs
MAIN_TEXT = orchestration_source_text()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"FAIL: {message}")


text = MAIN_TEXT

reset_fn = text.split("bool tryApplyP25VoiceResetLocked", 1)[1].split(
    "void syncP25Phase2MaskParametersToLiveDecoder", 1
)[0]
require(
    "const uint64_t pttGeneration = rx.p25PttGeneration;" in reset_fn,
    "tryApplyP25VoiceResetLocked must save p25PttGeneration before resetP25VoiceState",
)
require(
    "rx.p25PttGeneration = pttGeneration;" in reset_fn,
    "tryApplyP25VoiceResetLocked must restore p25PttGeneration after resetP25VoiceState",
)
require(
    "const bool grantedSlotImmutable = rx.p25Phase2GrantedSlotImmutable;" in reset_fn,
    "tryApplyP25VoiceResetLocked must save granted-slot immutability",
)
require(
    "rx.p25Phase2GrantedSlotImmutable = grantedSlotImmutable;" in reset_fn,
    "tryApplyP25VoiceResetLocked must restore granted-slot immutability",
)

nonblocking_reset_fn = text.split("bool tryResetP25TrafficSessionNonBlocking", 1)[1].split(
    "static P25Phase2AmbeEmitDedupeState& p25Phase2SyncAmbeEmitDedupeCallContext", 1
)[0]
for token, label in [
    ("const int64_t grantEpochMs = rx.p25VoiceGrantEpochMs;", "grant epoch"),
    ("const uint64_t currentCallSessionId = rx.p25CurrentCallSessionId;", "call session"),
    ("const uint64_t pttGeneration = rx.p25PttGeneration;", "PTT generation"),
    ("const bool grantedSlotImmutable = rx.p25Phase2GrantedSlotImmutable;", "granted-slot immutability"),
    ("const bool targetOffsetKnown = rx.p25Phase2TrafficTargetOffsetKnown;", "traffic target-offset known flag"),
    ("const double targetOffsetHz = rx.p25Phase2TrafficTargetOffsetHz;", "traffic target-offset Hz"),
]:
    require(token in nonblocking_reset_fn, f"nonblocking traffic reset must save {label}")
for token, label in [
    ("rx.p25VoiceGrantEpochMs = grantEpochMs;", "grant epoch"),
    ("rx.p25CurrentCallSessionId = currentCallSessionId;", "call session"),
    ("rx.p25PttGeneration = pttGeneration;", "PTT generation"),
    ("rx.p25Phase2GrantedSlotImmutable = grantedSlotImmutable;", "granted-slot immutability"),
    ("rx.p25Phase2TrafficTargetOffsetKnown = targetOffsetKnown;", "traffic target-offset known flag"),
    ("rx.p25Phase2TrafficTargetOffsetHz = targetOffsetHz;", "traffic target-offset Hz"),
]:
    require(token in nonblocking_reset_fn, f"nonblocking traffic reset must restore {label}")
require(
    "rx.p25VoiceLiveDecoder = P25LiveDecoder(p25VoiceDecoderConfigForReceiver(rx));" in nonblocking_reset_fn,
    "nonblocking traffic reset must rebuild the live decoder after restoring follow metadata",
)

return_block = text.split("auto returnP25AutoFollowToControl", 1)[1].split(
    "auto armP25VoiceFollowState", 1
)[0]
require(
    "const qint64 lastSpeakerBeforeReturnMs" in return_block
    and "const bool recentSpeakerBeforeReturn" in return_block,
    "return-to-control must snapshot recent selected-slot speaker activity before clearing follow state",
)
require(
    "std::isfinite(releasedVoiceHz) &&\n                            recentSpeakerBeforeReturn" in return_block,
    "one-RTL warm standby must be gated by recent selected-slot speaker audio",
)
require(
    "P25 warm standby skipped: TG traffic had no recent selected-slot speaker audio" in return_block,
    "no-audio watchdog returns must log immediate control retune instead of silent warm standby",
)

same_call_block = text.split("if (p25FollowAutoActive && p25FollowTalkgroupId == followTg.talkgroupId)", 1)[1].split(
    "if (p25FollowAutoActive) {", 1
)[0]
require(
    "bool resetTrafficCarrierAfterMetadata = false;" in same_call_block,
    "same-call metadata promotion must defer traffic-carrier reset until after metadata is applied",
)
require(
    "if (trafficCarrierChanged ||" in same_call_block
    and "incomingSourceStartsNewPtt ||" in same_call_block
    and "!commitSameCallMetadataInPlace ||" in same_call_block,
    "same-call traffic-carrier and real source-boundary changes must create a new PTT/session boundary",
)
require(
    "sourceChangeIsControlMetadataOnly" in same_call_block
    and "heldSourceMetadata" in same_call_block,
    "same-allocation control RID changes must be held soft while traffic PTT/ESS owns call boundaries",
)
require(
    "resetTrafficCarrierAfterMetadata = true;" in same_call_block,
    "same-call traffic-carrier change must request a post-metadata reset",
)
require(
    same_call_block.find("if (commitSameCallMetadataInPlace && followTg.tdmaSlotKnown)")
    < same_call_block.find("if (resetTrafficCarrierAfterMetadata)"),
    "same-call in-source carrier reset must run after slot metadata is applied",
)
require(
    same_call_block.find("if (commitSameCallMetadataInPlace && followTg.p25MaskParamsKnown)")
    < same_call_block.find("if (resetTrafficCarrierAfterMetadata)"),
    "same-call in-source carrier reset must run after mask metadata is applied",
)

hop_block = text.split("activeRx->p25TrafficSourceCenterFreqHz = hopCenterHz;", 1)[1].split(
    "if (!keepVerifiedOffset && inheritedControlTargetOffsetKnown)", 1
)[0]
require(
    "const bool incomingSlotKnown = followTg.tdmaSlotKnown;" in hop_block,
    "same-call MHz hop must compute incoming slot before traffic reset",
)
require(
    hop_block.find("const bool incomingSlotKnown = followTg.tdmaSlotKnown;")
    < hop_block.find("tryApplyP25VoiceResetLocked(*activeRx);"),
    "same-call MHz hop must anchor slot before tryApplyP25VoiceResetLocked",
)
require(
    "const bool missingCallSession" in hop_block
    and "activeRx->p25CurrentCallSessionId == 0" in hop_block
    and "activeRx->p25PttGeneration == 0" in hop_block,
    "same-call MHz hop must detect missing call session or PTT generation",
)
require(
    "p25Phase2BeginNewPtt(*activeRx, nowMs);" in hop_block
    and "p25Phase2RefreshGrantEpoch(*activeRx, nowMs);" in hop_block,
    "same-call MHz hop must create a new PTT on hard audio boundaries and only refresh otherwise",
)
require(
    hop_block.find("p25Phase2MarkGrantedSlotImmutable(*activeRx);")
    > hop_block.find("tryApplyP25VoiceResetLocked(*activeRx);"),
    "same-call MHz hop must restore granted-slot immutability after reset",
)

publish_block = text.split("const bool phase2SpeakerSessionReady", 1)[1].split(
    "// Never clear accepted speaker PCM on transient gate failure.", 1
)[0]
require(
    "result.callSessionId != 0" in publish_block
    and "rx.p25CurrentCallSessionId != 0" in publish_block
    and "result.callSessionId == rx.p25CurrentCallSessionId" in publish_block
    and "rx.p25PttGeneration != 0" in publish_block
    and "rx.p25VoiceGrantEpochMs > 0" in publish_block,
    "Phase 2 audio publish guard must require a valid current call session",
)
require(
    "P25 audio blocked: TG=%1 missing or stale Phase 2 call session" in text,
    "sessionless Phase 2 audio must log an explicit blocked-audio diagnostic",
)
require(
    "if (phase2SpeakerSessionReady && carrierOk && gateEmit &&" in text,
    "speaker push must be gated by phase2SpeakerSessionReady",
)

print("PASS: Phase 2 hop/session/audio guard verified")
