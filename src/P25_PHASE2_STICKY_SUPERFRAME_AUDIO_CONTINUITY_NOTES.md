# P25 Phase 2 sticky superframe continuity patch

Field log `Pasted text(85).txt` showed the important progression:

- Fast arm and centered voice tuning are working (`armDelay=0ms`, target equals granted RF).
- AMBE is now accepted at least once (`decoded=5 audio=4800 ambe=6/5`).
- However, most live windows still show isolated Phase 2 bursts (`p2bursts=1 p2vcw=4`) with `targetVcw=0 ambe=0/0` because they are not part of a fresh 12-burst superframe lock.

sdrtrunk's traffic channel decoder is continuous, so once it has superframe timing and scrambling phase it can keep mapping later isolated traffic bursts to the correct superframe burst index and XOR mask segment. The scanner-follow rolling window path was requiring the current window to rebuild the 12-burst lock each time.

This patch adds a retained Phase 2 superframe anchor:

- When a trusted 12-burst lock promotes the XOR mask phase, store the absolute stream dibit for burst 0.
- Later isolated sync hits are mapped back to the retained superframe epoch if they fall near the expected 180-dibit burst cadence.
- Those sticky bursts are decoded with the retained mask phase and correct logical TS0/TS1 assignment, allowing target-slot AMBE to be attempted even when the current window only contains one burst.

This should turn recurring `p2bursts=1 p2vcw=4 targetVcw=0 ambe=0/0` windows after acquisition into target-slot VCW/AMBE attempts instead of waiting for another full 12/12 window.
