# Workflow Devices (T-0103 / DEC-0181)

## Universal contract

Every RF workflow must ultimately select its radio in the same way, independent
of demodulator or protocol. P25 and SSTV are examples, not exclusive features.
The common control layer uses a stable physical identity, a workflow instance
ID and a generation-bound lease. Different physical radios may run the same
or different workflows concurrently. One instance cannot retune another's
exclusive source. A read-only VFO/decoder may share a source only while its
frequency and bandwidth fit the source, without changing hardware configuration.
These RF ownership rules are separate from each stream's audio routing/mute.

The first implemented milestone is below. It is not the complete migration.

## Operator controls

Open **Devices > Workflow Assignments** after discovering the radios. Stop a
radio before changing its reservation, choose its workflow, and save. Automatic
keeps existing allocation behavior. Several secondary radios may belong to
the same workflow pool. Disconnected radios' reservations remain saved.
Duplicate identities are rejected rather than matched to an arbitrary SDR.
The current primary Listen/P25-control radio is shown but not reassigned here:
its remaining index-based controller must be migrated before enabling that edit.

For SSTV, choose **Live RF**, select **Radio**, set **Frequency**, RF demodulation
and image format, then **Receive**. Main receiver uses the existing read-only
tap; a named radio starts an exclusive worker-owned session. Stop/cancel closes
only that session's radio. Selecting a missing radio reports an error; it does
not substitute the primary. Hardware startup must succeed; synthetic/stub IQ
does not qualify as reception. Output/save-folder behavior is unchanged.

Assignments are stored atomically in `workflow_devices.json` in application
data. Failed loads block new controlled tunes/claims until explicitly repaired
through the assignment window. Logs record workflow, device index, lease ID,
generation, requested frequency, confirmed startup and rejected tune reasons.
They remain subject to existing diagnostics consent; no new recording uploads.

## Migration matrix

| Path | Implemented in this milestone | Still required |
|---|---|---|
| Common device layer | Per-endpoint leases, stable-key reservations, generation/client checks, scoped tune/start/stop API, shared-hardware conflicts | Migrate all legacy gain/rate/start/restore callers; fault-injected hang/unplug qualification |
| Listen: NFM/WFM/AM/USB/LSB/CW/AUTO | Reservations protect secondary sources from main tuning/scan; DSP unchanged | Selectable logical receiver instances, per-instance configuration/audio, remove device-0 assumptions |
| P25 | Traffic-source pool selection excludes other reservations; no decoder/audio changes | Per-system control/follow instances, simultaneous calls, explicit CC/traffic roles and RF non-regression acceptance |
| SSTV | Dedicated selected radio via common scoped session, or read-only primary tap | Multiple image windows/sessions and source sharing with other VFOs |
| Inmarsat / Satcom | Per-selected-device ownership checks and scoped legacy release | Instance-based engines; token-based restore; replace singleton host takeover |
| Aircraft / 1090 | Existing owner now stored per device | Selectable source integration, token lifecycle and instance configuration |
| RDS / CTCSS / DCS / DTMF / Morse / APRS / APT / other taps | Existing decoder paths retained | Bind observers to selected logical receiver/session rather than implicit primary; compatible-rate/discontinuity contract |
| SDRplay diversity / dual tuner | Physical serial treated as shared domain; conflicting reservations refused | Qualified shared-control operation; dedicated composite sessions currently refused |

## Acceptance and next order

Policy tests cover every owner type, four same-workflow instances plus another
workflow, stale tokens, topology reorder, missing/duplicate identities, shared
SDRplay domains and persistence rejection. Widget tests cover edits, save errors,
stale topology, absent reservations and SSTV source/frequency selection.
`WorkflowRadioLifecycle` uses five **mock Soapy radios**, real DeviceManager
open/tune/stop code and an old-worker/new-lease teardown test. This is not a claim
of five physical radios or concurrent P25 speech decoding.

Next: inventory/migrate all hardware mutations to scoped commands, then expose
one reusable source selector on each workflow and logical receiver. Replace
singleton host/engine controllers with instance state before advertising repeated
instances. Add source/output controls, shared-source negotiation and automation
tests across all demods. Preserve existing signal processing; P25 timing/gating
changes require a separate measured decision, not an ownership exception.
