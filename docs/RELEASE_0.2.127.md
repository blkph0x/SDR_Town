# SDR Town 0.2.127 experimental

## Fix: revert regressing P25 realtime catch-up (DEC-0200)

v0.2.126 moved spectrum FFT off the Soapy read loop (good: captures became
gapless) but also activated a 360/160 ms active-clear catch-up profile. Live
capture `20261006_075758_372` then showed:

- empty live speaker WAV
- mass `Phase 2 AMBE rejected` / `ambe=N/0`
- scheduler stuck on 160 ms fresh hops after the first burst eye

That profile is removed. Active-clear backlog catch-up returns to the proven
240/160/280 geometry. The spectrum worker remains.

## Verification

Local gates and GitHub Actions release qualification. Live multi-minute re-prove
on the same CC is required before calling continuous clear audio restored.
