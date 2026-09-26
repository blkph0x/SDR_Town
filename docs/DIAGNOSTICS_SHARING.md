# Diagnostics sharing (0.2.104, local qualification)

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
Per-install enrollment/revocation and durable disk retention remain open.

Current qualification: local builds/tests pass. Production collector restart
was blocked; new server-side safeguards are not yet verified live. The client
and rotor milestone can ship independently, without claiming that server work
complete. Per-install security and external-tester receipt remain open gates.
