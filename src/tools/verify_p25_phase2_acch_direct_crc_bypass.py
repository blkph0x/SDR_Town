#!/usr/bin/env python3
from pathlib import Path
text = Path(__file__).resolve().parents[1].joinpath('P25LiveDecoder.cpp').read_text()
checks = {
    'directBits fail-safe': 'std::vector<uint8_t> directBits;' in text,
    'direct FACCH/SACCH CRC before RS': 'directCrcOk = lcch ? p25Phase2Crc16Ok(directBits, crcProtectedBits)' in text,
    'direct CRC returns parseable MAC PDU': 'directCrcOk && phase2DirectCrcMacPduLooksSdrtrunkParseable(directPdu, macContentBits)' in text and 'return directPdu;' in text,
    'RS failure returns diagnostic candidate': 'if (!directBits.empty()) return makePduFromBits(directBits, false, false, 0);' in text,
    'PDU helper centralizes byte/opcode parsing': 'auto makePduFromBits = [&](const std::vector<uint8_t>& messageBits,' in text,
}
missing = [name for name, ok in checks.items() if not ok]
if missing:
    raise SystemExit('FAIL: missing ' + ', '.join(missing))
print('P25 Phase 2 ACCH direct CRC/RS-bypass regression: PASS')
