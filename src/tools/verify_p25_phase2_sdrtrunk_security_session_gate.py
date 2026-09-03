#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(errors='ignore')
recv_path = root.parent / 'include' / 'Receiver.h'
recv = recv_path.read_text(errors='ignore') if recv_path.exists() else main
p25 = (root / 'P25LiveDecoder.cpp').read_text(errors='ignore')
ctrl = (root / 'P25Control.cpp').read_text(errors='ignore')
traffic = (root / 'P25TrafficChannelProcessor.cpp').read_text(errors='ignore')
target_evidence_region = main.split(
    'for (const auto& burst : live.phase2Bursts) {', 1
)[1].split(
    '// Ensure we feed mbelib in strict chronological order', 1
)[0]
expected_region = main.split(
    '// Compute "what we need" for the selected timeslot only.', 1
)[1].split(
    'for (const auto& burst : orderedBurstsForFeed) {', 1
)[0]
feed_region = main.split(
    'for (const auto& burst : orderedBurstsForFeed) {', 1
)[1].split(
    'if (lateEntryStrongTargetReleaseDecoded', 1
)[0]
dedupe_sync_region = main.split(
    'static P25Phase2AmbeEmitDedupeState& p25Phase2SyncAmbeEmitDedupeCallContext', 1
)[1].split(
    'static bool p25Phase2ShouldEmitAmbeFrame', 1
)[0]
same_call_update_region = main.split(
    'const bool incomingSourceKnown = followTg.lastSourceId != 0;', 1
)[1].split(
    'if (activeRx->p25IndependentTrafficSource)', 1
)[0]
same_call_hop_region = main.split(
    'const bool incomingSlotKnown = followTg.tdmaSlotKnown;', 1
)[1].split(
    'if (p25TalkgroupGrantProvesSpeakerEncrypted(followTg)', 1
)[0]
follow_arm_region = main.split('auto armP25VoiceFollowState =', 1)[1].split('auto scheduleP25VoiceFollowArm', 1)[0]
worker_job_region = main.split('struct P25VoiceDecodeJob {', 1)[1].split('struct P25VoiceDecodeResult {', 1)[0]
worker_result_region = main.split('struct P25VoiceDecodeResult {', 1)[1].split('struct P25VoiceWorkerQueueSnapshot {', 1)[0]
worker_still_current_region = main.split('auto stillCurrent = [&](std::string* reason) -> bool {', 1)[1].split('bool publishResult = false;', 1)[0]
worker_publish_region = main.split('P25VoicePublishOutcome publishP25VoiceDecodeResult', 1)[1].split('if (stale) {', 1)[0]
checks = {
    'receiver owns pending Phase 2 PCM queue': 'p25Phase2PendingAudio' in recv and 'p25Phase2PendingAudioArmed' in recv,
    'unknown Phase 2 AMBE is queued not emitted': (
        'applyP25Phase2SecurityAudioGate' in main and
        'waitingForClearGrant = true' in main and
        'p25QueuePhase2PendingAmbeFrame' in main and
        'Diagnostic AMBE probes use a throwaway' in main
    ),
    'pending queue is capped to sub-second audio': (
        'kP25Phase2PendingQueueMaxFrames = 20' in main and
        'queue.ambeFrames.size() > kP25Phase2PendingQueueMaxFrames' in main and
        'queue.ambeFrames.size() * 960u' in main
    ),
    'clear call drains pending raw AMBE queue': (
        'releasePendingRawVoiceFromEss' in main and
        'releasePendingRawVoiceFromTrustedTrafficState' in main and
        'releasePendingRawVoiceFromExplicitClearTrafficProof' in main and
        'p25TakePhase2PendingAudio' in main
    ),
    'encrypted call drops pending queue': 'trustedEncrypted' in main and 'p25ClearPhase2PendingAudio(rx)' in main,
    'MAC_IDLE resets session': 'case 3: // MAC_IDLE' in p25 and 'phase2ClearCallSession(*session);' in p25,
    'MAC_HANGTIME resets session': 'case 6: // MAC_HANGTIME' in p25 and 'phase2ClearCallSession(*session);' in p25 and 'MAC_HANGTIME' in ctrl,
    'MAC_HANGTIME still promotes target encryption evidence': 'pduType != 4 && pduType != 6' in p25 and 'session->hangtimeSeen = true;' in p25 and 'session->trafficSecurityKnown = true;' in p25,
    'known mismatched TG helper exists': 'static bool p25Phase2TrafficTalkgroupKnownMismatch' in main,
    'known mismatched TG is not target evidence': (
        'trafficTalkgroupKnownMismatch' in target_evidence_region and
        'out.phase2OppositeVoiceCodewords += burst.voiceCodewords.size();' in target_evidence_region and
        'continue;' in target_evidence_region
    ),
    'known mismatched TG is not expected audio': (
        'p25Phase2TrafficTalkgroupKnownMismatch(rx, b)' in expected_region and
        'continue;' in expected_region
    ),
    'known mismatched TG hard rejects feed': (
        'p25Phase2TrafficTalkgroupKnownMismatch(rx, burst)' in feed_region and
        'out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();' in feed_region and
        'out.phase2TrafficTalkgroupStaleMismatchVoiceCodewords += burst.voiceCodewords.size();' in feed_region and
        'continue;' in feed_region
    ),
    'traffic processor rejects stale different TG': (
        'const bool burstTargetsCall = slotMatches && trafficTalkgroupBelongsToCall;' in traffic
    ),
    'same-window known other TG blocks unlabeled selected-slot audio': (
        'selectedSlotKnownOtherTalkgroupInWindow' in target_evidence_region and
        'selectedSlotBlockedByWindowTalkgroup' in target_evidence_region and
        'selectedSlotKnownOtherTalkgroupInWindow' in expected_region and
        'selectedSlotKnownOtherTalkgroupInWindow' in feed_region
    ),
    'RID change resets AMBE and dedupe context': (
        'const bool sourceChanged' in dedupe_sync_region and
        'state.sourceId != currentSource' in dedupe_sync_region and
        'sourceChanged ||' in dedupe_sync_region
    ),
    'GUI same-call updates stamp RID before new PTT': (
        'p25Phase2AdoptGrantSourceIdForCurrentCall(*activeRx, followTg.lastSourceId);' in same_call_update_region and
        same_call_update_region.find('p25Phase2AdoptGrantSourceIdForCurrentCall(*activeRx, followTg.lastSourceId);') <
        same_call_update_region.find('p25Phase2BeginNewPtt(*activeRx, nowMs);')
    ),
    'GUI same-call hops stamp RID before new PTT': (
        'p25Phase2AdoptGrantSourceIdForCurrentCall(*activeRx, followTg.lastSourceId);' in same_call_hop_region and
        same_call_hop_region.find('p25Phase2AdoptGrantSourceIdForCurrentCall(*activeRx, followTg.lastSourceId);') <
        same_call_hop_region.find('p25Phase2BeginNewPtt(*activeRx, nowMs);')
    ),
    'GUI voice worker job carries selected RID': (
        'uint32_t sourceId = 0;' in worker_job_region and
        'uint32_t sourceId = 0;' in worker_result_region and
        'job.sourceId = monP25VoiceSourceId;' in main and
        'result.sourceId = job.sourceId;' in main
    ),
    'GUI voice worker rejects stale RID before decode': (
        'job.sourceId != 0' in worker_still_current_region and
        'rx.p25VoiceSourceId != 0' in worker_still_current_region and
        'rx.p25VoiceSourceId != job.sourceId' in worker_still_current_region and
        'return fail("source-changed");' in worker_still_current_region
    ),
    'GUI voice worker rejects stale RID before publish': (
        'result.sourceId != 0' in worker_publish_region and
        'rx.p25VoiceSourceId != 0' in worker_publish_region and
        'rx.p25VoiceSourceId != result.sourceId' in worker_publish_region and
        'staleReason.empty()) staleReason = "source-changed"' in worker_publish_region
    ),
    'GUI follow-arm reset preserves grant identity tuple': (
        'const bool armedVoiceDecodeEnabled = rx.p25VoiceDecodeEnabled;' in follow_arm_region and
        'const uint32_t armedTalkgroupId = rx.p25VoiceTalkgroupId;' in follow_arm_region and
        'const uint32_t armedSourceId = rx.p25VoiceSourceId;' in follow_arm_region and
        'const bool armedPhase2 = rx.p25VoicePhase2;' in follow_arm_region and
        'const bool armedSlotKnown = rx.p25VoiceTdmaSlotKnown;' in follow_arm_region and
        'const bool armedMaskKnown = rx.p25VoiceMaskParamsKnown;' in follow_arm_region and
        'const bool armedIndependentTrafficSource = rx.p25IndependentTrafficSource;' in follow_arm_region and
        'rx.p25VoiceDecodeEnabled = armedVoiceDecodeEnabled;' in follow_arm_region and
        'rx.p25VoiceTalkgroupId = armedTalkgroupId;' in follow_arm_region and
        'rx.p25VoiceSourceId = armedSourceId;' in follow_arm_region and
        'rx.p25VoicePhase2 = armedPhase2;' in follow_arm_region and
        'rx.p25VoiceTdmaSlotKnown = armedSlotKnown;' in follow_arm_region and
        'rx.p25VoiceMaskParamsKnown = armedMaskKnown;' in follow_arm_region and
        'rx.p25IndependentTrafficSource = armedIndependentTrafficSource;' in follow_arm_region
    ),
}
missing=[name for name,ok in checks.items() if not ok]
if missing:
    raise SystemExit('P25 Phase 2 sdrtrunk security/session gate FAILED: '+', '.join(missing))
print('P25 Phase 2 sdrtrunk security/session gate: PASS')
