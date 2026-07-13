# P25 Phase 2 sdrtrunk order/direction cross-check patch

This patch corrects the Phase 2 DUID codeword table to match sdrtrunk's
`DataUnitID.getValueWithParity()` constants exactly.

sdrtrunk maps the 8-bit DUID codeword from timeslot bits:

    0,1,74,75,244,245,318,319

and uses these encoded values:

    VOICE_4            0x00
    SCRAMBLED_SACCH    0x39
    VOICE_2            0x65
    SCRAMBLED_FACCH    0x9A
    SCRAMBLED_DATCH    0xA3
    UNSCRAMBLED_SACCH  0xC6
    UNSCRAMBLED_LCCH   0xD1
    UNSCRAMBLED_FACCH  0xFF

The previous local DUID generator produced non-standard values such as 0x3A,
0x66, 0x97, 0xCB, 0xDC and 0xF1.  That made true ACCH bursts decode as the
wrong burst kind.  In field logs this can look like stable p2sf=12/p2mask=12
and inflated p2vcw counts, while MAC/ESS never progresses.

The ACCH extraction order remains sdrtrunk-compatible after the previous patch:

SACCH/LCCH input extraction in transmitted order:
    INFO_1..INFO_30, PARITY_1..PARITY_22

RS input order:
    input[6]..input[27]   = PARITY_22..PARITY_1
    input[28]..input[57]  = INFO_30..INFO_1
    input[0]..input[5] and input[58]..input[62] are erased/shortened

FACCH input extraction in transmitted order:
    INFO_1..INFO_26, PARITY_1..PARITY_19

RS input order:
    input[9]..input[27]   = PARITY_19..PARITY_1
    input[28]..input[53]  = INFO_26..INFO_1
    input[0]..input[8] and input[54]..input[62] are erased/shortened

The next remaining gap versus sdrtrunk is that this decoder's local RS helper
primarily solves known erasures/punctures and does not yet implement full
unknown-symbol correction like sdrtrunk's ReedSolomon_63_35_29_P25.  On strong
RF this DUID fix should allow MAC attempts to be correctly classified.  If logs
show p2mac nonzero but crc=0, the next fix should be full RS unknown-symbol
correction and/or detailed RS syndrome diagnostics.
