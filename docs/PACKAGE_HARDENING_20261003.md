# Package hardening and source-kit ledger

T-0104 / DEC-0172; baseline e4d767e, version remains 0.2.122.
No RX, demodulator, P25, vocoder, speaker or consent code changes.

## Confirmed defects and repairs

1. CI 37111782581's downloaded portable ZIP has `rtlsdr.dll` but no
   `libusb-1.0.dll`. MSVC dumpbin proves that exact RTL binary imports the
   missing DLL. A fresh Windows loader process cannot load it. This is distinct
   from a successful CLI help/DTMF/RDS test, which does not open an RTL driver.
   CMake now copies libusb from the same configured dependency directory as RTL.
   The isolated loader test restricts search to the DLL directory/System32,
   verifies the bias-T export and loads the Soapy module. A complete disposable
   fixture passes; the same fixture without libusb fails. No RF I/O is performed.
2. Root project notices, miniaudio/mbelib texts and ten configured vcpkg
   dependency notice/SPDX files were absent from the package. Deployment now
   copies the actual build inputs without changing their contents. Upstream
   patent warnings are retained, not replaced with a blanket MIT label.
3. ZIP packaging lacked a complete file-list/hash contract. The new inventory
   binds each file to a known component group, size and SHA-256. It checks
   the source/executable provenance and configured vcpkg runtime hashes. The
   verifier rejects missing/empty notices, modified/unlisted entries, unknown
   runtimes, duplicate/case-colliding and unsafe Windows paths, symlinks, file/
   directory collisions, oversized archives/metadata and duplicate JSON keys.
   Python normalizes ZIP backslashes on Windows; the raw filename must pass too.
4. Publication previously had no executable enforcement of ISS-0060. Actions,
   the local release helper, direct CPack and the signed-release verifier now
   enforce the gate. Source CI can build/test/compress locally in the runner,
   but while blocked it uploads only the hash inventory, not the binary ZIP.
   Release branches fail rather than publish an incomplete package. Editing a
   generated report to remove blockers does not pass recomputed verification.

## Evidence semantics

Source929cb11 passed Windows CI37116308564 and YAML37116308593. Clean runner
loader/native/GUI/replay/smoke and ZIP verification PASS;122 files inventoried.
Downloaded inventory verified, with no binary uploaded while the five source/
notice requirements remain open. Exact hashes and run URLs are in BUILD_NOTES.

`licenses/build-inputs.json` records the source commit, configured Qt version,
actual pinned submodule commits, vcpkg versions/license declarations and known
vcpkg DLL hashes. Full vcpkg SPDX records are retained beside their notices.
No local paths, collector tokens, IQ, audio or message payloads are included
in the uploaded inventory. Config files are represented only by their hashes.

This is an exact-file integrity record plus known build inputs, **not** a
complete transitive SBOM, independent provenance proof, code-signing signature
or legal certification. The existing signature/checksum gates remain. It does
not establish live hardware reception or fix a demodulation issue.

The local long-lived Release tree also contains `pkgconf-7.dll`, an old
`pthreadVC2.dll`, graphics runtimes and translations outside the current CI
inventory. `StageRuntime.cmake` copies all root DLLs. These are now rejected
instead of silently entering a release. Files were not deleted or relabelled.
Dumpbin on local RTL/Soapy/QtGui confirms direct imports but does not exclude
dynamic plugin loads; do not remove graphics support based on that alone.
Qualify a clean dependency-closure staging path before claiming local portable
parity with the clean CI runner. Tracked as ISS-0064.

## Remaining materials before release

- Exact Qt 6.7.3 CI source/build configuration, LGPL replacement instructions
  and bundled third-party notices. Local Qt 6.11.1 is a different build input.
- Exact RTL-SDR/libusb sources and vcpkg patched build recipes, plus applicable
  dependency materials. Installed SPDX resource URLs/checksums are evidence,
  not delivery of those source archives. Record any combined-work review.
- MinGW static runtime versions, applicable notices and exception evidence for
  the RDS DLL; Microsoft redistributable exact versions and applicable terms.
- Remaining embedded/static/data notice coverage, including ASN.1 runtime
  per-file notices and actual packaged data. Do not confuse component grouping
  with complete transitive attribution.
- Corresponding-source archive/rebuild gate, then a new Actions release and
  independent public download/loader/smoke acceptance. Do not overwrite 0.2.122.

Primary Qt distribution guidance:
https://www.qt.io/development/open-source-lgpl-obligations .
Original project MIT terms remain unchanged; unresolved legal questions need
qualified review rather than an invented all-clear.

## Commands

```text
python scripts/test_package_inventory.py
python scripts/test_verify_release.py
cmake --build build --config Release --target deploy -j 4
python scripts/test_rtl_runtime_package.py --stage build/deploy_staging
python scripts/package_inventory.py generate --stage build/deploy_staging
python scripts/package_inventory.py verify --zip <portable.zip>
python scripts/package_inventory.py verify --zip <portable.zip> --require-publishable
```

`generate` requires final `build-info.json` after dependency/Qt deployment.
It is not a replacement for the release helper or workflow. `verify` validates
the bounded archive without extracting it. `--require-publishable` must fail
until the remaining materials above are implemented and reviewed; no override
flag exists. Test/build results belong in BUILD_NOTES and LOG.
