# SDR Town 0.2.135 experimental

## Chosen radio stays chosen; HF audio is cleaner

The Listen radio is remembered by USB identity, not by whichever dongle
enumerated first. Tune and Receive stop other unused Listen streams so a
second radio does not keep overflowing after you pick one.

Below 30 MHz, an optional 200–2800 Hz speech filter reduces rumble and
hiss. It never runs on P25 voice or control audio and never edits decoder
IQ. Spectrum DC removal, detail, and the tune marker are display only.

P25 decoding and encrypted mute are unchanged.

This remains an experimental tester build.
