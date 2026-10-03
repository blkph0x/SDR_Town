# Package hardening and source-kit ledger

T-0104 / DEC-0172/0174; baseline e4d767e, version remains 0.2.122.
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

The initial local test exposed `pkgconf-7.dll`, old `pthreadVC2.dll`, graphics
runtimes and translations being swept from the long-lived Release tree.
DEC-0174 replaces that sweep with declared target/module outputs and recognized
runtimes from the configured dependency directory. Qt is deployed by the
configured Qt tool directly into staging, identically in local and CI scripts;
app-local MSVC CRT comes from the configured compiler's installation.
`licenses/runtime-deployment.json` binds their versions and file hashes to the
inventory. Pinned module helpers refuse dirty upstream sources. CPack cannot
fall back to the developer folder; no developer files are deleted or relabelled.

Local actual staging, isolated RTL positive/negative loader, DTMF/RDS and four
GUI profiles PASS; ZIP verification inventories124 files with only the five
known publication blockers. Nine staging tests cover stale inputs, missing
dependencies, safe cleanup, Unicode/spaces, collisions, links, configuration
bounds and configured Qt deployment/failure. Sixteen inventory tests PASS.
ISS-0064 is locally repaired; exact-commit clean CI remains the source acceptance
gate for this change. This is an allowlisted configured runtime set, not proof
of a minimal dynamic dependency closure. Optional D3D12 rendering remains
unqualified; the Qt deploy warning is recorded in BUILD_NOTES.

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
python scripts/test_stage_runtime.py
python scripts/test_verify_release.py
./scripts/build_rtl_module.ps1
./scripts/build_sdrplay_module.ps1
cmake --build build --config Release --target deploy -j 4
python scripts/stage_runtime.py qt --config build/runtime-inputs-Release.json --stage build/deploy_staging
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
