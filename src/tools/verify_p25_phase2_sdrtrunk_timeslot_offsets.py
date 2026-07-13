#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
p25 = (root / 'P25LiveDecoder.cpp').read_text(errors='ignore')
assert 'bestInfo = static_cast<uint16_t>(info);' in p25, 'I-ISCH bestInfo must preserve all 9 bits'
assert 'for (size_t payloadDibit = 0; payloadDibit < 160; ++payloadDibit)' in p25, 'Mask must map to 160-dibit timeslot payload, not old +10/170 window'
assert 'const size_t payload = pos + P25LiveDecoder::Phase2FrameSyncDibits;' in p25, 'Timeslot payload must begin after 40-bit/20-dibit ISCH'
assert 'payload + kPhase2TimeslotPayloadDibits' in p25, 'Timeslot payload length must be 160 dibits'
assert 'payloadDibits[0]' in p25 and 'payloadDibits[37]' in p25 and 'payloadDibits[122]' in p25 and 'payloadDibits[159]' in p25, 'DUID dibit positions must match sdrtrunk Timeslot bits 0/74/244/318'
assert 'posInPeriod' not in p25 and 'status-stripped continuous payload' not in p25, 'Phase 2 timeslot payload must be 160 consecutive dibits after ISCH'
assert 'const std::array<size_t, 4> starts{1, 38, 86, 123};' in p25, 'Voice AMBE starts must match sdrtrunk Voice4/Voice2 bit offsets 2/76/172/246'
assert 'const size_t essStart = 74;' in p25, 'ESS must start at timeslot bit 148 / dibit 74'
assert 'const bool hardVoice = phase2BurstKindHasVoice(burst.kind);' in p25, 'AMBE voice extraction must be limited to Voice2/Voice4 bursts'
assert 'const bool softUnknownVoice' not in p25 and 'voiceLayoutKind' not in p25, 'UnknownTimeslot-equivalent bursts must not manufacture AMBE voice'
assert 'Force voice codeword extraction for masked phase2 bursts' not in p25, 'FACCH/SACCH/LCCH signaling bursts must never be manufactured as AMBE voice'
assert 'const size_t count = burst.kind == P25Phase2BurstKind::Voice2 ? 2u : 4u;' in p25, 'Voice4 must carry 4 AMBE codewords and Voice2 must carry 2'
print('P25 Phase 2 sdrtrunk timeslot offset regression: PASS')
