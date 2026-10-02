# SDR Town 0.2.119

Experimental workspace-contract release for FUBAR integration.

- Adds the native aircraft map status/control bridge used by FUBAR.
- Adds the native Inmarsat map status snapshot used by FUBAR.
- Keeps aircraft identities, positions, freshness and device state owned by SDR Town; FUBAR receives bounded snapshots.
- Preserves the P25/DSP/audio implementation and passes the protected-path guard.
- Includes the matching `SdrTownControl.dll` in the portable package and release assets.

Validation: 16/16 CTest tests, Inmarsat GUI/host lifecycle tests, and the P25 protected-path guard.
