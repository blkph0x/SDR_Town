# SDR Town 0.2.130 experimental

## Encrypted P25 grant IQ capture

A Tools menu item and P25 panel button, **Record Enc Grant IQ**, start a live
SigMF capture, wait for a known-encrypted talkgroup grant, follow that traffic
channel with the speaker muted, and keep the tuner there until teardown,
carrier drop, or you turn the control off. The capture folder includes the
usual P25 log plus `encrypted_grant_timing.txt`.

Encrypted audio is not decoded or saved as WAV. Normal auto-follow still skips
encrypted grants and still returns immediately when a voice channel proves
encrypted.

This remains an experimental tester build.
