# RSPdx discovery and Arm Pass hotfix

This change addresses two independently reported Windows problems.

## RSPdx discovery

- Read the SDRplay API installation directory from the official Windows registry key in both 64-bit and 32-bit registry views.
- Continue to support explicit environment overrides and conventional SDRplay/PothosSDR locations.
- Treat `SoapySDR::loadModule()` as successful only when it returns an empty error string.
- Preserve the exact module-loader error in Device Manager diagnostics.
- Retry module loading on a later Rescan until a real load succeeds.

The proprietary SDRplay API and SoapySDRPlay3 module are still external dependencies and are not redistributed by SDR Town.

## Manual satellite pass arm

- Use the downlink attached to the selected pass row.
- Treat the explicit **Arm pass** and **Arm ISS SSTV** buttons as permission to take tuner control.
- Configure the locked pass frequency before starting the Satcom worker.
- Make `SatcomScannerEngine::armPass()` start the receiver itself when required.
- Release the tuner lease and disarm cleanly if any part of startup fails.
- Keep background automatic pass capture non-forcing so it does not interrupt another active owner.

Rollback branch: `backup/pre-arm-pass-hotfix-20260921`.
