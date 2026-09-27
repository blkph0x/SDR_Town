# Diagnostics sharing (0.2.115)

## Automatic session evidence

Once **Help > Share Diagnostic Reports** is enabled, each launch reports PC
specifications (OS, CPU model/architecture/core count, physical RAM, Qt version),
running version and packaged build metadata when available. No credentials or
server settings need to be entered by testers using official release assets.

`app.runtime` snapshots every 30 seconds and after control batches include the
receive frequency/mode/BW/LPF/squelch/gain, radio model and configured/current
sample rate, tuning request/applied sequence, antenna, direct sampling, SDRplay
AGC/IFGR/RFGR, RTL bias-T readback, stream state and IQ write position. Output
selection/volume/mute, audio ring depth/underruns and P25 follow/audio counters
are included. A no-audio symptom is not automatically classified as a driver bug.

`ui.intent` queues button intent before clicked handlers; `ui.actions` batches
buttons/menu actions, numeric controls and selection indexes. These are observed
actions, not hardware-success acknowledgements. Unlabelled controls have structural
widget paths. No arbitrary typed text, object names, clipboard, file paths,
credentials or recordings are collected by the new observer.

Actions batch at most every two seconds (32 actions plus overflow count); immediate
intent is limited to one per second. Routine queued decoder reports coalesce;
startup/state/error evidence has priority. Both ingress and network queues are
bounded, with explicit coalesced/drop/acknowledgement counters. The 64 KiB/minute
budget still applies: this is diagnostic evidence, not a lossless operation trace.
Network failures are counted; fresh periodic state restores context without
unbounded retries. Existing automatic monitor reports include frequencies and
P25 IDs; Inmarsat performance summaries have their separate privacy policy below.

## View received reports

On the collector PC open **http://127.0.0.1:8787/admin**, sign in using the separate
admin credential, then select **All reports / startup / settings / actions**.
Click an installation in Recent Clients or filter by installation, session,
event type or severity. Expand Report details for received fields. Older reports
are paginated; Issues remains the separate grouped warning/error view.
`/api/events` exposes the same bounded history to authenticated admins.

Upgrade indexes up to 64 MiB / 20,000 valid recent legacy JSONL records without
duplicating issue counts. History retains 30 days / 20,000 events / 64 MiB of
payloads. Raw JSONL rotates at 4 MiB (one previous segment) with 30-day / 128 MiB
total retention. Maintenance runs every minute; bursts can temporarily exceed
caps. SQLite reuses freed pages. Issue summaries and recording quotas are separate.

Login uses an eight-hour HttpOnly/SameSite cookie, not a credential in the login
URL. Keep HTTP admin local/private; the existing public proxy exposes HTTPS
ingest, not admin. Restarting the collector preserves consent and the public URL.

0.2.105 also adds [FM stage diagnostics](FM_DIAGNOSTICS.md): bounded local logs
and opt-in `fm.pipeline.sample` numeric summaries. These never include recordings
or tuned frequencies. Local logging does not require remote consent.

Open **Help > Share Diagnostic Reports** and accept the disclosure. No server
address or credential entry is needed in an officially configured build. The
choice is saved. Turn it off in the same menu to discard pending reports and
stop transmission; reports already received cannot be recalled. Launching
with `--no-remote-diagnostics` overrides the saved choice.

Automatic reports include technical errors, decoder/timing counters, CPU and
memory usage, thread/handle counts, app/OS/radio details and pseudonymous IDs.
Inmarsat performance summaries exclude aircraft identities, positions,
channel frequencies, IQ recordings and PCM. Manual issue reports contain
the text the user submits. The default transport budget is 64 KiB per minute.

The collector credential is separate from admin authority. It is distributed
with the app and therefore is not a confidential secret or proof of genuine
software: see [RFC 8252 section 8.5](https://www.rfc-editor.org/rfc/rfc8252#section-8.5).
Validation and request/byte/concurrency limits follow the resource-bounding
principle in [OWASP API4](https://owasp.org/API-Security/editions/2023/en/0xa4-unrestricted-resource-consumption/).
They reject malformed/excessive traffic, not all plausible forged reports.
Per-install enrollment/revocation remains open; bounded event retention is now implemented.

Current qualification: local builds/tests pass; collector updated and public
HTTPS receipt verified from this PC. No external RSPdx evidence had arrived at
the start of this pass. Physical no-audio diagnosis still requires the affected
tester to opt in and reproduce on the current build.
