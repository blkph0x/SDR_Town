# Workflow Devices (T-0103 / DEC-0181..0184)

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

## Migration matrix

| Path | Implemented in this milestone | Still required |
|---|---|---|
| Common device layer | Per-endpoint leases, stable-key reservations, generation/client checks, scoped tune/start/stop API; settings/start/diversity permits and stop draining across shared hardware | Remove raw mutable model access; complete per-instance controllers and nonblocking lifecycle; hang/unplug qualification |
| Listen: NFM/WFM/AM/USB/LSB/CW/AUTO | Selected-radio Add Receiver/table/remove, in-span sharing check, main tune targets selected Listen source; DSP unchanged | Persistent editable per-instance configuration/audio, all lifecycle tokens, remove remaining device-0 assumptions |
| P25 | Traffic-source pool selection excludes other reservations; no decoder/audio changes | Per-system control/follow instances, simultaneous calls, explicit CC/traffic roles and RF non-regression acceptance |
| SSTV | Dedicated selected radio via common scoped session, or selected logical receiver tap; web source/frequency uses same validation | Multiple image windows/sessions and shared-source negotiation |
| Inmarsat / Satcom | Common source resolution; exact tune/start/restore tokens; per-radio GUI takeover and stale completion rejection; ownership-loss stop | Repeatable engine instances, fully asynchronous lifecycle and fault qualification |
| Aircraft / 1090 | GUI/web stable-key selection, worker-scoped capture configuration/start/stop; CLI uses same session adapter | Multiple concurrent aircraft instances, cancellation/hung-driver field qualification |
| RDS / CTCSS / DCS / DTMF / Morse | Read-only selected logical receiver windows; existing main-strip/repeater binding preserved | Persist instance layouts, general typed sample contract and automation |
| APRS / APT / satellite SSTV | Follow Satcom's selected radio and token lifecycle; existing DSP unchanged | Independent source/session controls outside singleton Satcom |
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

Next: finish migrating all hardware mutations to scoped commands. Replace
singleton host/engine controllers with instance state before advertising repeated
instances. Add source/output controls, shared-source negotiation and automation
tests across all demods. Preserve existing signal processing; P25 timing/gating
changes require a separate measured decision, not an ownership exception.
