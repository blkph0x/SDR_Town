# SDR Town 0.2.134 experimental

## Cleaner audio on every speaker device, and two radios at once

Playback no longer opens as a mono WASAPI client and no longer runs empty
callbacks before any audio is queued. That was starving HyperX Virtual
Surround, VB-Audio Cable, and similar mix formats at about 100 underruns
per second, including with the radio stopped.

Two independent SDRs (for example RSPdx + RTL) no longer share one live
I/O lock. Each USB identity has its own lock, so one radio's `readStream`
does not overflow the other. The same radio still serializes tune vs read
and TX vs stop.

P25 decoding, encrypted mute, and loopback control are unchanged.

This remains an experimental tester build.
