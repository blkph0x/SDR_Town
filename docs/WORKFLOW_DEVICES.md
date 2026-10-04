# Workflow Devices (T-0103 / DEC-0181..0190)

## Universal contract

Every RF workflow must ultimately select its radio in the same way, independent
of demodulator or protocol. P25 and SSTV are examples, not exclusive features.
The common control layer uses a stable physical identity, a workflow instance
ID and a generation-bound lease. Different physical radios may run the same
or different workflows concurrently. One instance cannot retune another's
exclusive source. A read-only VFO/decoder may share a source only while its
frequency and bandwidth fit the source, without changing hardware configuration.
These RF ownership rules are separate from each stream's audio routing/mute.

The implemented ownership and controller-routing milestones are below. They
are not the complete migration to independently repeatable workflow instances.

## Operator controls

Open **Devices > Workflow Assignments** after discovering the radios. Stop a
radio before changing its reservation, choose its workflow, and save. Automatic
keeps existing allocation behavior. Several secondary radios may belong to
the same workflow pool. Disconnected radios' reservations remain saved.
Duplicate identities are rejected rather than matched to an arbitrary SDR.
The current primary Listen/P25-control radio is shown but not reassigned here:
its remaining index-based controller must be migrated before enabling that edit.

For SSTV, choose **Live RF**, select **Radio**, set **Frequency**, RF demodulation
and image format, then **Receive**. **Receiver tap** uses the selected logical
receiver; a named radio starts an exclusive worker-owned session. Stop/cancel closes
only that session's radio. Selecting a missing radio reports an error; it does
not substitute the primary. Hardware startup must succeed; synthetic/stub IQ
does not qualify as reception. Output/save-folder behavior is unchanged.

**Tools > Additional Decoder Window** opens another Morse or DTMF observer,
or a named SSTV session. Each SSTV session has its own radio/frequency, save
folder, worker and Stop/Cancel controls. Reopen the same name to reuse its
settings; names are case-insensitive. Up to 32 SSTV windows may be open, with
actual reception still subject to physical-device ownership and CPU capacity.
Closing a named window stops only that session. Hiding a window or switching
workspaces does not stop reception; satellite rendering timers pause while
hidden, but the engine and automatic-pass controller continue.

Automation can GET `/v1/sstv/sessions`, or POST it with `sessionId` to open a
window without starting RF. The live/finish/cancel endpoints also accept that
ID. Omitting it retains the original default session. Status includes all
open sessions and their selected source, frequency, visibility and busy state.
IDs contain 1-64 ASCII letters, digits, underscores or hyphens. Unknown IDs do
not cancel another session; invalid IDs are rejected. Automatically generated
output folders include a UUID so simultaneous jobs cannot reuse one directory.

**Receivers > Add Receiver** selects a radio, frequency, analog mode, bandwidth
and squelch. A live radio can host another receiver within its existing capture
span; this action never retunes it to accommodate an out-of-span receiver. The
receiver table shows the actual instances; Remove operates on the selected row,
not the last instance. The main and P25 receivers retain their own stop controls.
CW, DTMF and SSTV have the same logical-receiver picker. **Tools > RDS / CTCSS /
DCS** opens a read-only status window for any selected receiver; the main status
strip and repeater-control panel still belong to the primary Listen receiver.
Removing/reordering receivers cannot redirect an observer to a replacement.
Repeater dual-watch RF changes and its shared status are bound to the main
receiver identity (DEC-0183). A secondary NFM receiver does not inherit that
pair, even when it is the first active receiver or shares the same SDR.

Inmarsat, Satcom and Aircraft have stable-key radio selectors. A missing saved
radio stays visible as unavailable, rather than falling back to radio zero.
Stop an active workflow before changing its source. Satellite restoration uses
the exact lease and confirms the old frequency before resuming paused Listen;
failure leaves it stopped. Two satellite takeovers on different radios have
separate GUI records. Aircraft opens/closes its selected radio in its worker,
with distinct opening/ready/stopped states and an explicit Stop control.

Use the Satellite workspace's **Add receiver session** menu to open or reopen
a named receiver. Names use 1-64 ASCII letters, digits, underscores or hyphens;
case is normalized. Each tab has its own radio, watch list, IQ cursor, decoder,
message history, aircraft/map model and map-consent settings. Up to 16 named
sessions can be open. Saved tabs reopen idle; reception always requires Start.
Close joins that tab's worker and restores only its radio. Hide/tab navigation
keeps it running. Two sessions cannot claim the same exclusive radio, even
when both are Inmarsat. The shared Aircraft overview may aggregate validated
positions; each Inmarsat map uses only its session's received identities.

The authenticated `/v1/inmarsat/sessions` endpoint lists sessions with GET.
POST takes `sessionId` and `action`: `open`, `configure`, `start`, `stop`, `close`.
`configure` requires a stopped session and accepts a `config` object containing
`deviceStableKey`, `channelHz`, `mode`, `baud`, `playAudio` and/or `watch`.
Unknown fields/IDs are rejected. Opening never starts RF; dry-run blocks Start.
Existing `/v1/inmarsat/control`, status/messages/map endpoints and CLI commands
retain their original default session. New FUBAR session-selection UI is not
implemented by this backend endpoint. Diagnostic recording is unavailable while
multiple engines are live because the current collector identifies a source by
frequency; starting a second engine cancels an armed ambiguous recording.

The same menu opens named **Satcom** and **Aircraft** tabs (up to 16 of each).
Each Satcom tab owns its selected radio, scanner, pass planner/observer,
catalogue selection, decoder history, recordings/log directory and audio queue.
Named automatic pass capture must be armed again in the current run. Stop also
disarms it, so its timer cannot reopen the radio after an explicit stop.
Each Aircraft tab owns its selected radio, local Mode-S worker, track/CPR state
and network setting. Internet aircraft default off for a new named session.
Saved tabs reopen idle; missing radios never fall back to the primary. Switching
tabs does not stop either workflow. Close and application shutdown join workers.

Authenticated GET `/v1/satcom/sessions` and `/v1/aircraft/sessions` list those
controllers; POST requires `sessionId` and `action`. Both accept `open`,
`configure`, `stop`, `close`. Satcom also accepts `start`; configure uses a
`config` object with `deviceStableKey`, `lowHz`, `highHz`, `stepHz`, `bandwidthHz`,
`mode`, `squelchDb`, `monitorAudio`. Aircraft accepts `tune`, `local`,
`network-off`, `refresh`; configure/tune take top-level `deviceKey` and
`captureBandwidthMHz` (2-20). `local` takes boolean `enabled`.
Invalid/stale requests cannot address the default session. RF start/tune is
blocked in dry-run. Existing default routes/CLI remain unchanged; these new
backend routes do not implement a corresponding FUBAR session-picker UI.

The CLI uses `aircraft tune [device-index]` and `aircraft stop` for this same
scoped radio lifecycle; CLI tune is hardware setup, not a new continuous Mode-S
decoder loop. The GUI web aircraft tune accepts `deviceKey`, and status exposes
`radioBusy`, `radioReady` and that key. `/v1/sstv/live` accepts `deviceKey` and
`frequencyHz` through the same SSTV controls. Main status includes a
`workflowDevices` list with stable identity, reservation, owner and runtime state.

Assignments are stored atomically in `workflow_devices.json` in application
data. Failed loads block new controlled tunes/claims until explicitly repaired
through the assignment window. Logs record workflow, device index, lease ID,
generation, requested frequency, confirmed startup and rejected tune reasons.
They remain subject to existing diagnostics consent; no new recording uploads.

Hardware gain, PPM, antenna, bandwidth, AGC, direct sampling and bias-T controls
now reject changes to a radio held by another workflow, even when requested by
an older GUI/CLI/web route. Configure it before starting that workflow, or use
its exact session token in a controller. Idle reserved radios remain configurable.
Main gain operates on the selected Listen radio, not always radio zero. Command
rejection is displayed; the web status includes `controlBusy` for each radio.
Debug logs correlate accepted commands and completion time by command ID;
rejections include the operation and ownership reason.

A radio is reported as ready only after startup controls finish. Stop retires
the session first and drains already-admitted commands before hardware teardown;
an old command cannot arrive on a newly claimed session. This protects ordering,
not responsiveness to a permanently wedged native driver. Driver isolation and
fully asynchronous operator lifecycle remain open.

DEC-0190 fixes reproduced startup starvation with FIFO admission to the existing
live-driver lock. It does not enable parallel driver calls: a hung native call
can still delay the other radios. The mixed fixture runs two Aircraft workers,
one Satcom engine and one Inmarsat engine simultaneously on separate mock radios.
Its fourth-radio startup failed before the repair and passes afterwards without
changing the startup timeout. Physical throughput remains to be qualified.

## Migration matrix

| Path | Implemented in this milestone | Still required |
|---|---|---|
| Common device layer | Per-endpoint leases, stable-key reservations, generation/client checks, scoped tune/start/stop API; settings/start/diversity permits and stop draining across shared hardware | Remove raw mutable model access; complete per-instance controllers and nonblocking lifecycle; hang/unplug qualification |
| Listen: NFM/WFM/AM/USB/LSB/CW/AUTO | Selected-radio Add Receiver/table/remove, in-span sharing check, main tune targets selected Listen source; DSP unchanged | Persistent editable per-instance configuration/audio, all lifecycle tokens, remove remaining device-0 assumptions |
| P25 | Traffic-source pool selection excludes other reservations; no decoder/audio changes | Per-system control/follow instances, simultaneous calls, explicit CC/traffic roles and RF non-regression acceptance |
| SSTV | Multiple named image workers, isolated settings and cancellation; selected scoped radio or receiver tap; same GUI/web validation | Shared-source negotiation and physical multi-radio RF qualification |
| Inmarsat | Independent named engine/GUI/API sessions, settings and message/map stores; exact tune/start/restore tokens and source selectors; lifetime-bound worker failure cleanup | Physical multi-radio RF qualification, fully asynchronous native-driver lifecycle, session-specific recording collector |
| Satcom | Named engine/planner/audio/settings and GUI/API sessions; exact tune/start/restore tokens, per-radio takeover and stale completion rejection | Fully asynchronous native-driver lifecycle and physical RF/fault qualification |
| Aircraft / 1090 | Named GUI/API workers, independent track/CPR/network/settings, stable-key source and worker-scoped start/stop; CLI uses default adapter | Cancellation/hung-driver and physical multi-radio field qualification |
| RDS / CTCSS / DCS / DTMF / Morse | Read-only selected logical receiver windows; existing main-strip/repeater binding preserved | Persist instance layouts, general typed sample contract and automation |
| APRS / APT / satellite SSTV | Follow each named Satcom engine's selected radio, decoder state and token lifecycle; unique recording paths; DSP unchanged | Physical multi-radio/pass acceptance |
| SDRplay diversity / dual tuner | Physical serial treated as shared domain; conflicting reservations refused | Qualified shared-control operation; dedicated composite sessions currently refused |

## Acceptance and next order

Policy tests cover every owner type, four same-workflow instances plus another
workflow, stale tokens, topology reorder, missing/duplicate identities, shared
SDRplay domains and persistence rejection. Widget tests cover edits, save errors,
stale topology, absent reservations and SSTV source/frequency selection.
`WorkflowRadioLifecycle` uses five **mock Soapy radios**, real DeviceManager
open/tune/stop code and an old-worker/new-lease teardown test. This is not a claim
of five physical radios or concurrent P25 speech decoding.

DEC-0182 extends those fixtures with confirmed restoration, explicit live reuse,
independent satellite leases, missing-key resolution, GUI source persistence,
receiver reorder/removal and stale GUI completion tests. These are mock/GUI
proofs, not physical concurrent RF acceptance.

DEC-0187 adds concurrent real Inmarsat engines on two mock hardware devices,
isolated stopping/settings/maps and actual no-RF GUI session API tests. It does
not validate simultaneous physical L-band reception or signal quality.

Next: finish migrating all hardware mutations to scoped commands. Replace the
remaining P25 controller with per-system state before advertising concurrent
trunk followers. Add source/output controls, shared-source negotiation and automation
tests across all demods. Preserve existing signal processing; P25 timing/gating
changes require a separate measured decision, not an ownership exception.

DEC-0189 tests two real Satcom engines and two Aircraft workers through mock
hardware, including occupied-radio rejection, stop-one/keep-other-running,
hidden local RX and global shutdown. GUI/API fixtures cover settings, malformed
requests, stale IDs, isolated observers, reopen and stopped auto-capture.
The bounded Satcom log is tested with concurrent producers and exact accounting.

### Remaining P25 extraction boundary

`gP25AudioLastSpeakerOutputMs`, global voice diagnostic fallback and trusted
control-channel offset feed GUI/CLI follow decisions, not just display.
MainWindow also owns the follow lifecycle and voice-job generation/queues.
These must become per-system context before two followers can operate safely.
Keep the existing default context compatible, extract the CC/follow controller
and output route, then compare known-IQ replay against the unchanged baseline.
Tests must include identical TGIDs on different systems, separate CC/traffic
radios, encrypted/opposite-slot isolation, independent stop and PCM ordering.
This pass does not change those frozen P25 paths or claim concurrent P25 calls.
