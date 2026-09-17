# Receive Band Plans

Post-v0.2.55 development. Open **Scan > Band Plans** or **Band Plan...** in
Receiver Controls. Select region, country, then location/profile and press
Apply. Previewing a different profile does not change the active receiver.
Selection and imported profiles persist locally. The default is Australia;
there is no IP lookup, GPS detection, or claim to know the user's location.

The waterfall displays the active region/country/location and frequency-aligned
service sections across the visible range, with demodulation hints. Overlaps
use the same priority/ambiguity rules as AUTO. Visible service edges
use the same frequency transform as the waterfall. Hide the overlay with
**View > Band Plan on Waterfall**.

Click or left-drag in the waterfall to select a frequency. Dragging previews
the frequency and commits one retune on release; it does not queue hardware
retunes for every mouse movement. The spectrum squelch handle remains separate.

## What AUTO Does

The selected profile replaces the old mixed-country frequency-prior table.
Existing AUTO mode/BW selection combines these priors with signal estimates.
Manual modes are not forcibly changed by selecting a profile. Highest priority
wins, then narrowest matching range. Equally ranked incompatible entries yield
AUTO, never an arbitrary demodulator. Ranges are half-open: start included,
end excluded. Channel entries include a receive window around their centre.

Mixed/data services remain AUTO and block a broader analog band hint. Frequency
alone does not prove a protocol. Decoder text is a hint, **not automatic decoder
activation**: RDS/RBDS, AIS, DSC, VDL2 and SAME are not implemented by this work.
P25 framing, encryption/slot checks, following and vocoder paths are unchanged.
Receive bandwidth/LPF values are application defaults, not legal emission limits.
Step is retained as metadata; this feature does not change tuning step controls.

## Coverage And Sources

Built-in profiles are explicitly **partial**, not complete national or worldwide
allocation databases. AU/GB/US cover selected broadcast, aviation and marine
services, plus Australian CB, UK PMR446, and US NOAA weather channels. No
state/city transmitter inventory is bundled. User-defined locations can be
imported. Unmapped frequencies are labelled as such and remain signal-driven.

Sources reviewed 2026-09-17:

- [ACMA spectrum plan](https://www.acma.gov.au/australian-radiofrequency-spectrum-plan)
- [ACMA CB](https://www.acma.gov.au/licences/citizen-band-radio-stations-class-licence)
- [Ofcom UKFAT](https://www.ofcom.org.uk/spectrum/frequencies/uk-fat)
- [Ofcom PMR446](https://www.ofcom.org.uk/spectrum/radio-equipment/wta-exemptions-jul15)
- [CAA aeronautical radio](https://www.caa.co.uk/commercial-industry/airspace/communication-navigation-and-surveillance/aeronautical-radio-stations/)
- [FCC allocations](https://www.fcc.gov/engineering-technology/policy-and-rules-division/general/radio-spectrum-allocation)
- [USCG marine channels](https://navcen.uscg.gov/international-vhf-marine-radio-channels-freq)
- [NOAA weather radio](https://www.weather.gov/marine/wxradio)

108-117.975 MHz is navigation, not a blanket AM voice prior. Australian UHF CB
22/23 have a data override. PMR446 is mixed analog/digital, not forced NFM.

The former generic HF amateur/shortwave mode assumptions are not presented as
verified national plans. Their removal means AUTO no longer receives those
specific frequency priors; manual USB/LSB/CW/AM still works. Completing sourced
HF sub-band coverage and additional countries is outstanding, alongside actual
decoder capability routing. Existing hardware/UI tuning limits are unchanged.

## Local Profiles

Export a profile as a starting point, edit its JSON, then Import. Built-in IDs
AU/GB/US cannot be overwritten: give exported templates a unique local ID.
An import replaces a local profile with the same ID. Replacing the active ID
updates that active profile immediately; otherwise press Apply to select it.
Imports are local data only, not downloaded or executed. Sources are references,
not evidence that an imported document is accurate. Verify them independently.

Required top-level fields: `schema` (`sdr-town-bandplan-v1`), `id`, `region`,
`country`, `location`, `coverage`, `revision`, and nonempty `entries`.
Each entry requires `name`, `startHz`, `endHz`, `mode`, `bandwidthHz`, `lpfHz`,
`stepHz`, `decoder`, and HTTPS `source`; optional integer `priority` defaults
to zero. Modes: AUTO, NFM, WFM, AM, USB, LSB, CW. Use AUTO with zero filter
values when frequency does not uniquely identify an analog receive mode.

Limits: 256 KiB/profile, 1024 entries, 32 local profiles, JSON depth 12,
priority -100 through 100. Nonfinite/reversed frequencies, unsupported modes,
control characters, missing metadata and out-of-range filters are rejected.
Profiles are immutable snapshots, so GUI selection cannot invalidate DSP readers.

## Automation

```powershell
SDR_Town.exe --cli --cmd "bandplans list"
SDR_Town.exe --cli --cmd "bandplans select AU"
SDR_Town.exe --gui-bandplan GB
```

CLI selection persists. GUI startup override applies only to that run, unless
the user explicitly presses Apply. Invalid IDs produce a warning and retain the
previous profile. `--gui-self-test` includes active profile metadata.
`scripts/test_workspace_gui.py` checks AU/GB/US across the workspace presets.
