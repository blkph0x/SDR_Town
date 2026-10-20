# SDR Town 0.2.141 - Smart Satcom Channels (Experimental)

## Operator Changes

- Smart scan covers a range in overlapping captured bands and alternates
  full-range discovery with saved-channel revisits.
- Channels are remembered with label, High/Medium/Low priority and Watch
  checkbox. Edits apply live and persist; equal priority retains the current
  conversation. A higher active priority can interrupt it.
- Automatic squelch compares mean channel-bin power with local median noise.
  Quiet hold defaults to600ms; when all watched activity ends, scanning resumes.
- Up to128 remembered channels and eight current-passband analog demodulators.
  Only foreground PCM is played; no delayed background replay or stereo mixing.
- Detector-only DC mean removal and off-centre revisits avoid locking onto the
  tuner centre spike. Retune acknowledgment and current-tuning IQ are required.
- Smart audio has an independent queue, default playback device and volume.
  Existing P25/Listen audio is not trimmed or cleared.
- Logs and API status expose tuning/activity/switch events, IQ discontinuities,
  processed samples, elapsed processing and slow-block counts. Stale input stops
  reception after3s with an error.

## Boundaries

RF activity is not proven voice. Continuous interference can hold a channel;
Skip suppresses it for the sweep, and Watch off is a persistent exclusion.
Remove forgets it and allows discovery again. Saved frequencies outside the
current capture cannot be watched concurrently on one SDR.

No MUOS WCDMA, DAMA, encryption, TX, P25 DSP or shared audio-engine changes.
Satellite passes, APT/APRS modes and the legacy sequential scan remain separate.
The existing foreground NFM AX.25 decoder tap is preserved. Named Satcom
sessions can use independent radios under the existing ownership contracts.

## Evidence

DEC-0226, T-0146 and BUILD_NOTES record the Release build, pure detector and
two-carrier demodulation tests, actual mock-Soapy worker transitions, UI/API
validation and public-package checks. The worker fixture found and fixed two
pre-publication defects: initial-range DC coverage and peak-bin noise holding
squelch open. No physical satellite reception or intelligibility is claimed
from simulated IQ. Hardware acceptance remains a tester task.

ISS-0124 separately preserves an intermittent rebuilt-Qt startup failure from
the preceding source CI run; no unproven app repair or weakened gate is claimed.
Source commit3fddc2556d2478cd837978cd6788ad2844c5bb70 passes both exact-commit
[source Actions39380022721](https://github.com/blkph0x/SDR_Town/actions/runs/39380022721)
and [release Actions39380022533](https://github.com/blkph0x/SDR_Town/actions/runs/39380022533),
including rebuilt-Qt checks. All24 local CTest suites pass. Anonymous public
downloads pass hashes, embedded provenance, source/runtime inventory, control
DLL parity and the signed experimental-channel update manifest. The downloaded
application passes CLI startup, four GUI workspace checks and independent
Inmarsat/Satcom/Aircraft session/API tests without hardware RX.

Public installer, portable ZIP and control DLL are available with checksums
and signed updater files. This release is marked Latest while retaining the
experimental updater channel and label. No claim of live satellite reception
or resolution of the earlier intermittent startup failure is made.
