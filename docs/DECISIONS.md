# Decisions

Format: ID, date, status, evidence, decision, consequences.

## DEC-0108 - Leftover SSTV families from handbook/QSSTV (2026-09-20)

Evidence: SSTV Handbook ch.4 Martin M3/M4, Scottie S3/S4, Wraase SC-1 24/48/48Q/96
scan tables; QSSTV `sstvparam.cpp` FAX480 (VIS 0, 512x500, 133.633 s) and MP/MR/ML
16-bit VIS (`0xNN23`) plus `modePD`/`modeRobot2` scan split. Auto line-sync first-match
would confuse FAX480 (267 ms) with Scottie 2 (278 ms).

Decision: add those layouts to the vendored crate. Auto: 7-bit VIS, then 16-bit VIS,
then closest 1200 Hz line-sync period (not first 15% match). FAX480 has no VIS.
MR175 omitted (QSSTV VIS 0x4A23 collides with MR140). Narrow 2172 Hz modes omitted
(demod is 1500–2300 Hz). FUBAR uses the same mode list and `/v1/sstv/live|finish|cancel`.

## DEC-0107 - Extra SSTV modes from QSSTV/handbook (2026-09-20)

Evidence: ON4QZ QSSTV `sstvparam.cpp` VIS/geometry; SSTV Handbook ch.4 SC-2 and AVT
line times. Dayton crate had no Robot B&W, SC2-30/60/120, or AVT.

Decision: vendor the MIT crate and add those layouts. Auto: VIS then line-sync
period. AVT has no 1200 Hz line sync (handbook); VIS or forced start-of-image.
Robot 24 is QSSTV 160x120 luminance, not a guessed YC table. Do not add FAX480
or narrow MP/MR modes in this drop.

## DEC-0106 - Unlock pinned SSTV Dayton modes (2026-09-20)

Evidence: unexcellent/sstv 16bf34aa implements 18 modes with VIS auto-detect
and per-line 1200 Hz sync re-align (slant lock). The helper previously rejected
everything except Robot36/Martin1 and killed jobs at 120 s (too short for PD).

Decision: expose those 18 modes in helper/CLI/GUI. Auto = VIS. Forced mode uses
the crate's first-line sync lock. 1–2.5 kHz pre-filter on recorded PCM.
Timeout/budget 480 s. AVT / Robot B&W / SC2-30/60/120 stay unsupported (not in
the crate). Do not invent a second decoder.

## DEC-0105 - Inmarsat voice follow stays disabled (2026-09-20)

Evidence: slicer has no unique-word; C-assign is fixture regex; AMBE pack is not
Aero 8400. Retuning on `voiceFollow` would hop L-band without a proven grant.

Decision: `voiceFollow`/`recordVoice` default false, ignored on load/API.
`applyVoiceFollow` does not retune. Status `locked` is always false.
Remaining gaps stay in `docs/INMARSAT.md`. Do not invent FEC/UW to close them.

## DEC-0104 - FUBAR must not receive or set home lat/lon (2026-09-20)

Evidence: FUBAR is a public website. Visitors with Take control could read/write
`/v1/satcom/observer` and aircraft `centerLat/centerLon`, which is the operator
home used for ISS/Doppler. That is not a useful remote control and leaks location.

Decision: Home observer stays in SDR Town GUI/CLI only. Town `publicStatusJson()`
and aircraft HTTP status omit coordinates. FUBAR removes lat/lon UI and the
satcom-observer proxy. Aircraft map fits aircraft tracks, not home. Satcom Start
from Take-control may send `force=true` (radio control, not location).

## DEC-0103 - Tuner lease, observer map, Doppler ECEF, CLI (2026-09-20)

Evidence: 0.2.67–0.2.74 review then tag v0.2.74 at 4f26f2e. Satcom/Inmarsat `setCenterFreq` stole live WFM/P25;
Dual Tuner Soapy make/setup raced `readStream`; diversity did not retarget listen;
CPR used signed `fmod`; TLE used GUI-thread `QEventLoop`; Inmarsat claimed FEC/AMBE
without unique-word. SGP4 look angles treated TEME as ECEF (range-rate missing Earth rotation).

Decision: DeviceManager lease (Listen/P25 > Satcom/Inmarsat/Aircraft; `force=true` to steal).
Doppler via `lookAnglesTeme` (GMST TEME→ECEF + ω×r). Home lat/lon from clickable OSM map
(and `observer set`) is the observer for ISS SSTV, passes, and Doppler. TLE over WinINet
off the GUI thread; `tle load` for fixtures. CLI covers observer/tle/satcom/inmarsat/aircraft.
Inmarsat remains an experimental prototype (no unique-word/FEC claim). P25 DSP untouched.

## DEC-0102 - RadioReference site CSV aliases (2026-09-18)

Evidence: fubarzi/TG-SITES `trs_sites_*.csv` uses RFSS, Site Dec/Hex, Description,
County Name, then ragged frequency columns. SDRTrunk treats site labels as
presentation identifiers attached to a system, separate from decode policy.
Extend the existing system-scoped alias list with optional `sites` entries keyed
by RFSS+site ID (site 0..65535 for RadioReference directories; over-the-air
matching still uses the decoder's reported site). Import via the same CSV action
with header detection; require an explicit destination system. Preserve
talkgroups on site reimport and sites on talkgroup reimport; manual overrides
survive. Ignore NAC/lat/lon/frequencies for RF decisions. Resolve names only in
control-log `site=` text and talkgroup Alpha Tag tooltips when system metadata
is known. Same 1MiB/10000 combined budget and atomic save rules as DEC-0100/0101.

## DEC-0101 - RadioReference-compatible CSV imports (2026-09-18)

Reference: https://trunkrecorder.com/docs/CONFIGURE#talkgroupsFile documents
direct RadioReference CSV headers Decimal, Hex, Alpha Tag, Mode, Description,
Tag, Category. Use Decimal plus Alpha Tag (Description fallback), Category
(Tag fallback) by header, not column position. These files lack WACN/System ID;
require an explicitly selected or newly created destination system. Never infer
it from TGID, filename or receiving frequency. Imported Mode/Priority/NAC must
not change any receive/security policy. Preserve existing manual overrides.
Qt has no CSV parser in the present dependency set: implement a small bounded
RFC4180 state machine with quoted commas, escaped quotes, embedded newlines,
CRLF/LF, UTF-8 BOM and explicit UTF-16 BOM. Reject malformed rows/quotes,
invalid encodings, duplicate IDs/headers and conflicting optional Hex IDs.
No silent skipping. Same 1MiB/10000-entry budget as JSON. Blank rows ignored;
blank headers, empty datasets and conventional frequency exports rejected.
Expose a distinct Import CSV action alongside JSON; destination dialog shows
system hex IDs and final list preview remains staged until Save. Tests use the
published column schema, adversarial CSV and real Qt destination workflow.

## DEC-0100 - System-scoped P25 alias lists (2026-09-18)

Evidence: SDRTrunk Playlist-Editor wiki (accessed2026-09-18), Aliases and
Channel General sections: named alias lists label identifiers independently
of decoding, attached to channels. Local P25TalkgroupEntry already stores
WACN/System ID and manual alphaTag. Implement independent presentation-only
lists keyed by WACN+System ID; never infer a system from TGID or frequency.
Unknown system identity gets no imported label. Existing alphaTag takes priority.
JSON version1 import/export uses Qt's structured parser; no new dependency or
claim of SDRTrunk XML/RadioReference API compatibility. One list per system,
bounded to1MiB/10000 aliases per document, with strict integer ranges, duplicate
rejection, bounded UTF-8 text and explicit source/date. Review before saving.
Editable names/groups become protected manual overrides on subsequent imports.
All imported metadata stays outside scanner/encryption/priority/voice decisions.
Persist atomically via QSaveFile; reject external changes while editing rather
than overwrite. Never erase an unreadable database. Tests cover malformed input,
system isolation, manual precedence, merge, roundtrip, storage and GUI workflow.

## DEC-0099 - Experimental live NFM SSTV session and window (2026-09-18)

Attach one bounded SSTV queue to the main receiver's existing raw NFM tap.
SstvReceiverFeed serializes attach/detach against producer publication with a
short mutex; producer only try-locks and records contention as a discontinuity.
No queue allocation until attach. Detach quiesces publication before stop.
Receiver ownership is captured on GUI start, not accessed through MainWindow
from a worker during teardown. Never change frequency, mode, bandwidth, audio
LPF or squelch to start SSTV; reject inactive/non-NFM/P25 monitor receivers.
Mode/source loss and retunes fail closed through existing metadata/gap checks.
No HF SSB SSTV claim: that requires a separate clean linear-demod audio tap.
Extend recorded window with source selection and Live NFM option. Finish
quiesces publication, drains the existing queue, closes input normally and saves
validated complete/partial images; Cancel and
close invalidate provisional output and join the worker. Live session capped
at six minutes (existing resource budget), no automatic hidden reattach.
Latest-only previews and existing new-directory PNG publication are retained.
Recorded and live controls remain mutually exclusive. Gate: attachment and
teardown tests, actual streamed GUI recording parity, interruption/finish,
small/large screenshots and existing regression suite. An off-air SSTV image
still needs a known transmission; report that acceptance gap explicitly.

## DEC-0098 - Bounded single-stream SSTV worker (2026-09-18)

Compose DEC-0095 events, DEC-0097 conversion and DEC-0096 helper on a single
non-GUI worker thread. Source callback returns data, idle or explicit EOF;
it must not block. Validate source/generation/epoch/rate/frequency/positions
again at the boundary. Initial gap markers are permitted before any samples;
a gap during a stream aborts the provisional image, never joins two sources.
Future live controller may start a new worker after that abort; no hidden retry.
Use a private QTemporaryDir, --stdin --progress and the existing bounded row
parser. Accept results only after EOF, normal zero exit, final row validation
and exact equality with helper RGB files. No output publication in this layer.
Resource budgets: 360 seconds input and 420 seconds wall per session, four
images (existing helper), 128 KiB queued stdin, 64 KiB stderr, 4 MiB stdout.
Poll waits <=5 ms; launch deadline 5 seconds, blocked-pipe deadline 5 seconds,
EOF completion deadline 10 seconds. These are worker resource/failure limits,
not demodulation or synchronization constants. Owner drains stderr/stdout on
each iteration; destructor guard kills/reaps on exceptions/cancellation.
Gate: independent recordings through actual queue/converter/helper match the
direct converted recording, progressive callbacks occur on worker, idle cancel,
explicit gaps/identity faults reject, and startup/EOF errors leave no child.
No GUI or RF wiring until this combined path passes. P25 untouched.

## DEC-0097 - SSTV streaming rate conversion (2026-09-18)

Use the bundled miniaudio linear resampler with its default fourth-order
anti-alias filter; preserve its state across blocks. Source: miniaudio.h
Resampling section and ma_linear_resampler_set_rate_internal, which reduces
integer rates by GCF and derives normalized filter coefficients from them.
Avoid set_rate_ratio's millionth-ratio truncation. Express input Hz rounded
to 0.0001 Hz and 48000 output Hz in the same scaled units (10000); maximum
960000000 fits uint32 and fractional accumulator sums remain below uint32.
At minimum 8000 Hz, 0.00005 Hz rounding error over 360 seconds is at most
0.108 output sample at 48 kHz. 48 kHz is a backend-supported worker format,
not a changed RF setting. Exact 48 kHz input bypasses conversion unchanged.
No AGC, speaker filters, time-based flushing, synthetic tails or padding.
Only a non-real-time worker owns the converter. Invalid input invalidates it;
an explicit start(rate) is required after discontinuity. Input blocks retain
the queue's 8192-sample bound; output allocations are bounded by conversion
ratio, not session length. Gate: chunk-invariant samples, bounded count drift,
tone fidelity, restart/error isolation, and independent image decode checks.
This does not qualify live RF reception or arbitrary weak-signal sensitivity.
Independent image gate: compare full images against the same upstream pictures
before/after conversion; allow at most +1 mean absolute 8-bit RGB level versus
native-rate decoding (engineering regression budget, not a protocol limit).
Partial recordings must remain partial with the same row count. Record measured
errors even on failure; do not loosen the gate to accommodate a poor converter.

## DEC-0096 - Streaming SSTV helper transport (2026-09-18)

Pinned sstv/src/decoder/mod.rs from_samples accepts Iterator<Item=i16> and
events consumes it incrementally. Add --stdin as the input argument using a
bounded buffered little-endian PCM reader, shared with file input. Keep the
same mode, sample rate, four-image and 360-second limits, row protocol and
pixel algorithm. Short reads, split samples and Interrupted reads are handled;
odd EOF, I/O errors and over-budget input cause a nonzero exit. Output remains
provisional until successful exit, as in the existing C++ file wrapper.
Flush metadata as well as rows so a pipe consumer sees completed images promptly.
The session owner must drain stdout/stderr concurrently, close stdin at its
sample budget and kill/reap on cancellation or a wall-time deadline. Blocking
stdin reads run only in this child, never on RX/GUI threads. No detached workers.
Gate: malformed-reader units plus independent Robot36/Martin1 full/partial
file versus pipe RGB/metadata parity and scanlines observed before stdin closes.
This is a transport milestone, not live RF acceptance. Fractional-rate input
conversion and GUI/receiver integration remain T-0031; never round a rate as
a substitute for resampling. No new dependency or P25/audio path change.

## DEC-0095 - Bounded live SSTV ingress before radio wiring (2026-09-18)

MainWindowP25Orchestration's analog branch already exposes FmMultiplexBlock
for NFM before speaker processing; Demod.cpp stamps rate, tuned identity, epoch
and sample position. Do not feed post-squelch speaker buffers or WFM MPX into
SSTV. First implement/test an isolated NFM ingress contract, not a live feature
claim. One producer, one consumer; fixed preallocated 16 slots x8192 floats,
also capped at two seconds of input by sample rate. These are memory/resource
budgets, not latency targets or protocol timers. Producer uses try_lock and
never waits on the decoder; contention, invalid blocks, missing/overlapping
samples, source/rate/tune/epoch changes and queue overflow flush queued input
and emit explicit discontinuity before new data. No sample splicing or padding.
Consumer/control operations may lock; they never run in RF/audio callbacks.
The owning session must detach/quiesce its producer before start, stop or
destruction; the queue alone does not cancel in-flight receiver callbacks.
This explicit lifecycle requirement must be tested again when wiring RX.
Next steps are actual streaming helper input, fractional-rate conversion and
GUI attach/detach/retune integration plus independent replay parity. Keep those
unavailable until proven. No P25, speaker or receiver wiring in this foundation.

## DEC-0094 - Progressive recorded SSTV and 0.2.58 release (2026-09-18)

Pinned backend Decoder::events() emits ImageStart/Row/ImageEnd while consuming
a pull iterator. Complete the recorded GUI feature before live push integration.
Optional helper --progress emits bounded JSONL RGB scanlines; no DSP or pixel
math changes. C++ validates schema, dimensions, image index, unique row indices,
hex length/content and final metadata/row agreement. Compare assembled preview
pixels with final RGB output before publishing PNGs. Limits: <=4096 bytes/line,
4 MiB stdout total, 64 KiB stderr, four images and prior time/input bounds.
These are transport/resource budgets, not protocol sensitivity constants.
Preview snapshots at eight-row intervals go into one mutex-protected latest
image, sampled by a GUI timer; never queue a full image per scanline. Clear
preview on failed/cancelled work. Saving remains final validated output only.
Test fragmented/malformed/duplicate/out-of-order row transport and independent
recordings, retaining exact GUI/direct output parity. Publish GUI+progressive
recorded reception as 0.2.58 with tested signed assets. Live RF is explicitly
not in this release and remains T-0022's next stage; P25 remains untouched.

## DEC-0093 - Recorded SSTV GUI on the shared file decoder (2026-09-17)

T-0022 continues with a nonmodal Tools window, one offline job per window and
one window per main application. Reuse DEC-0092 decoding unchanged; add an
optional cooperative cancellation callback, checked during PCM loading and
helper waits. Kill/reap the owned helper on cancellation. Cancellation ends
before output publication; once saving begins finish the bounded four-image
transaction rather than presenting a half-written result as cancelled.
QThread owns file decode and QProcess, never GUI or receiver/DSP callbacks.
GUI receives completion on its owner thread, keeps original-resolution images
and scales only previews. Open source, new output directory, mode selection,
decode/cancel, result list and output-folder action. Partial and no-image states
remain explicit. Close while running defers until worker stops; destruction
requests cancellation and joins, never terminates a thread.
Test real Qt widgets with a deterministic worker for single-job/close/error
cases, and the actual pinned decoder with independent recordings. No live SSTV
or P25 modification, no fabricated percentage progress or quality score.

## DEC-0092 - Isolated recorded SSTV image backend (2026-09-17)

T-0022 next gate: evaluate MIT-licensed unexcellent/sstv commit
16bf34aac81b0041f5fdce52a1aef64eea0d5f6e as a pinned Rust helper, rather than
copy GPL reference implementations or invent image demodulation. Its event API
reports actual rows and completeness; retain those distinctions. Only std and
libm dependencies are needed with C++ handling audio-file loading and PNG saving.
Development toolchain Rust 1.88 is installed inside build/toolchains only, without
altering PATH or unrelated projects. Pin Cargo.lock and retain dependency notices.

Recorded-only CLI first: bounded mono WAV/FLAC, <=360 seconds/128 MiB, temp PCM,
argument-array QProcess launch of application-adjacent helper, 120-second worker
deadline, capped metadata and <=4 images with bounded dimensions. No shell,
network, live radio changes or unbounded output. Save complete and partial
results explicitly, never infer protocol correctness from a plausible picture.
Compare a real off-air Robot36 recording and independent Martin recording before
shipping image modes; unsupported modes stay unavailable. Image GUI/live routing
remain separate. Release 0.2.57 includes only gates that actually pass.

## DEC-0091 - Bounded SSTV VIS inspection first (2026-09-17)

T-0022 begins with native recorded-audio header inspection, not image reception.
QSSTV 8c27d6d169d8c6c197eb47c2089870e39bc06a02 sstvtx.cpp/sendPreamble/sendVIS
defines 300 ms 1900 Hz, 10 ms 1200 Hz, 300 ms 1900 Hz, 30 ms start,
eight 30 ms LSB-first data/parity symbols (1100=1,1300=0), 30 ms stop at 1200.
sstvparam.cpp lists the parity-inclusive mode codes. Independent colaclanth/sstv
3e556eee8ad4c4425799cb652bac26ee58f8e113 supplies m1.ogg and its expected mode.
Both projects are GPLv3; no source is copied/linked and their audio is kept in
build-only QA, not redistributed without a separate fixture rights review.

Implement the published tone/framing facts, with a fixed 1 ms search grid and
10 ms rectangular Goertzel probes. This diagnostic requires >=75% normalized
tone energy and a 2:1 winning tone ratio: deliberately conservative engineering
acceptance gates, tested on synthetic tones and the independent recording, not
claimed as RF sensitivity specifications. Probe the leader throughout, require
break/start/stop and even parity; unknown seven-bit IDs remain unknown, not a
guessed mode. No extended/narrow VIS or image success claim. Bounded 910 ms
history, <=8192 samples/call, explicit reset on source loss, finite samples,
mono WAV/FLAC 8..96 kHz and 120 seconds/file. Output events/counters in CLI JSON.
No dependency added, no P25/analog routing changes, no pretend image adapter.
Follow-up gate reproduced Unicode recording-path failure: the existing narrow
fopen path uses Windows code-page semantics. Convert CLI UTF-8 to a native
filesystem path and use miniaudio's wide-file API on Windows for this new loader.
That alone did not pass: CLI batch echo already contained question marks before
file loading. Build batch strings from QCoreApplication::arguments() after Qt
initialization instead of the CRT narrow argv. Keep flag parsing/DSP unchanged;
rerun existing ASCII CLI regressions alongside the Unicode recording test.

## DEC-0090 - Checked release publication (2026-09-17)

T-0027: release.ps1 currently ignores native failures and pushes master although
the working branch is fix/acch-rescue-clear-grant-mac. Require an explicit version,
default to experimental, check each native command, run CTest before packaging,
and push the actual attached branch. Require committed source before packaging;
release metadata is committed only after successful packaging/signing. Validate
portable contents, installer/manifest hashes and detached signature before upload.
Publish 0.2.56 with the accumulated tested decoder/workspace/runtime work; do not
claim SSTV or satellite reception implemented. Those reference gates remain open.
No P25 tuning/security changes in this release-hardening pass.

## DEC-0089 - Deploy the configured RTL runtime (2026-09-17)

T-0026 evidence: CDB shutdown_probe_05 captures AV in libusb control transfer
under RTL/Soapy unmake. Standalone probe_rtlsdr_lifecycle.py (no Qt/Soapy/DSP)
reproduces close AV with shipped rtlsdr.dll on cycle 2; the configured vcpkg
rtlsdr 2.0.2 package passes 10 cycles. Both libusb DLL hashes are identical.
The executable folder contained a legacy v0.7.0-190-gdfd8 DLL not the manifest's
configured dependency. Make executable builds stage the imported rtlsdr target
deterministically, retain the old binary under build for forensic comparison,
and include its dependency licence in staging. No driver API, gain, DSP, slot,
queue or teardown timeout changes. Validate deployed native probe and actual
GUI under CDB, then reception. Physical Blog V4/RSP testing remains unperformed;
do not equate the Generic R820T acceptance with all hardware certification.

## DEC-0088 - Debugger-backed GUI acceptance (2026-09-17)

T-0026 intermittent shutdown AV has no root-cause stack yet. Extend existing
RDS GUI acceptance with an explicit CDB executable option, first-chance AV
stack logging and a strict failure if any AV occurs, even if application code
catches it. No system-wide debugger settings, registry edits, driver replacement
or speculative teardown patch. Retain normal real-hardware/station gates and
bounded process waits. Debugger exit alone is not application acceptance.

## DEC-0087 - Independent RTL capture comparison (2026-09-17)

DEC-0086 established native/adapter parity but not RF acquisition. Existing
rtl_sdr CLI can capture the same device without SDR Town/Soapy. Compare short
98.1 MHz captures at requested 40 and 20 dB, with identical offline analysis.
These are controlled diagnostic gain settings, not new app defaults or a
presumed fix. Extend the diagnostic to explicit unsigned 8-bit IQ, centered
at 127.5 and scaled by 128, with size validation and independent tests. Preserve
the sample rate/gain/tool output and decoded groups. No P25 changes.

## DEC-0086 - Same-input live RDS parity diagnostic (2026-09-17)

Two DEC-0085 live runs failed PI/PS identification while recorded adapter/native
parity passed. Add an opt-in SDR_TOWN_RDS_PARITY_LOG diagnostic wrapper: feed
identical borrowed MPX to the native backend and adapter, compare all published
decode fields except wall-clock timestamp values, count disagreements and
write one JSONL summary per exercised receiver at destruction. Reset/source
semantics match the adapter's documented contract. No sample logging, queues,
threads, DSP threshold changes or default extra decoding. Diagnostic overhead
is explicitly double RDS decoding and is not a performance benchmark. Never
treat agreement alone as successful reception. CLI fixture tests must validate
the report before the actual GUI live test uses it.

## DEC-0085 - Shared receive decoder contract, RDS-first adoption (2026-09-17)

T-0021: existing RdsMpxDecoder, CtcssDecoder and DcsDecoder already have bounded
synchronous input and thread-safe native snapshots. Wrap them behind a small
versioned receive interface; immutable registry lists only these implemented
decoders. Explicit float-domain (raw MPX vs discriminator), source ID, sample
clock, frequency identity, epoch, absolute cursor and gap flag accompany every
borrowed block. No retained spans, added queue, hidden resampling or new thread.
Reject wrong domain/version/invalid metadata, clear backend state, and force
reacquisition. Source changes force discontinuity even if epochs/cursors match.
Concrete snapshot variants preserve existing typed fields; factory selection
is allowlisted. Registration means compiled adapter, not guaranteed DLL/RF
availability. Backends still perform their own content validation and loading.
Adopt RDS in both GUI and CLI MPX file replay only after direct/adapter parity
on the independent recorded MPX fixture and synthetic tone adapters. Existing
tone file paths remain intact; P25 is not registered or migrated. No nominal
SSTV/satellite support entry until a real decoder exists. New libraries are
internal CMake organization, not new third-party dependencies.

## DEC-0084 - Experimental receive-only DCS (2026-09-17)

ETSI 103236 section 4.2 specifies 23 bits, 134.4 baud, LSB-first and physical
deviation polarity. test_dcs_reference.py independently checks codewords and
Golay polynomial 0xC75. Use SDRTrunk's enumerated payload values as protocol
data, not its reversed-bit I labels. Generate words algebraically and index
cyclic alignments and actual complements, retaining ALL equivalent labels.
Engineering profile, not certified squelch: three identical words spaced 23
bits confirm; expire after 46 bits without repeated-word evidence. Exact parity
only, no error correction. Test every code/polarity/rotation and malformed input.
Experimental recovery: eight staggered integrate/dump phases at nominal 134.4
baud, 2 Hz DC removal, two 300 Hz low-pass poles. Require two agreeing timing
hypotheses and a unique vote winner. These design choices require shaped/noisy/
clock-offset fixtures; no RF performance claim. Preserve all speech and P25 DSP.
Known-radio acceptance remains deferred by user; informational output only.
Catalogue test found 105 values in both our list and the reference, despite the
reference comment claiming 104; exact list comparison showed no differences.

## DEC-0083 - SSTV and public satellite receive roadmap (2026-09-17)

User requests SSTV, public satellites and weather satellites in the expansion
plan. Track per-downlink capabilities and hardware/coverage requirements, not
an unsupported promise of every spacecraft. Preserve the working P25 path and
finish DCS continuity/validation before adding another live decoder.
Sources reviewed: SatDump pipeline documentation and GPL-3.0 license;
gr-satellites supported-satellite documentation; ON4QZ/QSSTV; NOAA POES status
page search result (direct page fetch returned 403). Links and gates are in
SATELLITE_AND_SSTV.md. These references identify candidates, not dependencies
approved for bundling or proof of current transmitter operation.
Prefer a proven backend where appropriate, with pinned versions, explicit
input/output contracts, license review, bounded worker queues and cancellation.
An external process can isolate faults; it does not waive license obligations.
No network catalogue import may execute arbitrary commands. No automatic
transmit, decrypt, or private-traffic collection is part of this milestone.

## DEC-0082 - Preserve NFM data history during same-rate bandwidth updates (2026-09-17)

Live CTCSS GUI QA failed: 22 resets, zero complete windows at exit, with
automatic bandwidth 7605.46875 Hz. Inspection found exact bandwidth inequality
restarting the data stream even when its sample clock and FIR length agree.
FIR delay stores input IQ, not coefficient-dependent output: retain that history
on NFM coefficient updates when rate and length are unchanged. Continue resetting
on source gaps, rate/length/mode/center/identity changes and explicit DSP resets.
Do not change WFM behavior or speech DSP. Require varying-bandwidth regression,
bit-identical tapped/untapped speech, and repeated live GUI measurement.
User deferred independent known-tone RF acceptance until their radio is available.

Follow-up measurement: build/ctcss_reset_qa/run.log shows mid-stream reset
reason 2 only (explicit/automatic speech DSP reset), unchanged 12500 Hz bandwidth,
unchanged IQ epoch and exactly adjacent cursor. Demod's cumulative >5 kHz AFC
target test resets its speech oscillator. Isolate the NFM data mixer phase;
its nominal identity, source cursor and explicit resetState() define continuity,
not the speech-only automatic target threshold. Retain WFM behavior. Regression
must cross that threshold while proving unchanged speech output.

Final live isolation run passes with eight current-stream windows, but logs
three bandwidth-only GUI resets jumping source cursors (21:01:09/11/18).
syncMonitorVarsToReceiver conflates analog NFM bandwidth adjustments with a
retune, resetting the input cursor and audio ring. Preserve analog NFM stream
when only bandwidth changes; Demod already rebuilds coefficients and resets
data history when its clock/FIR length changes. Keep old reset behavior for
actual frequency/mode changes, P25 voice/control and all other modes.

## DEC-0081 - Receive-only CTCSS identification (2026-09-17)

Next user-approved decoder milestone. Start with informational CTCSS; do not
gate or reshape working audio before independent RF validation. GNU Radio's
gr-analog/lib/ctcss_squelch_ff_impl.cc documents the classic 38-tone set and
adjacent/edge guard-frequency approach. TI SPRA096 documents Goertzel energy
evaluation. Implement our own bounded streaming bank using that mathematics,
not copied GNU Radio code. Link references in docs/NFM_TONES.md.
Engineering acceptance profile (not a certification claim): one-second Hann
windows resolve the closest supported tones, two matching windows confirm,
DC removal and four cascaded 300 Hz low-pass poles suppress voice; >=65% tonal
purity, 4:1 strongest/runner-up energy and +/-1% guard comparisons reject
ambiguous/noisy/off-frequency windows. Test all supported frequencies, gain,
speech interference, noise, missing samples, switches and arbitrary partitions.
Tune/rate/mode/source-gap reset all data-only state. No invented DCS decoder
or tone squelch UI: defer these until reference codewords and RF captures exist.
Nominal channel identity is explicit for the NFM tap so phase-continuous AFC
updates do not erase a tone window. Preserve source-epoch and true retune resets.
GUI tone freshness is two seconds without input (presentation only).

## DEC-0080 - Live RDS and waterfall interaction (2026-09-17)

User requests automatic WFM RDS display, frequency-aligned band sections and
drag tuning while preserving improved audio. Use existing chronological IQ
window provenance (DeviceManager's vector wrapper previously discarded it),
reset only MPX/RDS state on gaps, and run the bounded decoder on the receiver
DSP owner. No RDS processing in P25 branches. UI reads short snapshot locks;
hide identity on inactive/non-WFM/retuned receivers and mark no recent RDS
after five seconds (presentation policy only, never a decoding/audio gate).
Draw disjoint visible band sections using existing priority/ambiguity lookup.
Drag previews on a frozen frequency axis, commits once on release: existing
frequencySelected handler retunes hardware, so emitting on each mouse movement
would queue repeated retunes. Click still commits on release; squelch stays
independent and only owns its spectrum region. Test actual mouse events,
section clipping and metadata lifecycle, plus recorded DSP and full regressions.

## DEC-0079 - RDS DSP integration and measured live gate (2026-09-17)

Partition gate reproduced: [mpx-partition] fails at the second 137-sample
block. Inspection also finds the shared audio FIR reads future input and
zero-pads block tails. Rather than alter audible analog output in this task,
the optional RDS tap will use a separate causal FIR/decimation state over the
same downmixed IQ and existing channel coefficients. Preserve phase across
all chunks; no future samples, no guessed gap filling. Disabled path remains
unchanged and enabled/disabled audio must still compare bit-for-bit.

Use pinned redsea subcarrier/liquid wrappers, not a newly invented carrier or
clock recovery. Existing liquid-dsp submodule is 9e00870e25ce9ecf473b7474875a19a3dfc52ce9;
main CMake explicitly disables MSVC integration. Probe installed GCC 11.3
MinGW to build an isolated DLL with a versioned C ABI: opaque handle, floats
in and byte bits out, caller-owned buffers, exceptions caught at the boundary.
Never pass std::complex, STL objects or ownership of allocations across CRTs.
If successful, reproducible CMake helper build and local absolute-path loading
are required; static MinGW runtime linking avoids hidden runtime DLLs.

Vendor pinned redsea DSP with documented minimal adaptations only: extract
MPXBuffer from file-reader dependencies and widen stream counters to avoid
the documented seven-hour timing jump. Full recreation on discontinuity resets
all DSP state, not just the upstream partial reset. Test real upstream MPX
fixture PI 0x6201, chunk invariance, reset/rate errors, noise and bounded input.

Live GUI integration is gated on those tests. Keep decoded metadata separate
from speech PCM and P25; bounded work, receiver identity and stale-state rules
must be explicit. Repair WFM decimation timing only after a failing partition
test, preserving original aligned-block audio. Do not claim RF validation from
synthetic or MPX replay alone. No P25 timeout, gate or vocoder changes.

## DEC-0078 - RDS foundation without changing P25 (2026-09-17)

User explicitly defers further P25 optimisation and approves the decoder
roadmap. Reuse redsea BlockStream/group at commit
7555c9f6259d50718697ee8c9f218ea012c6892c (windytan/redsea), retaining upstream
license and file notices. These modules provide bit sync, CRC and burst FEC
without the full executable's liquid-dsp/libsndfile/iconv dependencies.
No new RF decoder is claimed: this milestone consumes already-demodulated
MSB-first RDS bits, with a CLI fixture path and strict complete-group metadata.
Incomplete groups cannot publish station text; retune/reset clears all state.
Use bounded assembly and explicit PI / radiotext A-B lifecycle.

Add an opt-in WFM multiplex output before audio LPF/de-emphasis/squelch with
actual rate, frequency and reset/overwrite provenance. Disabled by default;
one retained block, no background thread and no audio mutation. First prove
audio equivalence enabled/disabled. Live 57 kHz extraction, carrier/timing
recovery and GUI metadata are next and require reference RF tests. A decoder
registry should follow a working MPX consumer, not list unavailable decoders.

## DEC-0077 - Partition-invariant P25 PCM interpolation and feature QA (2026-09-17)

Live GUI capture 20260917_084229 has five follows, zero IQ overruns and zero
producer drops, but underrun rises and rejected/missing VCWs. These are not
proof that interpolation causes all gaps. Source inspection separately finds
resampleDecodedP25PcmWithState reads idx+1/idx+2 and clamps them to the current
block tail. Therefore identical PCM split into frames differs from one batch.
Extract this function unchanged for a failing partition-invariance test. If
reproduced, use a two-input-sample causal delay so its cubic stencil uses only
available samples, retaining phase/DC state and exact frame counts. No new
vocoder, smoothing, gates or buffering thresholds. Test 8 kHz to 48/44.1 kHz,
20 ms and irregular partitions. Delay is stencil support, not guessed jitter.

Visual QA also found dB tick labels mapped upside-down over the entire widget
instead of using the spectrum curve transform. Correct that mapping. Band-plan
Qt tests must use explicitly selected settings format: the org/app constructor
does not follow setDefaultFormat, so it persisted the test's US selection.
Repair test isolation and restore AU. Report coverage/remaining RF gaps honestly.

## DEC-0076 - Explicit receive-band profiles, not inferred protocols (2026-09-17)

Replace the mixed-country first-match table with immutable selected profiles
and value-returning lookup. Region/country/location are explicit metadata;
local profiles can be imported from bounded validated JSON. Use half-open
intervals, priority then narrowest span; equal-rank incompatible overlaps must
not force a mode. Most-specific mixed/data entries block broader analog hints.
Band-derived defaults apply through existing AUTO paths, not forced changes to
manual modes or P25 follow. No plan grants encryption/decoder trust. Label
decoder hints separately from decoder availability. No IP/geolocation lookup.

Primary references checked 2026-09-17: ACMA Australian spectrum plan and CB
class licence, Ofcom UKFAT/PMR446 guidance, CAA aeronautical stations, USCG
marine channel table, NOAA NWR frequency list. Sources and partial coverage
travel with each profile. Receive bandwidths are application defaults, not
channel spacing or regulatory limits. New dependencies: none.

## DEC-0075 - Presentation-only workspace foundation (2026-09-17)

User approved the next-feature roadmap. MainWindow currently stacks receiver,
saved-frequency, P25, TX and capture controls in one QVBoxLayout. Reparent the
existing widgets into named Qt docks; retain all signal handlers and DSP paths.
Use a small WorkspaceLayout owner for presets, versioned QSettings state,
visibility/lock actions and reset. No custom docking dependency, decoder rewrite,
or pretend RDS implementation. Keep runtime-automated sessions from overwriting
the normal saved layout. Expose preset and screenshot arguments for GUI QA.
Gate: Qt interaction/persistence tests, real GUI screenshots at different sizes,
full existing tests, unchanged P25 reference replay. This presentation work is
independent of unresolved all-call P25 audio acceptance.

## DEC-0074 - Serialize exceptional audio cursor mutation (2026-09-17)

AudioEngine callback loads r, copies PCM, then stores advanced r. clearBuffers
stores w to that same read cursor without excluding the callback; bridge
discard also writes it. An in-flight callback can overwrite a clear/discard
with its stale r. Producer reuse can then race with its sample reads.
Use a per-ring consumer lease for the callback and exceptional clear/discard.
The callback only tries once and emits silence if control owns the lease:
it never waits, allocates or logs. Non-RT clear/discard waits for an active
consumer to finish. Normal SPSC pushes remain concurrent with consumption.
Do not attribute recorded live gaps to this race without capture evidence.

## DEC-0073 - Walk this-window validated block tails (2026-09-17)

`[block-tail]` fails on physical burst 12: with one permitted lock and two
complete valid superframes in one block, the second frame's A/B bursts are
lost. Uncovered-sync recovery only sees C/D S-ISCH. Permit the existing
bounded complete-burst walk for block input only when a lock in this very
commit established the current anchor. Never extend a retained previous-eye
anchor in block mode. Use the existing two-dibit local sync tolerance for
that current block. Preserve selected-slot/mask/security processing.

## DEC-0072 - Reject nonphysical Phase 2 quadrant permutations (2026-09-17)

Complete 060515 trace at 06:57 shows burst absolute dibit 57777 decoded
twice: the bad eye swaps every 0/2 (74 symbols) and preserves all 1/3 (86)
relative to the correct eye. I-ISCH goes from zero errors/location 0 to six
errors/location 2; codec repeats follow. The search admits all 24 arbitrary
quadrant permutations. SDRTrunk DQPSKGardnerSymbolEvaluator maps cyclic
angles -135,-45,+45,+135 to 3,2,0,1. Rotation/conjugation preserve opposite
dibit pairs (XOR 3); a 0/2-only swap does not. Restrict Phase 2 traffic search
and remembered candidates to these eight physical rotations/reflections.
Leave Phase 1 search unchanged. Verify all permutations, reference IQ, and
latest IQ before judging improvement; do not relax security to get audio.

## DEC-0071 - Complete opt-in replay provenance (2026-09-17)

Explicit validation still throttles pre-gate records to 250 ms and final
records to 100 ms, hiding frames in 80 ms replay hops. Permit unthrottled
records only with explicit validation plus SDR_TOWN_P25_VALIDATION_ALL=1.
Keep default/automatic logging bounded and existing rotation. This is an
offline forensic tool, not a live performance benchmark or gate relaxation.

## DEC-0070 - Explicit replay end-of-stream drain (2026-09-17)

GUI reference replay 103841 decoded 404 PCM frames but left 5760 samples
pending until its eight-second drain timeout: the empty speaker ring required
a 240 ms startup prime, while the entire remaining tail was 120 ms. At an
explicit end of stream no more samples can satisfy that threshold. Bypass
startup priming only for this drain, retaining whole-frame output, capacity
limits and already-applied security/slot gates. Live startup is unchanged.

## DEC-0069 - Resolve slot ownership before selecting session state (2026-09-17)

Reproduced by `[slot-session]`: I-ISCH location 2 rebases local C to absolute
burst 10 / slot 1, but its caller passes slot 0's state. With explicit starting
I-ISCH in the fixture, the original helper loses the known slot-1 talkgroup;
the corrected helper preserves 30302 and its own encryption state.
Use the same I-ISCH resolution and actual
burst position for both session selection and burst labelling in all paths.
Keep missing-I-ISCH fallback, mask phase, and security acceptance unchanged.
Reference: SDRTrunk `SuperFrameFragment` constructs C/D timeslots with the
final-fragment ownership swap before their messages reach per-slot modules.

## DEC-0067 - Exact RS arithmetic caching (2026-09-17)

Accepted after exhaustive arithmetic tests and same-IQ comparison. Replace
repeated GF64 polynomial multiplication/inversion and unit-symbol syndrome
construction with immutable lookup tables. Keep polynomial 0x43, decoder
search order, FEC/CRC criteria, and security gates unchanged. Capture 060515
commit time falls from 4423 ms to 421 ms over 92 windows; reference 103841 WAV
SHA256 is unchanged. No timeout/queue-size tuning is authorized by this result.
Full evidence and remaining gaps: `P25_AUDIO_FORENSICS_20260917.md`.
A decision is recorded **before** code that depends on it is written.

---

## DEC-0066 — Dead unknown-grant parks must return in ~8s (not ~45s)

- **Date:** 2026-09-15
- **Status:** accepted
- **Evidence (capture 20260915_131458):**
  - Return-to-CC stick fixed (0065); monitoring continued.
  - **Zero** `P25 audio output` lines; live_speaker.wav ~30 KB empty.
  - Unknown-enc follows TG12068/10326/… sat **~45s** each with `drop=A`
    `no-vcw-from-live-window` until ACQ watchdog.
  - FSM used `waitingUnknownClearGrant` no-VCW **45000/40000** ms.
- **Decision:**
  1. Unknown-grant + cold dead (no bursts/VCW/speech): **8s tuned / 6s silence**.
  2. Unknown-grant with some structure but still no VCW: **12s / 8s**.
  3. Clear-grant cold dead: **10s / 7s** (was 45s/30s).
  4. Keep long holds only when call was already live (continuation / speech).
  5. Hard-timeout also covers unknown/cold dead (~12s), was excluded before.
- **Out of scope:** why those grants had zero VCWs (RF/slot/enc) — next after
  hangs stop starving CC of follow opportunities.
- **Consequences:** Dead parks free the tuner quickly so the next clear grant
  can be followed. Expect ACQ watchdog ~8–12s on empty voice, not ~45s.

## DEC-0065 — Return-to-CC must follow physical RF, not retunesPrimary flag

- **Date:** 2026-09-15
- **Status:** accepted
- **Evidence (capture 20260915_125341 TG10120 @421.350):**
  - Live clear emits for ~few seconds (CADENCE duty≈0.7–1.0) then drop=A /
    no-vcw; ACQ watchdog returned.
  - Return logged: `continuing muted control-channel monitor on 420.350 …
    without RF retune` while cf was still **421.33875** (voice low-IF).
  - `retunesPrimary=false` because select thought CC "fits" on voice-park LO,
    but start still moved primary LO off CC → P25 log went quiet (no CC in
    passband).
- **Decision:**
  1. On independent-traffic release: if primary RF center is >75 kHz from CC,
     take one-RTL return/warm-standby path even when `retunesPrimary` was false.
  2. When start moves primary LO away from CC (DEC-0015 align path), latch
     `p25IndependentTrafficRetunedPrimary=true`.
- **Out of scope:** sparse mid-call feedRatio/absDup (separate audio duty track).
- **Consequences:** Return after one-RTL voice must retune home and keep watching
  CC. Prove: no `without RF retune` while cf≠cc; expect retune/warm-standby +
  continued stage-lock lines.

## DEC-0064 — Warm-standby must not kill CC monitor on return

- **Date:** 2026-09-15
- **Status:** accepted (amends DEC-0063 idle early-out)
- **Evidence (bridge Monitor-CC → follow → return):**
  - Voice ends; one-RTL warm-standby holds RF on voice while follow flags clear.
  - CC decode/validation resumed off-frequency (`p25CcInPassband` true at 2.048 Msps).
  - After follows >30s, ~28 bad windows → `disableP25ControlMonitorDueToValidation`
    zeros `p25MonitoredControlFreqHz` → P25 log stops; not watching CC.
  - DEC-0063 idle same-CC early-out could skip recovery re-arm while RF still on voice.
- **Decision:**
  1. Pause CC decode + validation while `warmStandbyUntilMs` is active (and while
     one-RTL traffic retuned primary).
  2. On real RF return to CC (immediate / follow / warm-standby expire): reset
     validation + reseed analyzer (same spirit as Monitor CC button).
  3. Failed retune must **not** claim `p25MonitoredControlFreqHz`.
  4. Idle arm early-out only if RF is physically on CC and not in warm-standby.
  5. Status exposes `warmStandbyActive`; analog tune / FUBAR refuse during it.
  6. Bridge P25 control **persists** `autoFollow` (no restore of stale GUI config).
- **Out of scope:** RID/TG decode quality; streaming DDC; DEC-0012.
- **Consequences:** Return-to-CC after bridge follow must resume muted CC watch
  like the normal GUI path. Prove via warm-standby log then
  `P25 control validation armed` / continued CC NAC lines (no disable).

## DEC-0063 — Idempotent P25 control arm + refuse analog tune while follow live

- **Date:** 2026-09-15
- **Status:** accepted
- **Evidence (FUBAR ↔ SdrTownControl.dll):**
  - Follow snaps back to Monitor CC **420.350** mid-call; P25 receive stops.
  - `armGuiRuntimeP25Control` always retuned to CC, cleared
    `p25AutoFollowVoiceFreqHz` / follow flags, and reset the live decoder —
    so a repeated `/v1/p25/control` (or Monitor-CC re-arm) killed the grant.
  - FUBAR analog `Tune` posts `/v1/tune` with `p25AutoFollow=0`, which took
    the non-P25 path and could retune RF off the voice channel.
- **Decision:**
  1. Same-CC re-arm while follow/traffic/voice-freq live → **idempotent**
     (refresh autoFollow + return-CC only; keep RF/voice/decoder).
  2. Same-CC idle re-arm with matching autoFollow → no decoder wipe.
  3. Analog `/v1/tune` while follow live → **409** unless `force=true`.
  4. Status exposes `voiceFrequencyHz`; log control tune requests.
  5. FUBAR bridge refuses Tune when status shows follow/traffic active.
- **Out of scope:** RID/TG decode quality (DEC-0062+); streaming DDC; DEC-0012.
- **Consequences:** FUBAR Monitor-CC / poll must not snap RF to CC mid-grant.
  Prove with `p25-control-arm-idempotent` / `tune-refused-follow` log lines.

## DEC-0062 — Mid-grant talkspurt must reset mbelib (225923 RID garble)

- **Date:** 2026-09-13
- **Status:** accepted
- **Evidence (capture 20260912_225923 TG30304 clear @421.225 slot=1):**
  - Live WAV CLEAR ~24 s; CADENCE ok=18/71 (TG30304 mean duty 0.55).
  - `src=unknown` on all emits — grant SRC never painted; cannot split RIDs in log.
  - GOOD emits (41): feedR≈1.0, absDup=0, push p50=**360 ms**.
  - BAD emits (37): raw feedR≈0.30 but **uniqueFreshR p50=1.0** (absDup==ctxDrop
    always) with fresh_tgt p50=**6** (~120 ms) — overlap tax, not fresh kill.
  - Operator: one RID perfect, other talk on same grant unintelligible.
  - Vocoder / preferred-AMBE / sequencer reset only on call/slot/freq change
    (`p25Phase2SyncAmbeEmitDedupeCallContext`); MAC_PTT only cleared pending
    AMBE queue. Predictor state survived mid-grant talker changes.
  - DEC-0061 240+280 held (workers ctx=280); not a thin-overlap regression.
- **Decision:**
  1. On target-slot **MAC_PTT** after this call has spoken: reset selected
     mbelib + resampler + audioTail + frame sequencer + preferred AMBE
     variants (**keep** abs-dibit de-dupe + security latch + opposite module).
  2. On target **END_PTT / IDLE / HANGTIME** after spoken: set pending flag;
     reset on next target voice or on following MAC_PTT.
  3. Debounce 400 ms against retransmitted MAC_PTT.
- **Out of scope:** inventing MAC_PTT source-address offsets; thinning overlap;
  streaming DDC; DEC-0012.
- **Consequences:** New talkspurts on the same TG grant should not inherit the
  prior RID's AMBE predictor. Re-prove live; expect `VOCODER_RESET
  why=mac-ptt-*` between talkers when MAC is visible.

## DEC-0061 — Restore catch-up overlap 280 ms (153932 jitter)

- **Date:** 2026-09-13
- **Status:** accepted (amends DEC-0060 sizes; restores DEC-0024 overlap)
- **Evidence (capture 20260912_153932 after DEC-0060 280+80):**
  - Live WAV CLEAR but **97** active islands, len p50=**40 ms**, mean≈94 ms;
    short≤80 ms=**75**; chop pairs (island≤120 & gap≤120)=**76**.
  - Worker shapes dominated by **160+80 / 280+80** (ctx=163840); empty-audio
    majority on those eyes; sustain 80+280 almost absent while speaking.
  - Audio top-up **68/69 bridge** (960/1920) at ringFill≈10–12%; underrun
    path inventing continuity between short emits.
  - pcm_vs_wall p50≈0.70 — pace improved vs 152348 half-audio, but thin
    overlap recreated the DEC-0024 eye-death / stutter class.
- **Decision:** Speaker `backlogCatchUp` → **240 ms fresh + 280 ms overlap**
  (minFresh 160). Idle sustain stays 80+280. Dual-slot latch (0058.2) unchanged.
- **Out of scope:** streaming DDC default-on; DEC-0012; absDup budget waste
  (still B-0001).
- **Consequences:** Catch-up keeps DEC-0009 lock surface while advancing ≈
  emit wall (~200–230 ms). Re-prove live duty; expect ctx=573440 on catch-up.

## DEC-0060 — Speaker backlog catch-up must pace RF (152348 half-audio)

- **Date:** 2026-09-13
- **Status:** superseded by DEC-0061 (thin overlap caused 153932 jitter)
- **Evidence (capture 20260912_152348 TG11108 RID 0x243754 clear):**
  - Live WAV CLEAR but longest active run **0.84 s**; active_ratio≈0.52.
  - Emit islands **pcm_vs_wall ≈ 0.45–0.65** (literally ~half wall speech).
  - SRC 0x243754 feed_ratio **0.624** with absDup=184 ≈ missing feed; median
    targetVcw=18 fed=10–12.
  - Worker shapes **99× fresh=160 ms + ctx=280 ms**; emit dsp p50≈**220 ms** >
    fresh 160 ms on **20/28** emit jobs; budget trips while ctxDrop/absDup
    dominate (fed/tgt collapses to ~0.22 on high-absDup hops).
  - pcm_per_fed median **960** — vocoder fine when fed; failure is pre-vocoder
    under-advance + budget spent on context that absDup discards.
  - DEC-0059 ReturnEncrypted only on enc TGs 12068/12069 — not this bug.
- **Decision:** Speaker `backlogCatchUp` geometry → **280 ms fresh + 80 ms
  overlap** (360 ms total, same surface as sustain 80+280). Idle sustain stays
  80+280. Dual-slot latched continuation (DEC-0058.2) unchanged.
- **Out of scope:** streaming DDC default-on; DEC-0012; inventing PLC.
- **Consequences:** Catch-up advances ≥ emit dsp p50 so RF backlog drains and
  CQPSK budget lands on new voice. **Superseded:** 153932 proved 80 ms overlap
  → 40 ms WAV islands / bridge top-ups; DEC-0061 restored 280 ms overlap.

## DEC-0059 — Companion ESS must not abort clear follows (145139)

- **Date:** 2026-09-13
- **Status:** accepted
- **Evidence (capture 20260912_145139):**
  - Live WAV ~14.3 s CLEAR / fragmented (longest run 1.32 s); CADENCE ok=7/60;
    primary FEED_GATE; TG30302 mean duty ~0.25.
  - Clear TG **30302** RID **0x2391D7** emitted ess=clear / targetEss=clear, then
    `ReturnEncrypted trustedEss=yes … ess=enc macCrc=2` while grantLatch=no —
    abandoned mid-call. Encrypted hold then blocked re-follow; RID **0x2353DA**
    later recovered better (absDup=0).
  - Same RF dual-slot: encrypted TG **12068** companion on opposite slot.
  - Root: sticky session-painted ESS / `trafficStatus.diag.encrypted` ORed into
    follow ESS, plus monotonic `RecentTargetEssEncrypted` OR that never cleared
    on later clear observations. Pending drain also released opposite-only
    (targetVcw=0 pendingRel=8).
- **Decision:**
  1. Traffic processor security flags only from **this-burst** ESS/traffic-SO.
  2. Voice target ESS encrypted only from observed this-burst paint; sticky
     clear may keep known, never promote encrypted.
  3. Recent target ESS: this-window clear **clears** sticky encrypted.
  4. MainWindow follow ESS uses target-slot fields; do **not** OR traffic
     `diag.encrypted`. Do not Clear→Encrypted promote while latch is Clear.
  5. Refuse pending AMBE drain on opposite-only windows (targetVcw=0, opp>0).
- **Out of scope:** DEC-0012 softening; streaming DDC default-on; budget/worker.
- **Consequences:** Clear RIDs should survive companion encrypted ESS on the
  opposite timeslot. Still open: absDup/ctxDrop underpush, budget trips (B-0001).

## DEC-0058 — Speaker backlog catch-up + latched dual-slot continuity (142104)

- **Date:** 2026-09-13
- **Status:** accepted
- **Evidence (capture 20260912_142104 TG10301 clear @417.675 ~57s):**
  - Live WAV listen=CLEAR but only **~15 s** / 436 s; longest active run **2.24 s**.
  - Worker windows **63×** iq=737280 fresh=163840 context=573440 (80+280).
  - dsp p50≈**198 ms** / p90≈**585 ms**; worker-busy **56**; budget trips **154**.
  - Emit gaps mean≈**0.9 s** max≈**3 s** — weak/strange choppy islands.
  - Mid-call waiting clear grant with p2ess=clear / callClearTrusted=yes on
    dual-slot MAC-dead hops (target≈opp, mac≈0) after Clear latch.
  - DEC-0032 absolute ban on backlog overriding speaker 80+280 under-advanced RF.
- **Decision:**
  1. When speaker-sustain/active-clear **and** acklogCatchUp: keep **280 ms**
     overlap; advance **160 ms** fresh (minFresh 80). Idle sustain stays 80+280.
  2. Latched/spoken clear + selected-dominant + companion accounted + target ESS
     known clear → do not dual-slot-garble-drop / feed-starve (DEC-0012 companion-
     louder PostEmitMixedMacDead unchanged).
- **Consequences:** Catch-up while lagging without shrinking overlap; fewer
  waiting-clear holes after Clear latch. Budget/worker cost still open under B-0001.
  **Amended by DEC-0060 then DEC-0061:** catch-up sizes are now **240+280**.

## DEC-0057 — Wrong-TDMA status on clear immutable grant

- **Date:** 2026-09-13
- **Status:** accepted (amended after Catch/forensic)
- **Evidence (capture 20260912_135857 TG30003):**
  - Listen WAV **29.2 s CLEAR**; CADENCE ok=16/83; **wrong_tdma=47**; 75 follow
    lines Phase 2 wrong TDMA slot with p2vcw&gt;0 decoded=0 (no concurrent emit).
  - CC grant **SLOT=1** immutable; emit avg targetVcw≈13.7 opp≈6.9 — dual-slot
    RF with companion talker. Slot probe correctly refuses flip when mask known.
  - Logic: phase2WrongSlot set whenever opposite has VCWs and selected has none
    — brands normal companion dwell as wrong slot.
  - Catch with planted final-fragment I-ISCH: lock slips (sfOff=180) and absolute
    index for phys C is **10 → grantSlot 1** (standards C/D swap). Lock-relative
    parity for index 6 would have labelled TS1 and fought the CC grant.
- **Decision:**
  1. Do not set phase2WrongSlot when p25Phase2GrantedSlotImmutable (companion
     dwell / opposite-only silence on our timeslot). Pending drain same rule.
  2. Keep DEC-0055.3: **grantSlot** from I-ISCH absolute index when A/B agree;
     else lock-relative %12. Do **not** force lock-rel-only (that mislabels
     final-fragment C/D and does not stop companion-dwell status spam).
- **Consequences:** Status should stop spamming wrong-TDMA on clear grants while
  slot labels stay standards-aligned when I-ISCH is present. Remaining continuity
  holes still FEED_GATE/budget/worker.


## DEC-0056 — Clear follow hang + WFM default BW

- **Date:** 2026-09-12
- **Status:** accepted
- **Evidence (capture `20260912_134135`):**
  - Enc TGs return in &lt;1 s (`ReturnEncrypted`). Clear TG30302 hung ~57 s after
    speech; live WAV only **5.64 s** CLEAR; CADENCE ok=2/64, budget_trip=47,
    worker_busy=63, primary FEED_GATE.
  - Meta: NFM **12.5 kHz**, `audio_lpf_enabled=false` — P25 speaker is AMBE PCM;
    Demod LPF/WFM BW are **not** on this path.
  - Root hang: `clearTrustedHold` extended 40 s speaker grace with no traffic;
    structure-only `lastActive` refresh; clear no-VCW timers 45–60 s; carrier
    acquire hold after speech.
- **Decision:**
  1. Extended speaker grace requires **current** traffic evidence (not grant-clear alone).
  2. After clear speech, no-VCW return in **12 s tuned / 6 s silence**.
  3. Carrier-acquire hold only in early acquire (`lastActive` ≤ tune+2.5 s).
  4. After speaker/clear latch, do not refresh `lastActive` on structure-only.
  5. WFM/AUTO default BW **220 kHz** (snap 180–250); separate from P25.
- **Not fixed here:** clear mid-call blocky islands (dual-slot/epoch/budget/worker)
  — still open under B-0001; BW/LPF not the cause on Phase-2 follow.

## DEC-0055 — Epoch identity + dual-slot keep-selected PCM

- **Date:** 2026-09-12
- **Status:** accepted
- **Evidence:** Code audit of IQ→mbelib→speaker: (1) DualSlot MAC-dead
  `audio.clear()` punched holes after Clear latch with labelled selected VCWs;
  (2) `epochTrusted` soft arm `establishedClear && xorApplied && grantSlotKnown`
  fed wrong sticky phase as continuous garble; (3) grantSlot used lock-relative
  `%12` only — I-ISCH flip rejected (20260720) but absolute **origin** unused.
- **Decision:**
  1. **Dual-slot:** Clear latch + labelled selected (`targetVcw>0`, fed,
     strong/trusted) → keep PCM (`dual-slot-untrusted-keep-selected-pcm`); else
     still `audio.clear()`. Feed mute / DEC-0012 companion-louder unchanged.
  2. **epochTrusted:** remove bare establishedClear+xor+grantSlot. Continuity via
     this-burst SF/mask/MAC or `forceEstablishedFeed` only. Prefer hole over garble.
  3. **I-ISCH origin:** when A/B I-ISCH agree, rebase absolute index
     `location*4+local` for grantSlot/mask; missing/disagree → lock-relative.
     Do not flip via I-ISCH alone without origin rebase.
- **Out of scope:** streaming DDC default-on (DEC-0038); DEC-0012 softening;
  invent PLC; budget DEC-0052–0054.
- **Consequences:** Fewer post-clear dual-slot silence holes; wrong-phase feed
  rejects instead of garble; mid-lock slot labels track standards when I-ISCH
  present. Live 100% still needs B-0001 start/stop + listen harvester.

## DEC-0054 — Restore cold full-commit; sticky cheap only (`081416`)

- **Date:** 2026-09-12
- **Status:** accepted
- **Evidence:**
  - `061217` (pre-0052): ~**92 s** CLEAR WAV, many emits — almost fully clear.
  - `064509` (0052 skip): ~0.6 s golden then silence.
  - `081416` (0053): **0.36 s SILENT** WAV, **1** audio emit, cheap-commit
    log hits **0**, budget trips generic only, worker-busy 147.
  - Root: CQPSK half-budget headroom stopped search before
    `realtimeBudgetExceeded()`, so forceCheap never re-armed; when cold
    forceCheap did run it poisoned first eye (phase0 / no deep rescue).
- **Decision:**
  1. **Remove** DEC-0052 CQPSK commit-headroom reserve.
  2. **Cold / non-sticky:** never forceCheap; re-arm
     `kP25LiveColdCommitAllowanceMs` (**200**) and full annotate.
  3. **Sticky only:** cheap-commit + re-arm
     `kP25LiveStickyCheapCommitAllowanceMs` (**120**).
  4. Never skip-commit (064509). Keep DEC-0051 mid-loop aborts.
- **Consequences:** Aim to restore 061217-class continuous CLEAR follows.
  Worker may again run >80 ms on cold acquires — acceptable vs silence.

## DEC-0053 — Sticky budget path cheap-commits (not skip) (`064509`)

- **Date:** 2026-09-12
- **Status:** accepted
- **Evidence (capture `20260912_064509`, ~257 s, post–DEC-0052):**
  - First follow emit ~0.6 s **CLEAR**, then permanent silence; only **2** emits
    total (WAV **1.24 s**) vs `061217` **91.96 s** CLEAR / **478** emits.
  - CADENCE ok≥0.65=**0**; no_vcw **470**; budget_trip **4**; emit p50 **170**
    (improved) but continuity collapsed.
  - eye-lost **0** — not DEC-0048. Skip-commit after sticky ready zeroed VCWs.
  - Cheap-commit with already-expired CQPSK deadline also emits 0 VCW (annotate
    loops break on entry) — need a short re-arm allowance.
- **Decision:**
  1. **Reject** sticky skip-commit. Sticky+budgetGone → **cheap-commit** on
     sticky lattice (same forceCheap path as cold).
  2. Re-arm `kP25LiveCheapCommitAllowanceMs` (**50**) before cheap annotate.
  3. Prefer `cheap-commit` tags in p25_log budget-trip lines.
  4. Keep CQPSK headroom (DEC-0052); do not soften DEC-0012; streaming DDC off.
- **Consequences:** Expect multi-second CLEAR follows again with bounded dsp;
  logscan should show `cheap-commit sticky-sustain` not silence after first emit.

## DEC-0052 — Close mustAnnotateCommit budget hole (`061217`)

- **Date:** 2026-09-12
- **Status:** accepted
- **Evidence (capture `20260912_061217`, ~713 s, gapless, SNR≈17.9):**
  - Live listen overall **CLEAR** (3 brief GARBLED 2 s islands) — almost fully
    clear subjectively.
  - CADENCE n=676: A=446 D=197 ok=**20**; duty mean 0.135 max 1.038.
  - Emit dsp p50≈**223** / p90≈**562** (worse than 044651’s 212); worker-busy
    **690** (~0.97/s); rolling busy max still **~15.9 s**; wall-timeout **0**.
  - DEC-0051 budget-trip log hits: **0** — early-out never ran.
  - Root cause: `mustAnnotateCommit = phase2CqpskTrafficDemod || …` is always
    true on live Phase-2 follow, so processIq still ran full annotate/commit
    after the deadline; CQPSK also consumed the whole 80 ms budget before
    commit.
  - File bars on clear islands: duty 0.67–0.78 listen=CLEAR; LIVE_WORSE is
    continuity (busy cliffs), not RF/mbelib.
- **Decision:**
  1. Sticky sustain + budget gone → **skip full commit** (free worker; next hop
     continues lattice). Log `[p25][budget][dec0052] skip-commit…`.
  2. Cold first-eye + budget gone → **cheap commit** only (no 12-phase /
     deep rescue); flag `m_phase2ForceCheapRealtimeCommit`.
  3. Reserve ~half of realtime budget as CQPSK→commit headroom (25–60 ms).
  4. Surface budget trips into p25_log (`P25 budget trip:`) + logscan
     `budget_trip` signature.
- **Consequences:** Expect emit p50≪120, busy/sec down, ok→D cliffs fewer;
  listen should stay CLEAR. Do not soften DEC-0012; streaming DDC stays off.

## DEC-0051 — Cooperative mid-decode realtime budget abort (`044651`)

- **Date:** 2026-09-12
- **Status:** accepted
- **Evidence (capture `20260912_044651`, automated full forensic):**
  - Live CADENCE A=86 D=35 ok=5; TG10120 max duty **0.909**; TG20202 max
    **0.639**; worker-busy **135**; rolling grew to **~15.9 s** while busy.
  - Worker dsp: emit p50≈**212 ms** / p90≈**491 ms**; empty p90≈**228 ms** /
    max **642 ms** under healthy budget **80** / eye-lost **120**.
  - Wall-timeout **0** — DEC-0046 post-hoc wall never fired; jobs still held
    single-flight for hundreds of ms (CQPSK stop ≠ commit/mask abort).
  - File path still recovers RF/mbelib; PRIMARY class FEED_GATE + slow worker.
- **Decision:**
  1. Arm a processIq-scoped deadline (`armRealtimeDecodeBudget`) shared by
     CQPSK search, Phase-2 sync scan, lock walk, 12-phase mask hunt, and
     sticky burst walk.
  2. Abort those loops cooperatively when exceeded; keep best-so-far; still
     run annotate/commit when Phase-2 traffic requires it, but commit itself
     is budget-gated (fixes “mustAnnotateCommit always unbounded”).
  3. Do **not** clamp decode wall (DEC-0046 stands). Do **not** soften
     DEC-0012. Streaming DDC stays default-off.
  4. Catch `[p25][cqpsk][budget]` ceiling **350 ms** (was 2500); verifier
     `verify_p25_phase2_cooperative_budget_abort.py`.
- **Consequences:** Live worker should release near budget so the next 80+280
  eye can run; expect fewer worker-busy cliffs and less rolling explosion.
  Operator still resets PPM≈−2 after DEC-0049; re-prove with start/stop +
  DEC-0050 listen classifier.

## DEC-0050 — PCM listen classifier (CLEAR / GARBLED / SILENT) for live vs file

- **Date:** 2026-09-12
- **Status:** accepted
- **Evidence:** File IQ replay is repeatedly “pretty good” (duty≥0.65 / PASS) while
  live same-day captures are islands then nothing / subjective garble. CADENCE
  duty, drop A–E, and STT chars/words do **not** score perceptual clear speech;
  duty can pass on blocky/garbled PCM.
- **Decision:**
  1. Add `p25_pcm_listen_classify.py` — frame RMS / ZCR / spectral flatness /
     envelope CV → **CLEAR | GARBLED | SILENT** (orthogonal to duty).
  2. Dump `*_live_speaker.wav` during start/stop IQ capture (speaker-push PCM).
  3. Wire classify into `run_p25_capture_full_forensic.py` +
     `run_p25_listen_bar_harvester.py`; CLI `p25 listenclassify <wav>`.
  4. Fixtures: file-replay WAV goldens + live/file mismatch flag
     `LIVE_WORSE_THAN_FILE`.
- **Consequences:** Automation can fail a “PASS duty” bar when listen=GARBLED,
  and prove live-path regressions without relying on operator ears alone.

## DEC-0049 — Harden Auto PPM after 1250 Hz overshoot (`044651`)

- **Date:** 2026-09-12
- **Status:** accepted
- **Evidence (capture `20260912_044651`, ~268 s):**
  - Live: TG10120 ok duty up to **0.909** (5 windows); TG20202 max **0.639**
    drop D; CADENCE A=86 D=35 ok=5; worker-busy **135**.
  - Auto PPM applied **twice** from AFC=**1250.0**Hz conf=**0.45**:
    ppm −1.93 → −4.91 → **−7.88**. Summary residual AFC ~874 Hz. CC TSBK
    showed high dibit corrections / CRC notes after overshoot.
  - File voicetest TG10120 duty **0.64**; TG20202 **0.615** — RF+mbelib OK;
    live “nothing” after good islands is eye/worker, not codec.
- **Decision:**
  1. Reject AFC samples on soft-probe rail ±1250 (±5 Hz).
  2. Min conf **0.55**, max |AFC| **2000**, max step **1.5** ppm, cooldown
     **120 s**.
  3. Apply only from **trusted CC offset** — never fall back to
     `gLastAfcOffsetHz` after Phase 2.
  4. `p25AutoPpmAfcSampleAcceptable` + Catch `[p25][ppm][dec0049]`.
- **Consequences:** Stops LO walk-off. Operator should reset device PPM
  near **−2.0** before next listen. Worker-busy / feedRatio still open.

---

## DEC-0048 — Escalate eye-lost cand=16 on first post-emit miss (`041612`)

- **Date:** 2026-09-12
- **Status:** accepted (implemented; live re-prove pending)
- **Evidence (capture `20260912_041612`, ~215 s, DEC-0046 exe):**
  - Live CADENCE n=100: drop A=82 D=13 B=5; duty max **0.399**; ok≥0.65 **0**;
    worker-busy **103**; Auto PPM **0**; wall-timeout **0**.
  - **Same IQ file** `p25 voicetest` TG20202 slot1 8s:
    `PASS_PARTIAL` **duty=0.805** drop=ok ambe=322/322 — RF+mbelib fine.
  - Live emits briefly then permanent `no-vcw` under healthy cand=4; eye-lost
    waited streak≥2 before cand=16.
- **Decision:** `kP25LiveEyeLostReplayCandStreak = 1` (first post-emit eye-lost
  hop uses cand=16 / 120 ms). Keep healthy cand=4/80. No mbelib-neo, hop/TTL,
  DEC-0012 soften, or streaming default-on.
- **Consequences:** Faster live re-lock toward file duty. Worker emit dsp
  p50≈227 ms still open (cooperative abort).

---

## DEC-0047 — Automated `p25 logscan` deep forensic CLI

- **Date:** 2026-09-12
- **Status:** accepted
- **Evidence:** Live “no audio” invisible to voicetest; need CADENCE /
  worker-busy / wall / Auto PPM / reject-before-feed rollup from startstop logs.
- **Decision:** `python src/tools/p25_logscan.py` + CLI `p25 logscan <dir>`
  prints primary failure class and mbelib-neo advice (only when CADENCE ok).
- **Consequences:** Every listen with startstop is automatable.

---

## DEC-0046 — Do not clamp live decode wall / wipe pending on wall stamps

- **Date:** 2026-09-12
- **Status:** accepted (reverts DEC-0045 clamp; hardens publish path)
- **Evidence:**
  - Operator: after DEC-0044/0045 Release, audio “really bad” again
    (almost-worked → next gap-fix kills it).
  - Wall is checked **after** `decodeP25VoiceAudioBlock` returns — it does
    not cooperatively abort. Jobs can still run ≫ budget.
  - Publish path: `decode-wall-timeout` without keepEvidence called
    `p25Phase2ClearStaleResultSpeakerPending` → wiped playout mid-call.
  - Clamping healthy wall to **105** made empty eyes (>105 ms) hit that
    path constantly → continuous audio death.
- **Decision:**
  1. **Reject** DEC-0045 wall clamp; healthy/eye-lost keep global wall **320**.
  2. Wall stamps (`decode-wall-timeout` / `overbudget-kept`) must **never**
     clear speaker pending; empty overruns publish diags only.
  3. Keep DEC-0044 auto PPM (return-to-control only).
  4. No hop/TTL / DEC-0012 soften / streaming default-on.
- **Consequences:** Restores pre-0045 pending continuity. Worker-busy when
  dsp ≫80 remains open — needs cooperative budget abort, not wall stamps.
  Catch `[p25][dec0046]` + verifiers lock the invariant.

---

## DEC-0045 — Cap live healthy/eye-lost decode walls (`032907`)

- **Date:** 2026-09-12
- **Status:** **rejected** (superseded by DEC-0046)
- **Evidence (capture `20260912_032907`):** emit-gate dsp p50≈451 vs budget 80;
  wall 320. Intent was to yield single-flight sooner.
- **Decision (original):** clamp healthy wall 105 / eye-lost 145.
- **Why rejected:** wall is post-hoc; clamp increased empty-timeout pending
  wipes without shortening jobs. See DEC-0046.

---

## DEC-0044 — Auto PPM from sustained CC AFC (return-to-control only)

- **Date:** 2026-09-12
- **Status:** accepted (implemented; live re-prove pending)
- **Evidence:** `032907` / prior captures: ppm device **0.0** while CC AFC
  sits near **~884 Hz**. Soft-AFC cold seed exists; live still acquires then
  cliffs on worker throughput. Manual PPM only today (`setFrequencyCorrection`
  / CLI `ppm`). Baking CC AFC into LO mid-voice fights DEC-0016 park.
- **Decision:**
  1. On **return-to-control** (not warm-standby / not mid-voice), optionally
     apply `estimatePpmCorrectionDelta(CC AFC, CC Hz)` via
     `DeviceManager::setFrequencyCorrection`.
  2. Gates: |AFC| 200–3500 Hz, conf ≥0.45 (or trusted CC offset ≤5 min),
     |Δppm| ≥0.40, step clamp ±5, cooldown 30 s.
  3. Prefer `gP25LastTrustedControlOffsetHz` over live AFC globals after
     Phase 2 park.
- **Consequences:** First follows start closer to LO; does not by itself fix
  worker-busy drop D (that is DEC-0045).

---

## DEC-0043 — Post-speak opp-dominant sticky wipe debounced (twin rescue reverted)

- **Date:** 2026-09-12
- **Status:** accepted (debounce kept; ±1 twin rescue reverted after `024000`)
- **Evidence:**
  - `20260912_020758` TG20201 clear: post-speak opp-dominant sticky invalidate
    wiped proven XOR/epoch → permanent `no-vcw` (file duty ~0.80).
  - `20260912_024000` live: clear TG30003 @421.975 file
    `PASS_CONTINUOUS duty=0.705` but live max **0.649**, wrong-TDMA /
    `no-sf-mask` / worker-busy. Soft DUID ±1 twin rescue + debounce stuck
    bad epochs — twin path reverted.
- **Decision:**
  1. After speak, opp-dominant sticky invalidate requires streak **≥3**.
     Pre-speak keeps immediate invalidate.
  2. **Do not** prefer ±1-burst lock twins via soft DUID scores on block path.
  3. No hop/TTL / DEC-0012 soften / streaming default-on.
- **Consequences:** Live clear continuity still open (worker drop D). Re-prove
  on ENC=clear follows after rebuild.

---

## DEC-0042 — Healthy live sustain uses cand=4 / 80 ms (002128 forensic)

- **Date:** 2026-09-12
- **Status:** accepted (implemented; live re-prove pending)
- **Evidence (capture `20260912_002128`, ~54 min, SNR ~15 dB, DEC-0041 exe):**
  - CADENCE n=2731: mean duty **0.115**, ok≥0.65 only **99**; drops
    A=1891 / D=650 / B=91 / ok=99. worker-busy **2782**.
  - **Same failure on every TG** (30017/30302/30003/10301/10330/20201/…):
    not a one-site grant quirk.
  - Drop D feedRatio **0.445** ≈ ok feedRatio **0.427** — playout duty is
    low because fewer VCWs/windows complete per second (tv 50 vs 117,
    windows 6.4 vs 10.2), not because the feed gate newly blocks.
  - Hard cliffs (duty≥0.50→&lt;0.15, n=27): **every** example co-timed with
    worker-busy in the prior 2.5 s; then targetVcw collapses → drop A.
  - Emit-gate WORKER dsp p50 **199** / p90 **535** ms on 80+280 eyes while
    sustain needs ~80 ms hops under pending=1.
  - **File voicetest** same capture TG **30017** slot1 skip=0 8s:
    `PASS_CONTINUOUS duty=0.83` — RF/MAC are continuous; live extract starves.
  - DEC-0041 eye-lost budget cap alone did not move the mean-duty class vs
    `234224` (0.134 → 0.115 on a longer sample).
- **Decision:**
  1. When post-emit eye is **healthy** (`!eyeLost`): live hot caps
     **cand=4 / budget=80** (`kP25LiveHealthySustain*`).
  2. Keep DEC-0041: first eye-lost miss cand=8/120; streak≥2 → cand=16/120.
  3. No hop/TTL change. No DEC-0012 soften. No streaming default-on.
- **Consequences:** Re-prove file bars 060036/095846. New live CADENCE must
  raise ok seconds / cut worker-busy without collapsing any TG’s peak duty.

---

## DEC-0041 — Live eye-lost re-lock keeps cand=16 inside hot 120 ms wall

- **Date:** 2026-09-12
- **Status:** accepted (implemented; file bars + live re-prove pending)
- **Evidence:**
  - Capture `20260911_234224` (~372 s gapless CC IQ, SNR ~12.5 dB) on ACCH
    branch Release: CADENCE drop **D=101**/338 with `worker-busy` while
    `pendingJobs=0` / `busy=yes`; operator heard short &lt;1 s islands after
    brief emit.
  - WORKER (rate-limited): dsp p50 **69.5** / p90 **461** / max **1515** ms;
    emit-gate dsp p50 **202** / p90 **590**; submit interval p50 **159** ms
    vs DEC-0009 sustain fresh **80** ms.
  - DEC-0035/0039 live eye-lost used full replay caps **cand=16 / budget=240**.
    That matches CLI/voicetest width but on the single-flight GUI worker the
    240 ms wall + block-channelize produced multi-hundred-ms jobs → scheduler
    skips → drop D death spiral. Healthy eye correctly stayed cand=8/120
    (DEC-0019 / 060221).
  - Do **not** soften DEC-0012. Do not invent hop/TTL. Do not default-on
    streaming DDC (DEC-0038).
- **Decision:**
  1. Keep DEC-0039 definition of eye-lost (post-emit no-target counts).
  2. First consecutive post-emit eye-lost hop stays **cand=8 / 120** (cheap
     challenge). Streak ≥2 escalates to **cand=16** (replay width).
  3. Live escalate budget is **`kP25LiveEyeLostReplayBudgetMs` = hot 120**,
     not `kP25ReplayHotBudgetMs` (240). CLI/voicetest may still use 240.
  4. Realtime unknown-mask deep ACCH rescue stays alt-kind capable but is
     bounded to top **2** phases × deep budget **1** (was 4×2 on the ACCH
     branch) so acquisition cost cannot re-feed drop D.
- **Consequences:** Re-prove file bars 060036 / 095846. Live CADENCE on a
  new GUI follow should cut worker-busy drop D without losing DEC-0035-class
  re-lock width after a short miss streak.

---

## DEC-0040 — Mechanical split of `main.cpp` (ISS-0004 / T-0009)

- **Date:** 2026-09-09
- **Status:** accepted (complete 2026-09-10)
- **Evidence:**
  - `src/main.cpp` was ~35k lines: shared P25 helpers, voicetest, `MainWindow`,
    `runCLI`, and `main()` in one TU (ISS-0004). After Phases 0–8: ~2k leftovers
    + `main()`; orchestration in dedicated TUs.
  - String-lock verifiers use `src/tools/p25_orchestration_sources.py`.
  - ISS-0004: must not mix a rewrite with feed-gate behavior changes.
- **Decision:**
  1. Mechanical move-only split into focused TUs:
     `P25AppGlobals`, `P25TalkgroupRegistry`, `P25VoiceTiming`, `P25RollingIq`,
     `P25VoiceDecode`, `P25VoiceTest`, `CliApp`, `MainWindow`, `AppBootstrap`,
     thin `main.cpp`.
  2. No hop/TTL/CADENCE/feed-gate/audio behavior changes in split commits.
  3. Verifiers read concatenated orchestration sources via
     `src/tools/p25_orchestration_sources.py`.
  4. Build + `sdr_town_tests` + verifier sweep after each extract phase.
  5. Branch `refactor/iss-0004-split-main`; push + PR when green.
- **Consequences:** Maintainability for P25/GUI/CLI; follow-up may further
  split the `MainWindow` constructor after this lands.

---

## DEC-0039 — Post-emit no-target counts as eye-lost (DEC-0035 refine)

- **Date:** 2026-09-09
- **Status:** accepted (implemented; live re-prove pending)
- **Evidence:**
  - Operator: latest live not like DEC-0035 / 095846 (~95%). Capture
    `20260909_110941` TG 10301 slot 1: max duty **0.452**, **0×** ≥0.65
    (095846 same TG/slot max **0.947**, 10× ≥0.65).
  - 110941: brief emits then long `no voice sync` / drop A; 80+280 held.
    Some hops had structure/companion bursts with `targetVcw=0` — DEC-0035
    required bursts==0, so those stayed cand=8 and starved re-lock.
  - Streaming default-on still rejected (DEC-0038 duty 0.23). Product path
    remains block + DEC-0035-class live re-lock.
- **Decision:**
  1. Keep DEC-0035 replay caps (cand=16/240) on eye-lost after speak.
  2. Expand eye-lost: after `hadSuccessfulEmit`, `targetVcw==0` and
     `decodedFrames==0` counts as lost even if companion/structure bursts >0.
  3. Healthy target eye still cand=8/120. No hop/TTL change. No streaming
     default-on.
- **Consequences:** Live should recover toward 095846 continuous clear after
  first emit; prove with a new start/stop capture.

---

## DEC-0038 — Streaming sticky Gardner; do not default-on (060036 still 0.23)

- **Date:** 2026-09-09
- **Status:** accepted (implemented; default-on still gated)
- **Evidence:**
  - Operator ask: match SDRTrunk sticky HDQPSK so live is continuous like
    replay. File bar: 060036 TG 10301 skip=261000 center=421.96375.
  - Block (unset): `PASS_CONTINUOUS duty=0.705` essKnown=yes p2macCrc=156.
  - Stream env=1 before this DEC (DEC-0033): duty **~0.25**, cqpskLock=0.
  - Root in tree: unlocked CQPSK search set `candidateTiming.cqpskValid=false`
    every candidate, cold-acquiring Gardner on each 80 ms eye and defeating
    streaming Costas continuity (SDRTrunk keeps Gardner across chunks).
  - Tried post-commit discrete CQPSK lock create / soft-latch: duty **0.12**
    (worse) — froze a weak map while search stop engaged.
  - After sticky Gardner + warm differential-only shaping: stream duty
    **0.23**, targetVcw=132, p2macCrc=4, essKnown=**no**, still ≪0.65.
- **Decision:**
  1. Under `enableStreamingChannelDdc`, do **not** clear `cqpskValid` during
     unlocked CQPSK candidate search (keep sticky Gardner/Costas).
  2. When streaming Gardner is already warm, skip absolute/conjugate CQPSK
     maps and limit dibit permutations (π/4 DQPSK-shaped like SDRTrunk).
  3. Do **not** create/freeze discrete CQPSK lock from soft structure or
     post-commit alone on streaming (measured regression).
  4. **Do not default-on** streaming (DEC-0014 gate). Env=1 still fails
     duty ≥0.65 + essKnown on the continuous-clear file bar.
- **Consequences:** Streaming mechanics closer to SDRTrunk timing continuity;
  product path stays block-channelize (live DEC-0035 class) until env=1 hits
  ≥0.65. T-0010 / ISS-0001 remain open for streaming default-on.

---

## DEC-0037 — Restore clear-eye rolling hold; never purge on hold

- **Date:** 2026-09-09
- **Status:** accepted (implemented; live re-prove pending)
- **Evidence:**
  - After DEC-0036 always-advance, capture `20260909_100909`: operator heard
    little chirps instead of ~95% continuous. CADENCE max duty **0.40**,
    **0×** ≥0.65 (was **0.947** / 10× on 095846). emit>0 36/77 but duty
    mostly 0.04–0.16.
  - DEC-0036 fixed unknown-grant start silence but broke the clear sustain
    path that needed a one-hop MAC/ESS retry on the same RF.
- **Decision:**
  1. Restore `return false` (hold) for normal clear eyes that have selected
     VCWs neither fed nor queued.
  2. Advance only for `waitingForClearGrant` / late-entry waiting /
     `WaitingForClearGrant` diag (095846 TG 30302 class).
  3. On hold: keep `holdDecodeAbsolute` but **do not** purge newer publish
     results or worker jobs (that purge was the start-silence killer).
- **Consequences:** Expect clear follows back near 095846 continuity; unknown
  starts should not wipe the pipeline if a hold still occurs.

---

## DEC-0036 — Do not hold rolling cursor when VCWs were not queued

- **Date:** 2026-09-09
- **Status:** accepted (implemented; live re-prove pending)
- **Evidence:**
  - Capture `20260909_095846` after DEC-0035: later TG 10301 clear follow
    excellent (max duty **0.947**, 10× ≥0.65). Operator: ~95% good with small
    gaps/lag; **quiet follows at start**.
  - First follow TG **30302** ENC=unknown: window with `targetVcw=14` fed=0
    pendingQueued=0 → `rolling cursor hold` + purge newer jobs → then
    permanent `p2bursts=0` / drop A for ~60 s until TG 10301.
  - Root: `p25Phase2RollingDecodeWindowConsumed` returned false when selected
    VCWs were visible but neither fed nor queued (waiting MAC/ESS). Hold
    re-decodes the same RF and **purges** newer live work.
- **Decision:**
  1. If selected VCWs were not fed and not queued, **advance** the rolling
     cursor (return true). Overlap still re-sees recent dibits next hop.
  2. Do not soften DEC-0012. Keep DEC-0035 eye-lost replay caps.
- **Consequences:** Unknown-grant starts must keep extracting after the first
  eye instead of holding silent. Mid-call small gaps (ctxDrop / feedRatio~0.3
  from 280 ms overlap) remain a separate tighten if still audible.

---

## DEC-0035 — Live eye-lost re-lock uses replay CQPSK caps

- **Date:** 2026-09-09
- **Status:** accepted (implemented; live CADENCE re-prove pending)
- **Evidence:**
  - Operator: live still one-emit cliff; file/GUI replay almost continuous.
  - Capture `20260909_094846` on DEC-0034 exe: live CADENCE drop **A=60**/62,
    emit>0 **5**/62, max duty **0.338**. TG 30302 @ 417.675: brief VCW →
    emit → permanent `p2bursts=0` while hops stay 80+280.
  - **Same IQ** CLI voicetest TG 30302 skip=1300 center=417.66375 slot 0:
    `PASS_PARTIAL_AUDIO duty=0.43` with `targetVcw=652` / `p2bursts=711`
    (RF has continuous clear; live extract starved).
  - Live GUI worker after speak: hot **cand=8 / 120 ms**. CLI voicetest +
    GUI IQ-replay hot path: **cand=16 / 240 ms** (`kP25ReplayHot*`).
  - DEC-0034 (keep block CQPSK hint) did not change 094846 — wrong lever for
    this live/replay split.
- **Decision:**
  1. On live block-channelize `hotPhase2TrafficJob` after speak: if the
     previous hop has no Phase-2 eye (`p2bursts=0` and no target VCW/mask),
     use **replay** CQPSK caps (16/240). If the eye is present, keep
     DEC-0019 cand=8/120 (060221 worker-busy).
  2. Do not soften DEC-0012. Do not invent hop/TTL. Do not default-on
     streaming.
- **Consequences:** Reopen GUI on new exe. T-0010 prove = live CADENCE must
  keep extracting after first emit on 094846-class follows (not only file).

---

## DEC-0034 — Post-emit empty eye must keep block CQPSK hint

- **Date:** 2026-09-09
- **Status:** accepted (implemented; live re-prove pending)
- **Evidence:**
  - Capture `20260909_092250` on DEC-0033 exe (~139 s): emit>0 **9**/131;
    drop **A=116** / B=11 / D=4; max dutySec **0.416**; fresh **80+280** held.
  - TG **12014** slot1: ~10 s of strong extract (`targetVcw` 12–72) with
    `fed=0` (dual-slot / ESS unknown gates), then one `gate=emit`, then
    immediate permanent `p2bursts=0` / drop A while hops stay 80+280.
  - DEC-0032 post-emit empty-eye streak≥2 called `clearBlockCqpskHint()`.
    Block-channelize has no sticky Costas — the hint is the only carrier
    continuity across hops. Clearing it after the first speak matches the
    one-emit-then-silence operator report.
  - DEC-0033 companion-only sticky fallthrough was **not** streaming-gated
    and could thrash dual-slot sticky epochs on the default path.
  - File bar after fix: 060036 TG 10301 skip=261000 still
    `PASS_CONTINUOUS_AUDIO duty=0.705`.
- **Decision:**
  1. Post-emit empty-eye soft rehunt keeps `ForceMaskEpochRehunt` only —
     do **not** call `clearBlockCqpskHint()`.
  2. Companion-only sticky fallthrough stays behind
     `enableStreamingChannelDdc` (DEC-0033 streaming path only).
  3. Do not soften DEC-0012. Do not invent hop/TTL/PLC. Default-on
     streaming still gated (DEC-0033).
- **Consequences:** Reopen GUI on new `SDR_Town.exe`. T-0010 live CADENCE
  must not cliff to all-A after first emit while RF continues (092250 class).

---

## DEC-0033 — Sticky HDQPSK: use persistent framer; stop hop CPR

- **Date:** 2026-09-09
- **Status:** accepted (implemented; default-on still gated)
- **Evidence:**
  - Capture `20260909_083254` (DEC-0032 exe): post-emit hops stay **80+280**;
    rolling **4 s**; CADENCE drop **A=147**/152, emit>0 **5**/152. First TG
    30302 emit then ~0.6 s `no voice sync`. ForceMask never logged. Dominant
    failure is extract after emit, not hop geometry.
  - SDRTrunk `P25P2DecoderHDQPSK.receive`: continuous DDC → sticky Costas +
    Gardner → `P25P2MessageFramer` for channel life. OP25 CQPSK: sticky FLL/
    Gardner/Costas; hop resets frame sync only, not Costas.
  - Our default block path clears Costas every hop (`P25LiveDecoder.cpp`
    channelize). Streaming DDC (`SDR_TOWN_P25_STREAMING_DDC=1`) keeps Costas
    but still failed 105622 duty ~0.09–0.16 (DEC-0014/0018): after first emit,
    wrong-slot / `p2bursts=0` alternation.
  - Root in tree: `P25Phase2Framer` is fed and `m_pendingFramerBursts` filled,
    but commit only consumed them when `demodState >= TrackingSoft`. Carrier
    confidence only rose on `cqpskCarrierLoopApplied`, so state often stayed
    **Cold** → pending bursts cleared → sticky lattice walk early-returned on
    companion-slot bursts. Always-queue without anchor / empty annotate /
    clearing anchor on one misaligned burst regressed 060036 env=1 to duty
    **0.02**; those holes are fixed below.
  - **Measured after fix (060036 TG 10301 skip=261000 center=421.96375):**
    block 80+280 `PASS_CONTINUOUS duty=0.705` (held). env=1 stream
    `PASS_PARTIAL duty=0.25` (was ~0.09–0.16 class on 105622; no wrong-slot
    islands). 105622 IQ not on disk. Default-on still rejected.
- **Decision:**
  1. Stop hop/TTL/grace/PLC CPR for ISS-0001 continuous clear. Do not soften
     DEC-0012. Do not default-on streaming until env=1 hits duty ≥0.65 on a
     continuous-clear file bar + live CADENCE.
  2. Streaming DDC queues persistent-framer bursts for commit; commit consumes
     them only when a superframe anchor exists. Annotate with source dibits.
     Misaligned framer bursts skip, do not wipe the anchor. Carrier/timing FSM
     notes structure under streaming.
  3. Sticky lattice early-return must not stick on opposite-slot-only VCWs
     when preferred TDMA slot is known — fall through to re-lock (Costas stays).
  4. GUI IQ-replay must not prepend overlap context when streaming DDC is on.
  5. Voicetest prints cqpskLock / residHz / framerBurst / demodState when
     stream contiguous DDC is on.
- **Consequences:** Block 80+280 remains shipped default. Env=1 improved but
  still partial (0.25 on 060036). T-0010 stays open until live prove.

---

## DEC-0032 — Post-emit keep DEC-0009 80+280; soft empty-eye rehunt (revert DEC-0031 planner)

- **Date:** 2026-09-09
- **Status:** accepted (pending live measure)
- **Evidence:**
  - Capture `20260909_081701` (DEC-0031 exe, ~264 s, SNR ~8.1 dB). Operator:
    single little emit at follow start then hang on `no voice sync`.
  - Live CADENCE: drop **A** 234 / D 4 / B 3; emit>0 only **7**/241 s.
  - TG **10120** @ 421.975: cold eye emit=12 duty 0.239, then immediately
    worker hops `fresh=245760` (**120 ms** backlog catch-up) with
    `diag=no voice sync` for tens of seconds. DSP median ~70 ms.
  - Root: DEC-0031 planned backlogCatchUp **before** speaker-sustain. Cold
    emit dsp~428 ms left backlog → next hop left DEC-0009 80+280 → eye death.
  - Second hole: every post-emit empty eye raised `MaskEpochRepairHoldWindows`,
    which makes the planner skip speaker-sustain (steal geometry).
- **Decision:**
  1. Restore speaker-sustain / active-clear **before** backlogCatchUp
     (DEC-0009 80+280 after speak). Catch-up 120+280 only off that path.
  2. Post-emit empty-eye streak≥2: `ForceMaskEpochRehunt` +
     `clearBlockCqpskHint()` — do **not** set MaskEpochRepairHoldWindows.
  3. Keep DEC-0031 once-clear `requireFedAudio=false` continuation.
  4. Do not reopen post-emit cold 240/64, streaming default-on, or DEC-0012.
- **Consequences:** Live prove = after first emit, hops stay fresh=80 ms
  context=280 ms and CADENCE must not cliff to all-A `no voice sync` while
  talk continues (T-0010 / 081701).

---

## DEC-0031 — Once-clear: backlog catch-up before speaker-sustain; continuation without fed chicken-egg

- **Date:** 2026-09-09
- **Status:** accepted (planner part **superseded by DEC-0032**; once-clear continuation kept)
- **Evidence:**
  - Capture `20260909_062006` (DEC-0030 exe, ~46 s, SNR ~13.8 dB). LO correct
    (`rfCenter=421.96375` = voice 421.975 − 11.2 kHz). Rolling reaches
    **8192000** (4 s) — DEC-0030 held.
  - Live CADENCE: drop **A** 41 / **D** 1; one emit island duty **0.139**; then
    `no voice sync` with windows still running. DSP median ~**70 ms** on 80 ms
    fresh (drain OK; absStart “jumps” were rate-limited log artifacts).
  - File voicetest same IQ TG **30003** slot 1 center 421.96375: 
    `PASS_PARTIAL_AUDIO drop=D duty=0.46`. Emit islands alternate with
    `unknown-waiting-clear` / companion-louder (`oppVcw > targetVcw`, fed=0)
    — DEC-0012 isolation class, not missing RF.
  - Planner: `p25Phase2PlanVoiceDecodeChunk` early-returned DEC-0009 80+280
    whenever `activeSpeakerClearPath`, so DEC-0024 backlogCatchUp (120+280)
    was unreachable after speak.
  - Security gate called `SameCallSelectedTimeslotContinuationSafe(...,
    requireFedAudio=true)` — chicken-egg vs dual-slot mute clearing audio
    before continuation can keep trustedClear (DEC-0003 once-clear).
- **Decision:**
  1. Plan backlogCatchUp **before** speaker-sustain early-return (still
     DEC-0024 120 ms fresh / 280 ms overlap; no 180 ms live-edge catch-up).
     **Superseded by DEC-0032** (caused 081701 post-emit eye death).
  2. Once latch Clear / hadSuccessfulEmit / callHadSpeakerAudio, evaluate
     continuation with `requireFedAudio=false` so dual-slot untrusted does
     not collapse once-clear into `unknown-waiting-clear`. **Kept.**
  3. Do **not** soften `PostEmitMixedMacDead` (DEC-0012 / 041716). Do not
     reopen streaming default-on, hot cand=3, or post-emit cold.
- **Consequences:** Re-measure 062006 file duty (expect ≥0.46, hope toward
  0.65 if continuation recovers selected-dominant mixed hops). Live prove =
  after first emit, CADENCE must keep extracting when RF has voice (T-0010).
  Live eye vs file extract gap on 062006 remains open if still drop A.

---

## DEC-0001 — Copy Athanor method, not Athanor product law

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** Operator asked to apply SovereignFoundry / Athanor rules and
  methodology to `maulaudio_pro` and not to edit Athanor. SDR Town already
  depends on Qt 6, SoapySDR, mbelib, miniaudio, spdlog, nlohmann/json.
- **Decision:** Binding process is `DEVELOPMENT_RULES.md` (never guess,
  trackers, comments cite evidence, definition of done). Product law is
  `SOURCE_OF_TRUTH.md` for this receiver. Zero-external-library / Knox /
  ML-KEM rules stay in Athanor only.
- **Consequences:** New third-party deps still need a DEC. Existing deps stay.

---

## DEC-0002 — Classify every Phase 2 audio hole as A–E before changing gates

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** `docs/p25_phase2_regression_tracker.md` and README v0.2.50
  show months of contradictory hotfixes (open security → garble; close →
  silence; overlap decode → dup starve; bridge → fake continuity).
  CADENCE already counts `targetVcw`, `fed`, `emit`, `dups`, `dutySec`.
  Voicetest already defines continuous as duty ≥ 0.65 (`src/main.cpp`).
  Late-entry strong selected VCW bar already exists as 8 codewords
  (`kP25Phase2LateEntryStrongTargetVoiceCodewords`).
- **Decision:** Dominant drop bucket is computed by `classifyP25AudioDrop`
  (earliest pipeline stage wins):

  | Bucket | Label | Rule (scaled by `windowSeconds`) |
  |---|---|---|
  | E | follow | `followReturned` |
  | A | extract | unique selected VCW (`max(0, targetVcw − dups)`) below 8 per second, and emit below the continuous bar |
  | B | feed | unique ≥ 8/s and `fed == 0` |
  | C | emit | `fed > 0` and `emittedPcm == 0` |
  | D | playout | `emittedPcm > 0` and duty (`emittedPcm * 0.020 / windowSeconds`) < 0.65 |
  | ok | none | unique ≥ 8/s, fed>0, duty ≥ 0.65. Idle all-zeros is A, not ok. |

  CADENCE (1 s) and `p25 voicetest` print `drop=`. No timeout/gate/hop
  change until ISS-0001 cites a run.
- **Consequences:** T-0003…T-0007 stay blocked until a capture names a bucket.
  8/s cites the existing late-entry constant, not a new magic number.
  0.65 cites voicetest `continuousOk`.

---

## DEC-0003 — Selected-slot speaker policy follows SDRTrunk P25P2AudioModule

- **Date:** 2026-09-07
- **Status:** accepted (policy; code changes wait for measured bucket B or C)
- **Evidence:** SDRTrunk: one AudioModule per timeslot; queue Voice2/4 until
  PTT/ESS establishes clear/enc once; play that slot until squelch; never mix
  opposite slot. Our dual-slot MAC-dead mute (beeaea8 / 034136) stopped
  wrong-epoch garble and also punched holes in already-proven selected-slot
  audio (004206 duty drop). Sticky ESS alone is not epoch proof (034136).
- **Decision:** When we touch feed/emit (REQ-P2.2 / P2.3):
  1. Companion slot never feeds the speaker mbelib instance.
  2. Selected slot: after **this slot** has clear proof (this-window MAC CRC
     or this-window ESS clear on that slot, or MAC_PTT/ACTIVE clear), keep
     feeding descrambled Voice2/4 until END/HANG/squelch/encrypted.
  3. Dual-slot MAC-dead mutes **unproven** frames and pending drain. It does
     not mute selected-slot frames that already have this-slot proof for the
     current call key.
  4. Sticky grant-clear / latch without this-slot proof must not open dual-slot
     MAC-dead windows (034136 stays forbidden).
- **Consequences:** No feed-gate patch until REQ-P2.0. Isolation (SoT L1–L2, L7)
  outranks duty.

---

## DEC-0004 — PASS_PARTIAL_AUDIO is not done

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** Voicetest distinguishes `PASS_PARTIAL_AUDIO` vs
  `PASS_CONTINUOUS_AUDIO`. Roadmap had marked continuous “done”. Field ~50%.
- **Decision:** SoT L3. Roadmap item 1 is open. String verifiers are not the
  gate.
- **Consequences:** ISS-0005 closed by retracting the roadmap claim.

---

## DEC-0005 — Do not invent PLC to hide missing Voice2/4

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** SoT L5. Prior silence-bridge work was an underrun guard
  (`p25Phase2PlayoutBridgeAllowed`, max ~4.5 s). Users heard blocky/partial
  speech, not a missing 20 ms click.
- **Decision:** No dummy speech PCM. Bridge may only fill true ring underrun
  on an already-open clear call. Concealment stays mbelib repeat/erasure
  already counted in voicetest (`concealmentOk` ≤ 25%).
- **Consequences:** REQ-P2.4 must raise unique emit rate, not lengthen bridge.

---

## DEC-0006 — Overlapping traffic eyes rewind the dibit stream cursor

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** `alignPhase2AbsoluteDibitCursor` comment already required the
  cursor to be the start of the current chunk. Contiguous and gap/rewind
  paths did that; overlapping lookback (chunkStart < previous end < chunkEnd)
  did not. Voicetest 20260905_105622 TG 30003 slot 0 skip=97334: hop 1
  0–720 ms then hop 2 160–880 ms then 80 ms sustain eyes with 80 ms context.
  Cursor stayed at the previous end; sticky lattice walk decoded 80–560 ms
  off the bursts. Hop lines: p2sf/p2mask high with p2vcw=0 or
  `wrong TDMA slot` islands; `PASS_PARTIAL_AUDIO drop=D duty=0.28`
  (ISS-0001, REQ-P2.1).
- **Decision:** If the new chunk overlaps the previous stream, set
  `m_phase2StreamDibits = chunkStartAbsolute` without clearing sticky
  epoch/tail. True ring-reset rewind (whole chunk behind the cursor by more
  than one burst) still clears. Sticky walk covers every complete timeslot
  in the working window (no 540 ms cap on a 720 ms cold eye).
- **Consequences:** Overlap Voice2/4 is de-duped by existing abs-dibit
  session IDs, not by leaving the cursor past the IQ. Not a hop/TTL tweak.

---

## DEC-0007 — Context IQ must not discard never-emitted selected Voice2/4

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** After DEC-0006, voicetest 105622 slot 0 duty fell 0.28→0.125.
  Hop startMs=98294: `targetVcw=2 ctxDrop=2 fed=0 dup=0` — unique selected
  frames a prior 80 ms hop missed (`p2vcw=0`) were hard-dropped because they
  sat before `freshStart − 240 dibits`. Abs-dibit `ShouldEmit` never ran.
- **Decision:** Pre-fresh Voice2/4 may be suppressed only when
  `p25Phase2ShouldEmitAmbeFrame` says they were already emitted (or 12-dibit
  start match). Do not hard-continue on `codewordIsContextOnly`.
- **Consequences:** Overlap still cannot replay (ShouldEmit / lastAbs). Missed
  RF in the lookback can reach mbelib after the call is proven clear.

---

## DEC-0008 — Sticky superframe lattice is contiguous-dibit only

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** Voicetest 20260905_105622 TG 30003 slot 0 skip=97334 8 s
  (HEAD with DEC-0006/0007): hop 2 (720 ms block eye) `targetVcw=24 fed=30`;
  hop 3+ 160 ms sustain `p2sf/p2mask` with `p2vcw=0` or
  `Phase 2 wrong TDMA slot`; `PASS_PARTIAL_AUDIO drop=D duty=0.28`.
  Block-channelize clears CQPSK/Gardner/framer every hop
  (`20260729_114627`) then the sticky stream-space walk / anchor-aligned
  lock reused the previous eye's dibit lattice. Slot labels and Voice2/4
  DUIDs followed that stale grid. Streaming DDC keeps a real contiguous
  dibit stream; that path may keep the lattice.
- **Decision:** `findPhase2AnchorAlignedSuperframeLocks`, the hot sticky
  burst walk (early return), and the continuous sticky walk run only when
  `enableStreamingChannelDdc` is true. Block-channelize hops re-lock the
  superframe on this window's syncs. Sticky XOR mask phase stays.
- **Consequences:** Default CLI/GUI traffic (streaming DDC opt-in) extracts
  from each independent eye. Not a hop/TTL/minFresh change. Isolation
  unchanged (grant slot still authoritative).

---

## DEC-0009 — Locked block-channelize sustain eye is one superframe

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** After DEC-0008, voicetest 105622 slot 0 skip=97334 8 s duty
  0.40; 5 s talking slice duty 0.51 (`fed=128`). Sustain hops used 80 ms
  overlap → 160 ms eyes; many `p2bursts=1`. Hop 2 (720 ms) still recovered
  24 selected VCWs. A Phase 2 superframe is 12 × 30 ms = 360 ms (SDRTrunk
  SuperFrameFragment). Hop/minFresh stay 80/40 ms (not a TTL tweak).
- **Decision:** `kP25Phase2VoiceDecodeSustainOverlapSeconds` and
  `kP25Phase2VoiceDecodeSpeakerSustainOverlapSeconds` are 0.280 so
  overlap+fresh = 360 ms. Streaming DDC overlap remains 0.
- **Consequences:** More IQ per live tick than 160 ms eyes; still half the
  720 ms cold eye. Abs-dibit de-dupe still owns overlap.

---

## DEC-0010 — Deep overlap is lock-only after first emit

- **Date:** 2026-09-07
- **Status:** superseded by DEC-0013 (105622 duty 0.735→0.35; missed-eye catch-up needed)
- **Evidence:** Live capture `20260907_073304` (146 s gapless IQ). Operator:
  some audio better, still garbled + repeats.
  Repeats: TG 10330 slot 1 seq=386 emit=18 then seq=389 ctxVcw=10 ctxDrop=0
  (200 ms already-played overlap pushed again). CADENCE 17:34:54–58:
  dups=78–100/s, feedRatio=0.29, dutySec=0.72–0.92. Independent CQPSK eyes
  (DEC-0008) shift recovered abs by more than the 12-dibit ShouldEmit wobble,
  so DEC-0007's "context is playable if not an abs duplicate" replays speech.
  Tried also muting mixed MAC-dead continuation (TG 30302 seq=9 opp=16
  p2mac=0/2 gate=emit): 105622 voicetest duty 0.735→0.35. Reverted that
  half (T-0004 stays open).
- **Decision:** After `lastAbs != 0` or `hadSuccessfulEmit`, Voice2/4 that end
  more than one sustain hop (80 ms / 480 dibits) before `freshStart` are
  lock-only. The last hop of lookback still uses ShouldEmit (DEC-0007
  missed-eye). Dual-slot continuation escape is unchanged.
- **Consequences:** Overlap stays 280 ms for superframe lock (DEC-0009). Not a
  hop/TTL change. Mixed MAC-dead garble remains T-0004 / DEC-0012. Last-hop
  80 ms leak on independent eyes is DEC-0011.

---

## DEC-0011 — Block-channelize post-emit context is all lock-only

- **Date:** 2026-09-07
- **Status:** superseded by DEC-0013 (105622 duty 0.735→0.11 with DEC-0011)
- **Evidence:** Capture `20260907_073304` TG 10330 slot 1 after DEC-0010's
  480-dibit floor: seq=401 `ctxVcw=4 ctxDrop=0` (exactly one 80 ms hop of
  lookback still replayed). Independent CQPSK eyes shift recovered abs past
  the 12-dibit ShouldEmit wobble, so those four frames are treated as new.
  Same capture later TG 30302 CADENCE `dutySec=1.28–1.36` with `dups=0`.
- **Decision:** When streaming DDC is off (default live/voicetest block
  eyes), after `lastAbs != 0` or `hadSuccessfulEmit`, every Voice2/4 that
  ends before `freshStart` is lock-only. Streaming DDC keeps the last-hop
  ShouldEmit catch-up (contiguous abs, DEC-0007).
- **Consequences:** Overlap remains 280 ms for lock/MAC/ESS. Not a hop/TTL
  change. First-emit missed-eye catch-up unchanged (`lastAbs == 0`).

---

## DEC-0012 — Post-emit mixed MAC-dead mutes feed, not the hop PCM

- **Date:** 2026-09-07
- **Status:** accepted (feed-only; 2026-09-08)
- **Evidence:** Capture `20260907_073304` TG 30302 seq=9 `oppVcw=16 p2mac=0/2`
  `gate=emit` `action=explicit-clear-grant-traffic-clear-release` after a
  clean seq=1 `opp=0 p2mac=11/13`. Capture `20260908_041716` TG 10330 slot 1
  seq=131 `opp=0 p2mac=5/6` `trusted-clear-pending-release` then seq=134
  `targetVcw=6 oppVcw=12 fed=4 emit=4 p2mac=0/0 ess=clear`
  `explicit-clear-grant-traffic-clear-release`, then `Phase 2 wrong TDMA slot`.
  Operator: one good emit, then garbled wrong-slot audio. Call 1 on the same
  file already mixed-emitted (seq=27/36/47 `opp=8 p2mac=0/x ess=clear`).
  `DualSlotUntrustedGarbleWindow` stayed false because this-window ESS clear
  + companion accounted + strong selected (overlap Voice ESS / 004206 escape).
  Muting via that helper + `audio.clear()` dropped 105622 duty 0.735→0.38
  because it discarded already-proven PCM in the same hop.
- **Decision:** After the call has already spoken (`hadSuccessfulEmit` or
  sticky `p25Phase2CallHadSpeakerAudio`), if both slots have voice, this
  window has no selected MAC CRC, **and the companion is louder**
  (`oppVcw > targetVcw`), do not feed bursts that themselves lack MAC CRC.
  Same-call continuation and this-window ESS clear must not escape that
  skip. Do not brand the hop as `dual-slot-untrusted-garble-drop` and do
  not `audio.clear()` proven PCM. Selected-dominant mixed ESS
  (`targetVcw >= oppVcw`) still feeds (105622 startMs=99134 target=18
  opp=8; 004206). This-window selected MAC CRC still authorizes
  companion-louder hops (105622 startMs=100254 slot0Mac=2). Selected-only
  MAC-dead (`oppVcw=0`) still feeds.
- **Consequences:** Blanket mixed-MAC-dead mute after emit dropped 105622
  duty 0.685→0.27 (too many selected-dominant ESS hops). Companion-louder
  is the 041716 seq=134 / wrong-slot pattern. T-0004 first-hop
  `target=6 opp=28 fed=0` stays a separate cold-eye case. Equal mixed
  (041716 call 1 seq=27 target=8 opp=8) is still ESS-authorized.

---

## DEC-0013 — Overlap replay uses ISCH lattice identity, not lock-only

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** DEC-0010/0011 lock-only after emit dropped 105622 TG 30003
  slot 0 skip=97334 duty 0.735→0.35→0.11 (missed-eye catch-up, DEC-0007).
  Live 073304 TG 10330 seq=389/401 still replayed overlap because recovered
  abs moved more than the 12-dibit ShouldEmit wobble (DEC-0008 independent
  eyes). `Phase2VoiceFrameKey` equality includes that jittered abs / window
  local superframe anchor, so sequencer recentKeys also miss.
- **Decision:** After a selected-slot Voice2/4 is actually fed, remember
  `(slot, superframeBurstIndex, voiceIndex)` plus recovered abs. A later hop
  with the same lattice identity inside 1800 dibits (~300 ms, overlap 280 ms,
  superframe 2160 dibits) is a replay and is not fed. Next superframe keeps
  the index but is ≥2160 dibits later. Context Voice2/4 that were never
  emitted still use ShouldEmit only (DEC-0007). Mixed MAC-dead feed mute
  (DEC-0012) is feed-only after the call has spoken; do not hop-PCM clear.
- **Consequences:** Not a hop/TTL/overlap change. Requires ISCH superframe
  lock (`burstIndex < 12`); unlocked bursts still use 12-dibit abs.

---

## DEC-0014 — Independent Phase 2 traffic is SDRTrunk HDQPSK.receive

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:**
  - SDRTrunk `P25P2DecoderHDQPSK.receive(ComplexSamples)`: one Costas +
    Gardner + MessageFramer on a contiguous channelized stream; `main()`
    consumes 2048 samples at 25 kHz ≈ 81.92 ms. Two `P25P2AudioModule`s,
    `if (message.getTimeslot() == getTimeslot())`; queue Voice2/4 until
    PTT/ESS; PTT `clearPendingVoiceTimeslots()`; then 20 ms frames as they
    arrive. No overlapping independent CQPSK eyes.
  - Live capture `20260907_095450`: dual grant TG 30013 slot 1 + TG 30003
    slot 0 on 420.225 MHz. First eye `iq=1474560` (720 ms) `dsp=327 ms`
    `targetVcw=4 opp=0 p2mac=1/13`; seq=2 `decode-wall-timeout`. Later hops
    `fresh=80 ms context=160–280 ms` `dsp=159–573 ms` (wall 320 ms). seq=21
    `targetVcw=10 oppVcw=10 fed=10 emit=10 ctxDrop=0 p2mac=0/2` (200 ms PCM
    on an 80 ms hop = overlap replay + mixed-slot feed). First CADENCE
    `windows=593 dutySec=2.320` at 19:55:47 — 53 s of hops dumped as `1s`
    because `lastLogMs==0`.
  - Capture `20260830_064319` / `20260829`: auto streaming DDC into
    **20–40 ms** context-free slices on an unlocked eye → `p2bursts=0`.
    That was slice length, not the DDC. Cold eye stays 720 ms (DEC-0009).
- **Decision:**
  1. **Tried** independent realtime Phase 2 traffic enabling streaming DDC
     by default (SDRTrunk `receive`). Voicetest 105622 TG 30003 slot 0
     skip=97334 8 s fell `PASS_CONTINUOUS_AUDIO duty=0.685` →
     `PASS_PARTIAL_AUDIO drop=A duty=0.095` (`emptyWindows=80/89`,
     `targetVcw=56` vs 396). Same eye-loss as 20260830, not slice length
     alone — the first hop was already 720 ms. **Default stays
     block-channelize.** `SDR_TOWN_P25_STREAMING_DDC=1` still opts in.
     Do not hand off via `stickyReady`.
  2. When streaming DDC **is** on, locked hops are **80 ms** fresh-only
     (`overlap=0`), not 40 ms. Cite SDRTrunk 2048@25 kHz and 20260830.
  3. Hard reacquire `reset()`s Costas/DDC/framer. It must not flip an
     operator-opted-in stream back to block-channelize (`env=1` keeps DDC).
  4. CADENCE `1s` starts its bucket on the first diagnostic tick
     (`lastLogMs==0` discards, does not print) and uses elapsed time as
     `windowSeconds`. Capture 095450 `windows=593 dutySec=2.320` was a
     53 s backlog labeled as one second. Pending AMBE stays PTT-clear /
     ESS-drain (DEC-0003).
  5. DEC-0012 mixed MAC-dead is feed-only after emit (041716 seq=134).
     Slot bleed on 095450 (`targetVcw≈oppVcw`, `p2mac=0/x`, `ctxDrop=0`)
     remains independent-eye ISCH + overlap on the first hop (T-0004).
- **Consequences:** Live/voicetest default is still 80/40/280 block
  eyes (DEC-0009). CADENCE is honest. Streaming DDC remains the SDRTrunk
  stream experiment, not the proven continuous path on 105622.

---

## DEC-0015 — Tuner center is SDRTrunk CenterFrequencyCalculator

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:**
  - Operator: some follows look off-center.
  - Our Phase 2 follow parked `voice ± 250 kHz`
    (`kP25Phase2TrafficLowIfOffsetHz`) and reused a CC-centered tuner
    whenever `|voice−center| ≤ min(0.42*sr, max(250 kHz, 0.25*sr))`
    (~512 kHz at 2.048 Msps). Capture 095450 (CC 420.475, voice 420.225)
    therefore kept the tuner on the control channel: the grant sat 250 kHz
    off spectrum middle, and the CC channel overlapped the LO/DC spike.
    The 250 kHz offset was a local note (419.375 decoded from a 419.125
    ring, poorly when parked exactly on voice). That is “don’t sit on DC”,
    not SDRTrunk’s formula. Comment in `main.cpp` that claimed the 250 kHz
    park “matches SDRTrunk” was wrong.
  - SDRTrunk `CenterFrequencyCalculator.getCenterFrequency`
    (`source/tuner/manager/CenterFrequencyCalculator.java`), called from
    `HeterodyneChannelSourceManager.updateTunerFrequency` when
    `!tunerController.isTunedFor(channels)`:
    - One channel: `minFrequency − middleUnusableHalfBandwidth + 1`.
    - Two+ channels with span `≤ usableHalfBandwidth`:
      `minChannelFrequency − middleUnusableHalfBandwidth`.
    - `TunerChannel.getMinFrequency()` = `frequency − bandwidth/2`.
  - `DecodeConfigP25Phase2.getChannelSpecification`:
    `ChannelSpecification(50000.0, 12500, 6500.0, 7200.0)` → channel
    bandwidth **12500 Hz**.
  - R820T/R820T2 (typical RTL-SDR): `R8xEmbeddedTuner`
    `DC_SPIKE_AVOID_BUFFER = 5000`, `USABLE_BANDWIDTH_PERCENT = 0.98`.
    One traffic follow: `center = voice − 6250 − 5000 + 1` =
    **voice − 11249 Hz**. Wiki: do not park a decode channel on DC or on
    filter roll-off; recompute tuner center from the sourced set.
  - `TunerController.isTunedFor`: every channel inside
    `center ± usableBandwidth/2`, and none overlapping
    `[center − dcHalf, center + dcHalf]`.
- **Decision:**
  1. Replace the 250 kHz low-IF helper with SDRTrunk’s calculator
     (`include/P25SdrtrunkTune.h`). Keep the name
     `p25Phase2LowIfTrafficCenterHz` as a wrapper so existing string
     locks still see it.
  2. **Traffic-only** (CC paused, dedicated SDR, CLI waitgrant):
     single-channel center = `voiceMin − 5000 + 1`.
  3. **CC still live on the same tuner:** compute center for
     `{ccHz, voiceHz}` and **nudge** the tuner if `!isTunedFor`. Do not
     leave CC sitting on DC while DDC’ing traffic 250 kHz away. Do not
     pause CC for that nudge (`retunesPrimary` stays false).
  4. Reuse vs physical retune uses `canTune` (span ≤ usable bandwidth
     and a valid center exists), not the 250 kHz / 0.25*Nyquist quality
     clamp. Capture 005246 (750 kHz from a CC-at-DC tuner) was that
     geometry; after this calculator the lower channel sits just right
     of DC.
  5. No hop/TTL/feed-gate change. Do not re-enable default streaming DDC
     (DEC-0014).
- **Consequences:** Waterfall follows sit ~11 kHz right of center, not
  250 kHz off. File voicetest of 105622/073304 still uses capture
  geometry (`--center`), not this live LO. 095450 live should log
  `rfCenter≈420.21375` for CC 420.475 + voice 420.225 if both stay
  sourced. Isolation on 095450 dual-TG remains T-0004.
- **Superseded in part by DEC-0016:** two-channel `getCenterFrequency`
  on `{cc, voice}` is not the follow LO. It parks the *lower* channel
  off DC, which put 421.975 MHz at 997 kHz offset on capture 115315.

---

## DEC-0016 — Follow LO parks the granted voice, not the CC+voice set

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:**
  - Capture `20260907_115315` (162 s gapless, SNR ~17 dB). Operator:
    audio really bad. DEC-0015 two-channel calculator:
    - TG 30003 420.725 + CC 420.475 → `rfCenter=420.46375` **offset=261.3 kHz**
      (CC is the lower channel, so CC sits off DC and traffic is 261 kHz up).
    - TG 30003 421.975 + CC 420.475 → span 1.51 MHz ≤ usable 2.007 MHz so
      `canTune` was true. Set calculator then places highest at the high
      end: `rfCenter=420.97773` **offset=997.3 kHz**. Voice channel max
      equals `center + usableHalf`. SDRTrunk wiki: filter roll-off at
      spectral edges is unusable for decode. Capture 005246: 750 kHz
      offset left `p2vcw≈0`.
    - CADENCE after that hop: a few seconds `drop=D dutySec=0.22–0.45`,
      then `drop=A` `reject` high / `no-sf-mask` / `no-vcw-from-live-window`
      for the rest of TG 30003 and almost all of TG 30302 at 421.225
      (reused the 420.97773 LO, `kind=existing-wideband-source-low-if`).
  - SDRTrunk `CenterFrequencyCalculator` optimizes the *set* because
    every sourced channel is a first-class decode. This GUI has one
    selected speaker follow. The channel that must sit just right of
    the R820T DC hole is the **granted voice** (single-channel formula,
    DEC-0015). CC stays only if that same LO still `isTunedFor` CC.
- **Decision:**
  1. `p25SdrtrunkTunerCenterHz(voice)` is always the single-channel
     park (`voiceMin − 5000 + 1`). Do not pass CC into
     `getCenterFrequency` for the follow LO.
  2. Keep CC on the same tuner only when
     `isTunedFor(voiceParkCenter, {voice, cc})`. Otherwise one-RTL
     physical retune (pause CC) to that same voice park.
  3. Same-call hops use the voice park, not `{cc, voice}`.
  4. Two-channel `getCenterFrequency` stays in the header as the cited
     SDRTrunk routine; it is not the follow LO. 250 kHz stays gone.
  5. No hop/TTL/feed-gate change. Streaming DDC stays default-off.
- **Consequences:** 115315’s 421.975 follow would be `rfCenter≈421.96375`
  (offset ~11 kHz) with CC paused. 095450 voice 420.225 is still
  `≈420.21375` and CC still fits. File voicetest `--center` unchanged.

---

## DEC-0017 — Locked block-channelize hop is one superframe of fresh IQ

- **Date:** 2026-09-08
- **Status:** rejected
- **Evidence:**
  - Capture `20260907_123525` live: 80+280 ms jobs (360 ms IQ / 80 ms hop
    = 4.5× CQPSK), `dsp=72–134 ms`, `worker-busy`, CADENCE duty **0.34**.
    File voicetest skip=20000: duty **0.64**.
  - Tried `chunk=minFresh=0.360 overlap=0`. Voicetest 105622 skip=97334
    `PASS_CONTINUOUS_AUDIO duty=0.685` → `PASS_PARTIAL_AUDIO drop=D
    duty=0.35`. 123525 skip=20000 duty 0.64→0.325. Independent 360 ms
    eyes lose DEC-0007 overlap catch-up.
  - Streaming DDC still fails 105622 duty 0.095 (DEC-0014 re-measured).
- **Decision:** Do not ship 360 ms fresh-only block hops. Restore
  DEC-0009 80+280. Next live hole is worker-busy skipping IQ, not hop
  size. Isolation (L2) and streaming-DDC-off unchanged until DEC-0018.

---

## DEC-0018 — Streaming DDC must not jump the dibit lattice to RF-sample time

- **Date:** 2026-09-08
- **Status:** accepted
- **Evidence:**
  - DEC-0014: `SDR_TOWN_P25_STREAMING_DDC=1` on 105622 skip=97334
    `PASS_PARTIAL_AUDIO drop=A duty=0.095 emptyWindows=80/89`. First hops
    extracted; later hops `p2bursts=0`. Locked hops were already 80 ms
    overlap 0 (not the 20260830 40 ms slice).
  - `decodeP25VoiceAudioBlock` maps `iqStartAbsolute * 6000 / inputSr` and
    calls `alignPhase2AbsoluteDibitCursor` before `processIq`. Streaming
    DDC FIR/resampler output lags that RF-time prediction. The old
    streaming branch treated a small forward gap as harmless and **set
    `m_phase2StreamDibits = chunkStartAbsolute`**, jumping the sticky
    HDQPSK lattice off the actual dibits. Block-channelize overlap still
    needs that rewind (DEC-0006).
  - DEC-0017 360/0 independent block eyes failed 105622 duty 0.35: overlap
    CQPSK is required unless the Costas/framer stream is truly contiguous.
- **Decision:**
  1. When streaming DDC is on, do not align the dibit cursor from input-IQ
     sample indices. Contiguous hops append actual DDC/CQPSK dibits.
  2. If align is still called, a forward gap ≤ 8 bursts must **not** jump
     the cursor; leave the actual dibit count. Larger gaps still reset
     (retune / dropped RF).
  3. Re-measured 2026-09-08 after this skip: 105622 env=1 still
     `PASS_PARTIAL_AUDIO` (duty 0.16 at 80 ms, 0.09 at hopms=160, 0.045 at
     hopms=360). Default-on stays off (DEC-0014). The jump was real; it was
     not the whole eye-loss. Block 80+280 remains the proven file path.
- **Consequences:** Block 80+280 (DEC-0009) unchanged. Isolation (L2)
  unchanged. Env=1 becomes the experiment to re-measure, not a hop/TTL
  guess.

---

## DEC-0019 — Block CQPSK hint that framed Phase 2 stops the grid

- **Date:** 2026-09-08
- **Status:** accepted (hard early-stop only; cand=3 rejected)
- **Evidence:**
  - SDRTrunk `P25P2DecoderHDQPSK.receive`: one Costas + Gardner stream;
    `main()` feeds 2048@25 kHz ≈ 82 ms. Two `P25P2AudioModule`s filter by
    `message.getTimeslot()` — no companion-louder mute, no per-hop CQPSK
    re-search.
  - Our block-channelize path clears Costas every hop but keeps
    `m_blockCqpskHint`. Capture `20260908_060221`: dsp median **161 ms**
    on 80 ms fresh (14% ≥360 ms), **100** worker-busy, CADENCE talk median
    duty **0.30**, emit islands median length 1. Same class as 053241.
  - File voicetest of 060221 TG 30302 slot 1 skip=128500 center=420.21375:
    `PASS_CONTINUOUS_AUDIO duty=0.922` — IQ has continuous voice; live
    hole is drop **D**, not missing RF.
  - Soft-evidence early-stop trial: 105622 duty **0.645→0.305**, 041716
    **0.87→0.5**. Rejected (20260729 wrong-hint freeze).
  - Hard early-stop + file cand=16: 105622 duty **0.645** (kept).
  - Hot cand=**3** after speak (voicetest mirror of live): wall 17→7 s but
    105622 duty **0.645→0.055** drop=**A**. Same eye-loss class as DEC-0020.
    Cand=3 rejected; live hot stays **8**.
- **Decision:**
  1. After evaluating the block CQPSK hint, if it already produced
     **hard** Phase 2 lock evidence this window, set `stopCqpskSearch`.
  2. Do **not** cap hot CQPSK candidates at 3 after emit.
  3. Do not default-on streaming DDC (DEC-0014). Do not change
     hop/TTL/overlap (DEC-0009). Do not reopen DEC-0012 hop-wide mute.
- **Consequences:** Live drop D still open (T-0010). Shrinking eyes (DEC-0020)
  and cand=3 both buy wall clock by destroying extract. Next measured path
  must keep duty ≥0.65 while cutting per-hop DSP another way, or fix
  streaming DDC eye loss (DEC-0014/0018).

---

## DEC-0021 — Streaming lock-only cand=1 is premature on our Costas

- **Date:** 2026-09-08
- **Status:** rejected (insufficient; eye dies with cand=16 too)
- **Evidence:**
  - env=1 105622: after first emit, cand=1 → wrong-slot / p2bursts=0.
  - Disabling lock-only (always hot cand=16): window 5 still wrong-slot;
    windows 6+ stuck `p2bursts=1 p2vcw=0`. Duty **0.01**. Not cand=1 alone.
  - Transition window 4→5 is also **160 ms → 80 ms** fresh. Windows 2–4 at
    160 ms extracted voice; 80 ms sustain did not.
- **Decision:** Do not treat lock-only disable as a fix. Next trial is
  DEC-0022 (160 ms streaming sustain). Restore prior lock-only code shape
  only if a later DEC needs it; leave disabled while measuring 160 ms.
- **Consequences:** Streaming still not default-on (DEC-0014).

---

## DEC-0030 — Active rolling clamp must honor DEC-0023 4.0 s (not 2.048 s)

- **Date:** 2026-09-09
- **Status:** accepted (pending live measure)
- **Evidence:**
  - Capture `20260909_060036` (~392 s gapless, SNR summary ~6.8 dB, DEC-0029
    exe). Eyes 80+280. Quiet-returns down (DEC-0029 held). Still sparse voice.
  - CADENCE: emit>0 only **10**/215 s; max duty **0.639**; drop **A** 209;
    worker-busy **216**. Total speaker PCM ~**2.24 s**.
  - TG **10301** @ 421.975: cold `context=0` emit=32 duty **0.639**, then
    rolling stuck at exactly **4194304** samples while worker-busy; all later
    hops `p2vcw=0` / drop A / bridge top-ups.
  - **File voicetest** of the same IQ (skip≈261000, slot 0, center 421.96375):
    `PASS_CONTINUOUS_AUDIO duty=0.705` (5.64 s audio / 8 s span). Live silence
    after the first island is starvation, not missing RF.
  - Root: DEC-0023 set `kP25Phase2VoiceDecodeActiveRollingSeconds = 4.000` but
    both GUI/CLI rolling clamps still used `4194304` (= **2.048 s** @ 2.048
    MHz). Soft-trim could not keep the 4 s window DEC-0023 required.
- **Decision:** Route live rolling sizing through
  `p25Phase2VoiceRollingMaxSamples` with hard cap **16 s** (4× active per
  DEC-0023). Eye/emit backlog allowance matches the 4.0 s active window (was
  1.2 s / 4194304). Do not invent hop sizes, reopen streaming default-on, or
  post-emit cold escalate.
- **Consequences:** Live prove = after a cold emit island, rolling may exceed
  4194304 and CADENCE must keep extracting (not cliff to duty 0). Re-check
  105622 when available.

---

## DEC-0029 — Clear-trusted follow hold + structure exits coldAcquire

- **Date:** 2026-09-09
- **Status:** accepted (pending live measure)
- **Evidence:**
  - Capture `20260909_053448` (~308 s gapless, SNR ~17 dB, DEC-0028 exe).
    Eyes OK (0× 40 ms; 1365× 280 ms). **0** ReturnEncrypted.
  - Operator: very little / sparse voice — one small emit every few minutes.
  - CADENCE: emit>0 only **16** of **176** s; **0×** duty≥0.65. Drop **A**
    dominant (162), **D** (12). Block mostly `no-vcw-from-live-window`.
  - Timeline TG **30302** @ 421.225: cold mega-eye finds VCWs; emit island
    (emit=8 duty **0.151**); then empty eyes; at **+5 s**
    `ended or went quiet` releases traffic source while `callClearTrusted=yes`;
    next grant cold-rearms `context=0` / generation++ → another sparse island.
  - Follow SM: after emit, `kSpeakerImmediateGraceMs=2500` required current
    traffic structure; empty eyes dropped speaker grace, then
    `activitySilenceLimitMs=3500` fired ReturnActivityGone. Clear-trusted
    calls need the full 40s speaker grace (field 032428) without a live VCW
    window, plus 15s activity silence (same as
    `kP25Phase2ClearTrustedUnacquiredDwellStealGraceMs`).
  - DSP: emptyEye med ~67 ms (DEC-0028 helped); structureNoVcw med **~484 ms**
    — coldAcquire still required target VCW, so structure-only peaks kept
    burning 240/64.
- **Decision:**
  1. Clear-trusted / traffic-audio-open holds: full 40s speaker grace without
     current VCW/structure; activity silence 15s (not 3.5s).
  2. Exit `coldAcquireJob` once sustain has sf+mask ≥4 (epoch-grade
     structure), not only target VCW / hard acquire.
  3. Same-call metadata commit must not wipe traffic-proven clearKnown on
     unknown OP=0x02 grants (mirror in-place follow).
- **Consequences:** Re-measure 105622 ≥0.645. Live prove = no quiet-return
  within ~15s of clear emit while clearTrusted (T-0010 / 053448 pattern).
  Do not reopen streaming default-on, hot cand=3, or post-emit cold escalate.

---

## DEC-0028 — Never cold-escalate CQPSK after first emit (remove emptyStreakReacq)

- **Date:** 2026-09-08
- **Status:** accepted (pending measure)
- **Evidence:**
  - Capture `20260908_115603` (~487 s gapless, SNR ~17.8 dB, DEC-0027 exe).
    Eyes OK (0× 40 ms). 0 ReturnEncrypted.
  - Operator: first emit perfect (response to missed prior), then rest of
    conversation unheard.
  - Timeline TG **30302** @ 421.225: 21:58:53 emit=28 duty **0.553** (good);
    next workers dsp **605 / 470 / 481 / 472 ms** on structure/wrong-slot;
    then drop **B** (tv=16 fed=0) on companion-louder mixed window (DEC-0012
    isolation — correct). CADENCE peak still **0.599**; 0× ≥0.65.
  - structureNoVcw still med **465 ms** after DEC-0027 — emptyEye streak≥2
    armed cold 240/64 on the *prior* hop; the next structure/voice hop ran
    that cold grid. emptyEye itself is ~102 ms on hot.
  - `emptyStreakReacq` required `hadSuccessfulEmit`, so it **only** fired
    post-emit (pre-emit already uses `coldAcquireJob`). DEC-0026/0027 could
    not make post-emit cold safe.
- **Decision:** Delete post-emit `emptyStreakReacq` cold path. After speak,
  always hot cand=8 / 120 ms (DEC-0019). Soft mask rehunt + coldAcquireJob
  remain. Do not change hop geometry / TTL / DEC-0012 companion-louder bar.
- **Consequences:** Re-measure 105622 ≥0.645. Live prove = CADENCE after
  first emit without 400+ ms structure/opp hops (T-0010).

---

## DEC-0027 — Cold CQPSK escalate only on true emptyEye (not structure/opp)

- **Date:** 2026-09-08
- **Status:** accepted (superseded hole: DEC-0028 on 115603)
- **Evidence:**
  - Capture `20260908_112922` (~483 s gapless, SNR ~14.4 dB, DEC-0026 exe
    mtime after BN-0015). Eyes OK (0× 40 ms; 2805× 280 ms). 0 ReturnEncrypted.
  - CADENCE: **0** seconds ≥0.65; peak **0.639** drop=D. worker-busy **417**,
    wrong TDMA **141**. Talk emit>0 median duty **0.318**.
  - DSP by eye class (112922):
    - emptyEye (b=0,tv=0): med **101 ms**, 0× ≥200 ms
    - oppOnly: med **202 ms** (DEC-0026 helped vs prior p90 burn)
    - **structureNoVcw** (b>0,tv=0,ov=0): n=61 med **462 ms**, 40× ≥400 ms
    - target>0: med **241 ms** (worse than 110146 202 ms) — starved by above
  - Soft mask-epoch rehunt already exists for structureNoVcw
    (`p25Phase2StructureNoTargetVoiceWindows >= 3` →
    `invalidatePhase2StickyMaskEpoch`). Cold 240/64 was stacked on top.
  - `emptyStreak >= 3` alone also cold-escalated during opp/structure silence
    (re-opened DEC-0026 hole without needing structureNoTarget).
- **Decision:** `emptyStreakReacq` = hadEmit && emptyEye && emptyStreak≥2 &&
  !oppositeSlotOnlyEye. Remove structureNoTarget and
  StructureNoTargetVoiceWindows from the cold CQPSK path. Keep soft mask
  rehunt. Keep hot cand=8 (DEC-0019). Do not change hop geometry / TTL.
- **Consequences:** Re-measure 105622 ≥0.645. Live prove = new GUI CADENCE
  (T-0010). StructureNoVcw dsp should fall toward hot budget (~100–200 ms).

---

## DEC-0026 — Opposite-slot-only eyes must not cold-escalate CQPSK

- **Date:** 2026-09-08
- **Status:** accepted (superseded hole: DEC-0027 on 112922)
- **Evidence:**
  - Capture `20260908_110146` (~445 s, DEC-0025 exe): eyes OK (0× 40 ms;
    2472× 280 ms). No `ReturnEncrypted` (DEC-0025 held). Companion-louder 0.
  - Only **12** CADENCE ok seconds all session. Dominant live: drop **A/D**,
    `worker-busy` **382**, wrong TDMA **238**.
  - Best IQ (TG **20202** RID 0x1F95EB @ 421.975): file
    `PASS_CONTINUOUS duty=0.85`; live **5** ok seconds, worker-busy 54,
    wrong-TDMA follow ×69. Wrong-slot worker hops dsp med **~173 ms**
    p90 **~434 ms**.
  - TG **30017** @ 420.225: file duty **0.74**; SNR p10 **13.3**; live
    talkMed **0.303** / dsp med **203 ms**.
  - Hot path after emit treated `structureNoTarget` (bursts>0, targetVcw=0)
    + empty feed streak as cold reacq (**240 ms / 64 cand**) — that is
    normal opposite-slot TDMA silence, not a lost eye.
- **Decision:** Exclude opposite-slot-only eyes (`Phase2WrongSlot` diag or
  oppVcw>0 with targetVcw=0) from `structureNoTarget` cold escalate. Keep
  DEC-0019 hot cand=8; do not reopen cand=3. Do not change hop geometry.
- **Consequences:** Re-measure 105622 ≥0.645. Live prove = new GUI CADENCE
  (T-0010). Weak-RF calls (SNR p10 ~5) remain hard. **112922:** opp-only
  improved, but structureNoVcw + bare emptyStreak≥3 still cold-burned
  (DEC-0027).

---

## DEC-0025 — Clear→Encrypted latch needs MAC/PTT bar (match follow trusted ESS)

- **Date:** 2026-09-08
- **Status:** accepted (110146: zero ReturnEncrypted)
- **Evidence:**
  - Capture `20260908_103955` after DEC-0024: `context=81920` **0**; 280 ms
    eyes held. Operator 50/50; same TG **20202**, different RIDs.
  - RID **0x1F83FF** @ 420.725: live choppy; file voicetest duty **0.46**
    `PASS_PARTIAL` / no voice sync. Ring SNR p10 **7.3 dB**.
  - RID **0x1F95EB** @ 420.225: live better (7× CADENCE ok); file
    `PASS_CONTINUOUS duty=0.83` essEncrypted=no. SNR p10 **12.8 dB**.
  - Same “good” call ended `ReturnEncrypted` at 20:42:17 while last follow /
    DEEP DIAG still `p2ess=clear` / `encrypted=no` and PCM emit 18 ms prior.
  - Follow SM `trustedEncryptedEss` already requires `phase2MacCrcValid > 0`.
    Security-gate `thisWindowObservedEncrypted` could Clear→Encrypted latch
    (then `grantProvesEncrypted`) without that MAC bar.
- **Decision:** Treat this-window target ESS encrypted as latch/follow proof
  only with target/any MAC CRC **or** PTT-derived security state — same bar
  as `trustedEncryptedEss`. Log which ReturnEncrypted clause fired. Do not
  change hops/settle/DEC-0024. Do not invent TTL.
- **Consequences:** Re-measure 105622 ≥0.645; re-file both RID windows; live
  prove = new GUI capture (T-0010). Weak-RF RID remains drop D until Costas
  continuity improves (not this DEC).

---

## DEC-0024 — Backlog catch-up must keep DEC-0009 overlap (280 ms), not 40 ms

- **Date:** 2026-09-08
- **Status:** accepted (file 105622 ok; live 103955 proves eye fix)
- **Evidence:**
  - Capture `20260908_101644` (desktop exe after DEC-0023): soft-trim
    `context=573440` dominant (~1000 hops); old 80 ms protect almost gone.
  - Operator: start/middle BAD; last voice ~90% good.
  - First clear TG **30302** slot 0 @ 420.225: cold 720→280 ms eyes, then
    from 20:16:59.378 for ~8 s every hop was `context=81920` (**40 ms** =
    `kP25Phase2VoiceDecodeBacklogCatchUpOverlapSeconds`). CADENCE dutySec
    **0** / drop **A** / `diag=no voice sync` until 20:17:07 when eyes
    returned to 280 ms and duty briefly hit **0.681**.
  - Late TG **10330**: almost all hops `context=573440` (148 vs 2–3 smaller)
    — matches the “last voice good” report.
  - SDRTrunk `P25P2DecoderHDQPSK`: continuous Costas+Gardner traffic source;
    no shrink-context catch-up when the worker lags.
- **Decision:** Backlog catch-up may still take **120 ms fresh / 80 ms
  minFresh**, but overlap is **280 ms** (DEC-0009), not 40 ms. Do not
  lengthen `kP25Phase2PostArmSettleMs` (80) without a settle-specific
  measure — 101644 cold eye already produced audio before the 40 ms spiral.
- **Consequences:** Re-measure 105622 ≥0.645. Live prove = new GUI CADENCE
  on desktop exe (T-0010). Do not reopen 80/0 or streaming default-on.

---

## DEC-0023 — Rolling soft-trim must protect DEC-0009 sustain overlap (280 ms)

- **Date:** 2026-09-08
- **Status:** accepted (superseded hole: DEC-0024 on 101644)
- **Evidence:**
  - Capture `20260908_095936` (desktop `SDR_Town.exe`, TG 30302 slot 0 @
    421.225): promising start (dutySec up to **0.60**, gate=emit,
    companion-louder **0**), then collapse. Talk median dutySec **0.40**
    still drop **D**; cliff ~20:00:18 → drop **A** / return-to-CC.
  - Worker eyes: early **360 ms** (`fresh=163840 context=573440`), then
    dominant **160 ms** (`context=163840` = 80 ms) — 110 submitted hops.
    DEC-0009 already measured 80 ms overlap → duty 0.40/0.51.
  - Rolling backlog hit **2.6–2.9 s** while worker-busy; soft-trim constant
    `kProtectedOverlapSamples = 163840` (**80 ms**) matches the clipped
    context. Comment in `RollingIqWindow::append` already names this as the
    20260901_042425 stutter path; protect depth was never raised to DEC-0009.
  - Voice-period ring SNR median **16.7 dB** (summary 5.2 dB is late CC).
    Not an RF fade for the good-start window.
- **Decision:** Soft-trim protected pre-roll is **280 ms** (573440 samples @
  2.048 MHz = DEC-0009 speaker-sustain overlap), not 80 ms. Active rolling
  window while speaker-live is **4.0 s** (was 2.0 s) so soft-trim can keep
  that overlap under the lag seen on 095936. Hard-cap must not jump the
  decode cursor while undecoded RF is protected; emergency cursor jump only
  above 4× rolling. Do not invent a new hop size. Do not reopen DEC-0012.
- **Consequences:** Re-measure 105622 block duty (must stay ≥0.645). Live
  prove = new GUI CADENCE on desktop exe after this change.

---

- **Date:** 2026-09-08
- **Status:** rejected
- **Evidence:**
  - env=1 after 160 ms sustain: 105622 duty **0.125**, 073304 **0.22**,
    041716 **0.365** (was ~0.01 / 0.125 / 0 with 80 ms). Better, still
    far below PASS_CONTINUOUS 0.65. Wall ~7 s / 8 s span.
  - Root: after first emit, sticky HDQPSK still goes wrong-slot /
    `p2vcw=0`; longer slices only delay the starve.
- **Decision:** Revert to DEC-0014 80 ms streaming slices. Do not default-on
  streaming. Block 80+280 remains the file-proven path.
- **Consequences:** Live drop D needs a Costas/channelize fix that keeps
  duty, not another hop-size guess.

---

- **Date:** 2026-09-08
- **Status:** rejected
- **Evidence:**
  - Capture `20260908_082235` (stock 0.2.51): CC OK; talk median dutySec
    **0.189**; worker-busy; dsp median **185 ms** on 80+280 eyes; drop **D**.
  - File voicetest with DEC-0009 80+280 waits (~16.6 s wall / 8 s span) so
    duty stays 0.645 while live cannot wait.
  - Trial: after emit, block eyes **80 ms fresh + 0 overlap** (voicetest
    `speakerSustainContextMs=0` + live chunk planner same). Wall **2.8 s**
    (fast enough) but 105622 duty **0.645→0.055** drop=**A**; 073304
    **0.045**; 041716 **0.085**; 060221 **0.09**. Same class as streaming
    DDC eye loss (DEC-0014) / independent-eye DEC-0017 — not a live win.
- **Decision:** Do **not** shrink block sustain eyes to 80/0. Keep DEC-0009
  80+280. Live drop D needs either (1) faster DSP on 360 ms eyes (DEC-0019
  path) measured on the **desktop** GUI, or (2) streaming DDC that keeps the
  CQPSK/dibit eye (DEC-0014/0018 still open). Do not invent TTL/grace/PLC.
- **Consequences:** Reverted. T-0010 remains open.

---
