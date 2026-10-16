# SDR Town and FUBAR human usability audit

Date: 2026-10-16. DEC-0224 / T-0144. Implementation follow-up: T-0140.

The current applications expose substantial capabilities, but the default UI
still asks beginners to understand receiver internals before completing simple
tasks. The first priority is truthful, working controls and visible radio/audio
state. The second is a task-focused layout. A cosmetic restyle alone will not
resolve the problems below. Preserve the accepted P25/DSP and shared ownership
controllers; this audit does not authorize a new signal-processing path.

## Scope and evidence

- Public applications inspected: SDR Town 0.2.139, runtime source `a45b200`, and
  FUBAR 1.1.52, runtime source `2b419ec`. Source audit started at Town `7efab0a`
  and FUBAR `12b4648`; the later commits are documentation-only.
- Actual Town GUI: isolated no-RF settings, 1100x800 window, Listen, Satellite
  workspace, Inmarsat, docking and SSTV entry/source presentation. Native
  accessibility names were inspected, not inferred from screenshots.
- Actual FUBAR website through the packaged DLL and actual Town GUI: all eight
  task pages, 390x844 mobile and 1280x800 desktop layouts, control acquisition
  and release, expiry warning, invalid-frequency validation, idle Listen and
  keyboard tab navigation. No radio transmission, rotor motion or bias power.
- Source review: native main/device/audio menus, assignments, all specialty
  controllers, companion pages and native FUBAR startup/settings. Source-only
  findings below are not claims of physical-hardware reproduction.
- The existing actual-app pair harness also exercised authentication, bounded
  named session routes, invalid device selections and no-RF ownership behavior.
  Local evidence is under `D:/SDRTown-UIAudit-20261016/paired`; raw logs and
  operator content are not committed with this report.
  The 900-second pair completed with both processes exiting normally (code 0),
  all functional assertions passing and no real RX started. The website then
  showed Website unreachable and disabled tuning controls. The separate native
  walkthrough process also exited; no test applications were left running.
- Native FUBAR's window was not launched against the operator profile: its startup can
  load settings, prune recordings and automatically start capture. Safe isolated
  preview remains T-0142. The paired harness ran FUBAR in web-only CLI mode.
  Screen-reader speech, 150/200% DPI, physical RSPdx,
  real multi-radio handoffs and novice user trials remain qualification gaps.

Evidence keys: **Observed** = actual released UI; **Source** = executable code
path inspected; **Proposal** = recommended behavior, not implemented here.
Priorities refer to operator impact, not a claim of a security vulnerability.

## Findings

### HUI-01 High - Enabled commands that perform no action

**Source.** `MainWindow.cpp:13932` connects menu Start Smart Scan to an empty
lambda; `:13953` and `:13954` do the same for Mute All Outputs and Test Tone
(All); `:14095` does so for Preferences. The separate Smart Scan panel button
at `:410` starts eligible receivers rather than implementing that named scan.
Individual output-test buttons are a different, implemented path.

**Impact:** a user can invoke mute and still hear audio, or assume settings,
testing or scanning failed without any explanation.

**Proposal:** connect these to tested shared actions, or disable unsupported
commands explicitly. Do not rename receiver startup as scanning. A visible
global mute must have a truthful state and a tested reverse action.

**Gate:** exercise every menu command; each must cause a verified state change,
open the correct dialog, or be unavailable with a reason. Mute must affect the
intended outputs without destroying decoder state.

### HUI-02 High - Radio identity and receive state are not primary information

**Observed + Source.** Listen shows Tune and Receive without an adjacent radio
selector, confirmed hardware state or Stop control. The receiver table is in a
dock, and Saved Frequencies is raised by default (`MainWindow.cpp:5175`,
`WorkspaceLayout.cpp:78`). Radio 0 in that table is an index, not an operator
name. Highlighting a Device Manager row becomes the monitor selection through
Apply (`MainWindow.cpp:8598`).

**Proposal:** persistent task header containing the radio's friendly name and
stable identity, task, actual frequency/mode, Receiving/Starting/Stopped/Failed
state, and Start/Stop. Put hardware setup behind a secondary action. Do not use
the displayed frequency or existence of a waterfall as proof of reception.

**Gate:** first-time reception, unplug, missing device, slow start, busy device
and failed start all name the affected radio and offer an appropriate recovery.

### HUI-03 High - Audio output settings do not preserve visible truth

**Source.** The output dialog initializes every per-device slider to 80%
(`MainWindow.cpp:8169`). Apply and individual Test then write these values back
(`:8132` and surrounding `applyAudioSelection`). Master volume changes live;
the device selection/levels require Apply despite the blanket live-change text
at `:8088`.

**Proposal:** populate actual output values and show exactly which controls are
live. Distinguish station/output gain from browser listening volume. Testing
one output should not unexpectedly enable it permanently or reset other levels.

**Gate:** set different device volumes, reopen, inspect, test one output and
close. Unrelated output levels and routing must remain unchanged.

### HUI-04 High - Specialist tasks are secondary to unrelated Listen controls

**Observed + Source.** At 1100x800, selecting Satellites / Aircraft left roughly
the first 500 pixels occupied by the main Listen spectrum and controls, while
the selected Inmarsat task occupied a small scrolling area. It showed 100 MHz
Listen above 1546 MHz Inmarsat settings. This is competing task context, not
proof of an RF tuning error.

`MainWindow.cpp:236` and `:5162` retain the central Listen widgets;
`WorkspaceLayout.cpp:65`, `:98` and `:109` put specialty views in bottom docks
and change dock visibility/height only. Satcom adds another scroll area at
`SatcomScannerWidget.cpp:201`. Float is available, but a newcomer must discover
and rearrange the workspace before the selected task becomes usable.

**Proposal:** the chosen task gets the central working surface. Preserve a
compact global radio/audio strip and an optional spectrum panel. Keep task
Start/Stop/status visible while scrolling results. Preserve expert dock layouts.

**Gate:** at 960x720 and 1280x800 the selected task's source, primary action and
current state are visible together; nested page-level scrolling is eliminated.

### HUI-05 High - Native accessible names do not identify essential controls

**Observed + Source.** Frequency, mode, bandwidth, gain, squelch and volume were
announced in the native accessibility tree as variants of Receiver Controls,
rather than their distinct labels. Standalone label construction appears at
`MainWindow.cpp:448`, `:506`, `:524`, `:546` without field-specific accessible
names or buddy associations.

**Proposal:** associate each label and field; provide distinct names and units,
logical focus order and visible focus. Do not rely on surrounding visual text.

**Gate:** keyboard-only and screen-reader users can identify, edit and recover
each essential field without guessing its position. Current web task tabs
already pass arrow-key navigation and roving-tabindex checks; preserve that.

### HUI-06 Medium - Mobile basic tuning is buried under advanced settings

**Observed.** At 390x844, after a validation message, frequency appeared around
document y=782 and Tune & receive at y=1326. Bandwidth, LPF, gain, station
volume, squelch and direct sampling all precede the primary action. There was
no document-wide horizontal overflow in this test; vertical task length is
the problem. `FUBAR/src/web_server.cpp:340` onward constructs this sequence.

**Proposal:** Radio, Frequency, Mode and Tune form the short primary workflow.
Advanced RF/audio settings expand separately. Keep local Listen/Mute available
without making a listener acquire station control. Compact the shared header.

**Gate:** a phone user can select a radio, enter a frequency and find Tune
without traversing advanced fields. All expert settings remain discoverable.

### HUI-07 Medium - Immediate and pending settings share one form

**Observed + Source.** The web has both Switch mode now buttons and a Mode on
Tune selector. Direct sampling posts immediately (`web_server.cpp:1900`), while
volume/gain/filter values are part of the Tune payload (`:1929` onward).
The labels distinguish the two mode actions, but the user must learn mixed
application rules across otherwise similar controls.

**Proposal:** one predictable tune form with pending-change feedback, plus
explicitly live local audio controls. Put quick-mode actions in an expert view
or make their immediate effect unambiguous. Preserve draft values during polls.

**Gate:** editing a field, switching tasks and receiving a status update never
silently applies or discards a draft. Applying a tune updates the actual-state
summary only when confirmed by the controller.

### HUI-08 Medium - Workflow assignment labels encode an obsolete index rule

**Source.** `MainWindow.cpp:13905` labels index 0 Main Listen + P25 control and
disables assignment editing there, regardless of the current Listen selection.
This is not derived from the stable device identity/current owner. An idle
single-radio setup can therefore have no editable assignment row.

**Proposal:** show current owners and explicit assignments by stable identity.
Use human names such as Roof antenna and Desk RTL, with serial/model available
for disambiguation. Separate the selected physical radio from decoder sessions.

**Gate:** reorder enumeration, select a nonzero Listen radio, unplug/replug and
use a sole idle radio. Labels and available assignment actions remain correct.

### HUI-09 Medium - Selecting a receiver does not define control scope

**Source.** The receiver table is read-only (`MainWindow.cpp:247`); selecting a
row does not target the main receiver controls. Main squelch updates every
active receiver (`:4834`). Table Receiving derives from the receiver active
flag (`:280`), not independently confirmed hardware readiness.

**Proposal:** visible selected-receiver context and scoped controls. Label truly
global actions explicitly. Treat requested/starting/receiving states separately.

**Gate:** with two mocked receivers, altering one never changes the other
unless the user invokes an explicitly global command.

### HUI-10 Medium - Slow Inmarsat start can block feedback and loses a cause

**Source.** The GUI click handler calls `engine.start(true)` synchronously
(`InmarsatWidget.cpp:375`); operational stream polling can sleep/wait for up to
ten seconds (`InmarsatEngine.cpp:335`, `:535`). On an earlier start failure,
`:525` overwrites the device error with Could not start selected Inmarsat
receiver. This identifies a slow/no-IQ path, not every startup and not the
established root cause of hosted ISS-0117.

**Proposal:** cancellable, generation-fenced startup with visible Opening /
Tuning / Waiting for samples stages and preserved device errors. Do not solve
this by extending UI timeouts or retrying an ambiguous start command.

**Gate:** delayed/no-IQ mock device keeps the UI responsive, Cancel leaves no
live session, and the user sees the original error with a useful next action.

### HUI-11 Medium - Capture status can outlive the recording

**Source.** Manual start sets IQ: recording and button state
(`MainWindow.cpp:4924`); manual stop restores them. Automatic low-storage stop
at `:13198` calls `stopLiveIqCapture`, logs and displays a temporary status-bar
message, without updating those persistent local widgets.

**Proposal:** bind capture UI to capture lifecycle state, not button callbacks.
Keep Stopped - storage limit and Open output available after the transient
notice disappears. Display output folder, duration and storage limit clearly.

**Gate:** simulated low disk space, write failure, external stop and normal
stop all leave consistent labels, buttons and saved-output information.

### HUI-12 Medium - SSTV exposes irrelevant source context

**Observed + Source.** The Recording view shows a disabled receiver at 100 MHz
and disabled RF frequency 145.8 MHz beside the file controls. Dense live-radio
guidance remains above Decode. The fields are correctly disabled; this is not
an enabled-control or tuning bug. See `MainWindow.cpp:11355`,
`SstvWindow.cpp:77`, `:109`, `:335`.

**Proposal:** separate Live radio and Recording source views. Show only the
selected source's controls, concise current status, image gallery and save
folder. Keep image Auto as the normal default; move protocol details into help.

**Gate:** users can start live reception and decode a file without confusing
recorded audio, receiver tap, physical radio, RF mode and image mode.

### HUI-13 Medium - Current setup help includes obsolete pairing instructions

**Source.** README's pairing quick start at `README.md:419` still calls the
current release 0.2.96, refers to FUBAR 1.1.41 and says the web lacks an RF
selector at `:425`, despite the current working released selector. The pairing
document opens with historical versions before current evidence. Main Help
offers a design-document location rather than task-oriented product help
(`MainWindow.cpp:14112` onward).

**Proposal:** a short current setup guide first, with matched package/DLL,
radio selection, first sound and remote-control checks. Move historical release
notes below a clearly historical heading. Link contextual task help from UI.

**Gate:** a fresh installation follows the guide without obtaining an old DLL,
searching source files or needing the developer to explain terminology.

### HUI-14 High - Escape can bypass rotator dismissal cleanup

**Source.** `AntennaControlWindow.cpp:136` places stop and meter-disconnect only
in `closeEvent`. The panel persists and uses a shared controller. Qt's Escape
path calls reject/done; `QDialogPrivate::close` installs a CloseEventEater,
preventing the subclass close handler on that path. This was checked against
[Qt 6.7.3](https://github.com/qt/qtbase/blob/v6.7.3/src/widgets/dialogs/qdialog.cpp#L110)
and [Qt 6.11.1](https://github.com/qt/qtbase/blob/v6.11.1/src/widgets/dialogs/qdialog.cpp#L113),
not inferred solely from the absence of a reject override. No physical rotor
motion or hardware connection was tested.

**Proposal:** one idempotent dismissal cleanup invoked by all accepted close,
reject and shutdown paths, with motion disarmed independently of network-stop
delivery. Keep a physical emergency stop requirement; software is not one.

**Gate:** a mock controller verifies Escape, window close and app shutdown all
disarm and request stop once. Do not test this by moving real hardware.

### HUI-15 Medium - Opening another IQ recording can retain old tuning fields

**Source.** `MainWindow.cpp:9267` populates replay Center/Target only when zero.
The retained values override a new file's metadata at `:9746`. Opening file A,
then B, can leave the previous recording's center/target visibly configured.

**Proposal:** distinguish metadata-derived values from deliberate user
overrides. Loading another file refreshes derived values; retaining an override
must be explicit. Show capture center, target offset and recorded time together.

**Gate:** open two fixtures with different frequencies in sequence, play each
and confirm the right metadata/target reaches the replay controller.

### HUI-16 Medium - Aircraft radio startup and decoder enable are separate

**Source.** Local decoding defaults off (`AircraftMapWidget.cpp:83`) and
processing stays gated at `:217`; Tune 1090 can report ready independently.
The web similarly presents both Tune 1090 and Local ADS-B.

**Proposal:** Start local aircraft reception configures both intended actions,
or prominently says Receiving - decoding disabled and provides Enable decoding.
Internet tracks must never imply the local decoder is working.

**Gate:** fresh settings, network disabled and known Mode-S fixture produce an
unambiguous progression from stopped to receiving to decoded local tracks.

### HUI-17 Medium - Aircraft details and map interactions need a usable alternative

**Source.** The native aircraft popup uses a one-time snapshot
(`AircraftMapWidget.cpp:586`) and omits per-field ages/reference/source at
`:596`. Aircraft picking is mouse-driven (`:531`), and Inmarsat provenance
details depend on hover (`InmarsatMapWidget.cpp:158`). A partial RF identity
table is not a complete keyboard alternative for online/estimated map tracks.

**Proposal:** selectable track list and persistent details panel, updated with
validity/age/source. Provide keyboard navigation and named map controls. If a
popup is deliberately a snapshot, timestamp it clearly rather than presenting
its speed/altitude as continuously current.

**Gate:** select a track using only the keyboard; change/expire its data and
confirm details remain truthful. Test touch access without hover.

### HUI-18 Medium - Channel selection has inconsistent apply semantics

**Source.** Inmarsat band-plan selection applies on double-click
(`InmarsatWidget.cpp:231`), while watch selection populates the controls on
single-click (`InmarsatWatchUi.cpp:114`). A newly highlighted band-plan row need
not be the frequency subsequently used by Tune.

**Proposal:** a single selection consistently fills pending controls; explicit
Tune applies. Show the selected frequency/rate and actual tuned frequency
separately until confirmed. Double-click may be an expert shortcut, not the
only discoverable way to select a channel.

**Gate:** mouse, keyboard and touch selection all target the same requested
channel; no old channel is silently applied.

### HUI-19 Low - SSTV no-result status does not distinguish the cause

**Source.** `SstvWindow.cpp:310` reports No images detected; useful input and
discontinuity evidence at `:320` goes to diagnostics rather than the operator.

**Proposal:** distinguish no samples, waiting for a picture, unsupported image
mode and finished without a valid image only when evidence supports the state.
Offer the relevant source, recording or settings action. Do not invent an RF
failure explanation from a zero-image count.

### HUI-20 High and Medium - Companion native lifecycle and web outcome gaps

The [FUBAR-specific audit](https://github.com/blkph0x/FUBAR/blob/main/docs/AUDIT_20261016_FUBAR_UI.md)
contains exact anchors and acceptance cases. Principal source findings:

- **High:** native startup automatically starts capture; Refresh clears source
  selection and restarts (`FUBAR/src/app_window.cpp:301`, `:637`). Conditional
  input probing can pick a different source. First use should require explicit
  source confirmation; unattended auto-start should be a deliberate preference.
- **High:** device/channel/checkbox edits restart capture after Stop (`:387`),
  while threshold edits remain pending (`:407`). Preparing settings must not
  silently resume capture or recording.
- **Medium:** native status polling synchronously calls a DLL request with a
  2500ms timeout on the UI thread (`app_window.cpp:1188`,
  `sdr_town_bridge.cpp:92`). Keep local Stop responsive during remote failure.
- **Medium:** website-start failure clears enabled state, then presents Website
  off instead of the cause (`app_window.cpp:1128`, `:1086`). Preserve Failed,
  attempted port and Retry; do not offer an unrelated fallback URL as a live site.
- **Medium:** a lost command reply looks like failure without generic outcome
  reconciliation (`web_server.cpp:1866`). Distinguish rejected from unknown;
  perform bounded status readback, never an automatic duplicate mutation.
- **Medium:** Assigned radio and generated session IDs obscure the physical
  target (`web_server.cpp:2354`, `:2374`). Show resolved identity and use a
  distinct name for timed control access versus receiver workspace.
- **Medium:** recording-list rebuilds discard web focus/native selection
  (`web_server.cpp:488`, `app_window.cpp:899`). Update by stable clip identity.

These native consequences are source-confirmed paths, not native UI/hardware
reproductions. The uncertain initial browser queue click is not recorded as a
queue-admission defect. Existing admission tests remain valid.

## Behaviors worth retaining

- Web control lease ownership, remaining time, expiry warning and two-extension
  state are visible; releasing control disables mutation controls.
- Invalid frequency input produces a clear global and local error rather than
  silently changing the radio. The old error can remain while unrelated tasks
  are viewed; contextual invalid-field recovery is a further polish item.
- Desktop web task tabs support arrow-key navigation and correct selected/focus
  state. Mobile substitutes a labelled Task selector instead of overflowing tabs.
- Device selection, named sessions and control admission already exist. Improve
  their presentation rather than introducing another controller or bypassing
  ownership.
- Inmarsat maps label received, online and estimated positions. Real map tiles
  rendered in this walkthrough; no live RF aircraft positions were qualified.
- SSTV defaults to automatic image selection, has a persistent save folder and
  an output-opening action. Preserve these useful defaults.
- Expert docks, detailed diagnostics and multiple workflows are assets. The
  beginner view should simplify entry, not remove those capabilities.

## Proposed interaction model

Use the same task vocabulary in native Town and the companion: Listen, P25,
SSTV, Satellites, Inmarsat, Aircraft, Recordings and Analysis. Choose the final
labels with novice tests; do not make protocol names disappear for experts.

Each active task has one persistent header: task/session name, physical radio,
actual receive state, frequency/mode and Start/Stop. Its main surface contains
that task's results. Secondary controls live in collapsible Advanced sections;
logs and diagnostics remain available without dominating the starting view.

Choosing a task changes the view, not the radio. Starting it invokes the
existing ownership controller. A busy radio names its owner and offers a free
radio or an explicit, informed handoff. Closing a view must not silently change
the task's running state; that behavior is visible and consistent.

Separate three concepts everywhere: selecting the radio, running reception or
decoding, and hearing audio. A web listener's mute/volume must be distinct from
changing the station's shared audio output. Distinguish connection failure,
receiver stopped, no signal, waiting for a transmission, encrypted/muted and
audio output unavailable instead of collapsing them into Idle or no sound.

## Implementation order

1. **Trustworthy controls:** HUI-01/02/03/08/09/11/14/20. Fix dismissal safety,
   stopped-state capture, dead actions, visible ownership and current state
   before moving panels.
2. **Basic task shell:** HUI-04/06/07/12. Put the selected task first, use short
   start workflows and progressive advanced controls; retain Expert layouts.
3. **Accessible recovery:** HUI-05/10/13/15/16/17/18/19. Correct field names,
   focus/selection, replay metadata, responsive startup/cancel, useful errors
   and current task help.
4. **Acceptance:** novice scenarios, keyboard/screen reader, small/high-DPI
   windows, mobile browser, offline/busy/unplugged states and multi-radio
   ownership. Use the same controllers and current regression fixtures.

Implement these as reviewable increments, not a rewrite. Each UI patch must
name its affected workflow, preserve saved configuration or provide a migration,
and pass the relevant native/API/web tests. Shared audio/device changes require
the existing accepted P25 clear/encrypted/slot non-regression gates. No UI
redesign warrants changing vocoder, timing, encryption or slot decisions.

## Acceptance scenarios

| Journey | Required evidence |
|---|---|
| First sound | New settings, select named radio, tune station, see actual RX state, select output, mute/unmute and stop without developer assistance |
| P25 | Select radio/control channel, start monitoring, understand waiting/following/encrypted states; preserve existing clear replay output |
| SSTV live/file | Source-specific setup, automatic image mode, useful waiting/no-input state, completed images and saved-folder access |
| Inmarsat | Named radio/session, channel/watch setup, start/cancel/stop, lock and message status, map source/age, voice audio scope |
| Aircraft | Distinguish internet tracks from local 1090; radio/rate selection, no-data recovery, visible provenance and stop |
| Satellite pass | Location/orbit prerequisites, selected satellite/downlink, arm/disarm, radio availability and scheduled status without implying a decoder lock |
| Capture/replay | Output destination and duration, capacity failure, finalization, replay position, clear distinction between live and recorded data |
| Two radios | Two different tasks, re-enumeration, busy conflict and independent stop; no invisible cross-task retune/settings change |
| Remote listener/operator | Listen without control, queue/lease, expiry, local versus shared audio, preserved drafts, failed/ambiguous commands |
| Accessibility/layout | Keyboard completion, useful accessible names, visible focus, 960x720 and high DPI, 390px mobile, long device/session labels |

Completion means these journeys work and users understand them. Passing a
screenshot-size check or opening every dialog is not sufficient.
