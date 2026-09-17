# Native runtime acceptance

DEC-0088/0089, T-0026: an executable folder can contain obsolete DLLs even when
the C++ source and dependency manifest are current. Do not infer runtime versions
from the manifest alone. Compare deployed hashes to configured imported targets.

The September 17 failure was reproduced using the executable folder's legacy
rtlsdr.dll, without Qt, Soapy or application DSP. Cycle 2 of open/read/cancel/close
raised an access violation. CDB independently caught a GUI shutdown AV in
libusb control transfer under the RTL driver and Soapy Device::unmake. The
configured vcpkg RTL package passed the identical native probe. Both libusb
files were byte-identical; changing application timeouts was not justified.

`stage_rtl_runtime` now copies CMake's imported shared RTL target on executable
builds. Release staging copies that runtime and its configured package licence.
The legacy DLL remains under build/rtl_runtime_evidence for local forensics;
it is not a release asset. The configured package SBOM is version 2.0.2 even
though its Windows FileVersion resource says 2.0.1. Use provenance and hashes,
not the Windows resource alone, when identifying it.

## Tests

Only one test/application may own the SDR. These commands tune receive-only to
98.1 MHz on device 0. Native probe requires exactly one enumerated RTL receiver.
Run the native probe in a subprocess with a timeout: a defective native driver
can hang or terminate its host process. It changes no saved application settings.

```powershell
python scripts/probe_rtlsdr_lifecycle.py --dll build/bin/Release/rtlsdr.dll --cycles 10
python scripts/test_gui_shutdown.py --debugger "C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe" --output build/shutdown_new_run --cycles 5
python scripts/test_rds_live_gui.py --frequency-mhz 98.1 --expect-pi 0x2981 --expect-ps i98FM --parity --debugger "C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe" --output build/rds_debug_new_run
```

CDB tests require new output paths, log first-chance AV stacks, and reject even
exceptions the application catches. They also require actual hardware, final
GUI results and normal exit. Shutdown repetition additionally checks audio
destruction and stream shutdown messages. No registry/global debugger settings
are modified; symbol lookup is local to avoid network stalls during a fault.
Use the optional RDS `--rf-gain 20` for a temporary gain experiment; the harness
restores the preceding setting. This is not a universal RF gain recommendation.

Acceptance on the local Generic R820T is not certification of other dongles,
Blog V4 HF behaviour, RSP models or all native driver failure modes. P25 DSP and
security were not changed by the runtime deployment fix. Keep future crashes
as separate evidence rather than assuming every native fault has this cause.
