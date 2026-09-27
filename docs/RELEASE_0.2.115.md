# SDR Town 0.2.115 - Automatic opted-in diagnostic evidence

Experimental portable testing release. Extract the complete ZIP to a fresh folder.

Enable **Help > Share Diagnostic Reports** once. Future launches automatically
report app/build version, PC specifications, radio settings, control actions,
receiver/audio state and bounded performance counters. No server setup is needed
for testers using the official asset. Sharing can be stopped from the same menu.

Startup/context reports are protected from routine decoder-report floods. New
action batches and snapshots include loss/transport evidence within the existing
64 KiB/minute budget. Audio, IQ, arbitrary typed text and credentials are not
uploaded automatically. See [diagnostics guide](DIAGNOSTICS_SHARING.md).

The collector admin page now has authenticated, filterable report history, not
only grouped issues. Existing recent JSONL reports are indexed without duplicating
issue counts. Bounded retention limits growth; login uses a private session cookie.

P25, analog demodulation and radio driver algorithms are unchanged. MainWindow's
exact diagnostics-only change is guarded; this release does not claim to repair
the outstanding P25 follow ownership or unverified remote RSPdx no-audio symptoms.
It supplies the missing context needed to diagnose those faults.

This is a portable prerelease, not a signed stable installer/updater package.
