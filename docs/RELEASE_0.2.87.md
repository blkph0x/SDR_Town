# SDR Town 0.2.87

## Satcom and Inmarsat hardware selection fix

This release fixes the incorrect message:

```text
No real SDR device is available; placeholder/stub devices cannot run Satcom
```

### Root cause

During safe startup, DeviceManager may publish an entry named:

```text
RTL-SDR placeholder (enable will try hardware)
```

That entry is a deferred **real-hardware probe**, not a demo receiver. Pressing Start asks SoapySDR to open the physical device, and SDR Town only continues after the stream reports `live hardware` and real IQ arrives.

Satcom and Inmarsat previously rejected every label containing `placeholder` before allowing that real-open attempt. This could reject a receiver that already worked in Listen, or one that would have opened successfully when Start was pressed.

### Corrected behaviour

- The known `placeholder (enable will try hardware)` entry is now eligible for Satcom and Inmarsat.
- Receiver selectors display this state as **PROBE ON START**.
- Explicit demo entries containing `(stub)` remain rejected.
- Unknown generic placeholders remain rejected.
- Satcom and Inmarsat still require the stream to reach `live hardware` and provide real IQ; simulated data cannot pass the runtime gate.
- The same correction is applied to the Inmarsat engine and receiver selector.

### Regression protection

Automated tests now cover:

- acceptance of the exact deferred-hardware proxy label
- acceptance of normal physical SDR labels
- rejection of RTL-SDR and SDRplay `(stub)` labels
- rejection of unknown generic placeholders

The frozen P25 guard, non-P25 hardening checks, SSTV helper tests, SDRplay preflight syntax check, Windows MSVC build, core tests, Qt/live-decoder tests, deployment, ZIP creation and checksum generation must all pass before release publication.

## P25 safety

No protected P25 control, follow, traffic, Phase 1/2, vocoder or P25 audio-pipeline source is changed by this fix. DeviceManager, Receiver, Demod and AudioEngine implementation files are also unchanged.

## Rollback

```text
backup/pre-satcom-device-proxy-fix-20260923
backup/pre-v0.2.87-release-20260923
```

Real SDR operation still depends on the installed hardware driver, Soapy module, USB connection and the physical receiver being available to SDR Town.
