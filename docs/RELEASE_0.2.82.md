# SDR Town 0.2.82

This release contains the tested P25 talkgroup hotfix, RSPdx runtime discovery improvements, and the completed satellite arm/capture runtime repair.

## Satellite receiver fixes

- Automatic capture runs from the always-present Satcom hub, including while the panel is hidden.
- The Satcom receiver is selected explicitly and persisted by stable device identity rather than a fragile enumeration index.
- Manual Arm pass and Arm ISS SSTV start the selected stream, tune the downlink, enter locked decoding, and apply Doppler tracking.
- Automatic capture may take a normal listening receiver but will not take an active P25, Inmarsat, or Aircraft owner.
- At LOS, automatically owned capture is finalised and the previous listening centre is restored.
- Decoder input uses the current DeviceManager IQ centre during asynchronous hardware tuning.
- Failed startup or tuning rolls back planner state, leases, streams, and worker lifecycle cleanly.

## Other included fixes

- RSPdx and RSPdx-R2 discovery covers the official SDRplay API x64 layout and reports module or dependency failures more clearly.
- P25 talkgroups populate even when unknown to RadioReference; known TGIDs receive their unambiguous alpha tag.
- Selected satellites are propagated on the observer map, with supported SSTV, APRS, and APT capture paths retained.

Rollback branch: backup/pre-satcom-auto-arm-runtime-20260921.
