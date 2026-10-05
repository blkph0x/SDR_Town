# Package hardening and source-kit ledger

## Current completion pass: DEC-0196

Local exact-package qualification now passes with143 files and zero blockers.
The first clean CI attempt stopped on the newer Jansson2.15.1 MIT AND dtoa
expression; upstream LICENSE includes the Lucent dtoa notice. The verifier now
checks that notice explicitly, and CI's vcpkg commit is pinned. Full details
and negative-test evidence are in BUILD_NOTES. Public release is not yet claimed.

The four historical blockers were unconditional strings, so new material alone
could never satisfy them. Policy T-0104-notices-8 instead verifies a source-bound
distribution kit and exact-package Qt/USB replacement qualification. Missing
material remains blocked; mismatched notices, source hashes, executable hashes,
runtime sets and failed QA are rejected. This is a finite engineering checklist,
not a perpetual requirement to repeat an unspecified legal certification.

Original source remains MIT. The RTL-enabled combined distribution selects
GPL-3.0-or-later with original grants/notices preserved. DISTRIBUTION.md explains
scope, corresponding sources, rebuilding and user library replacement.
Fourteen embedded roots and all runtime-candidate Qt attribution expressions
are accounted for from the shipped source archives. The source kits retain
per-file copyrights and terms; the catalogue does not assert every optional
Qt component is linked. FreeType uses FTL. Original licence texts are hash-pinned.

The release RDS compiler is WinLibs GCC14.2.0 / MinGW-w64 12.0.0 UCRT POSIX,
upstream ZIP SHA256403380c3c125b5ba565d6b29d1f4aa9e18e6080f048da97bee841979d520b4c4.
Packaging matches compiler binaries/static archive candidates to that ZIP,
checks the configured RDS compiler/link flags/output, and ships GCC runtime
exceptions, actual C++ headers and complete MinGW runtime sources. Libm0.2.16
and both pinned Soapy module sources are included too. Compiler executables
themselves are not put in the portable app.

Independent local USB rebuild from exported recipes/scripts passed in86.25s
with binary caches disabled, exact source-resource comparison, isolated DLL
replacement positive/negative loader tests and CLI smoke. Qt6.7.3 and6.11.1
attribution reviews pass63/91 records. New distribution/qualification tests9/9,
existing inventory20/20 and release-verifier17/17 pass. Identified-compiler RDS
reference PI/MPX/confirmation/error tests pass. Final exact-commit package,
Actions and downloaded public release acceptance still follow; do not mistake
this implementation entry for a published asset.

The historical entries below are retained for provenance. Requirements they
describe clear when the new verifier validates the concrete materials and QA,
not by editing a generated blockers array.

DEC-0195: the libusb Windows recipe requires vcpkg-msbuild and its transitive
helpers, omitted by the original two-helper export. The kit now includes six
receipt-verified helpers and the tracked pkgconf recipe. App-local pkgconf DLLs
remain excluded. Tool acquisition can still download Meson/pkgconf inputs;
this does not silently close the independent USB rebuild gate.

2026-10-05 / DEC-0194: publisher explicitly confirms Visual Studio/Build Tools
licensing and permission to redistribute the permitted runtimes. Close that
external confirmation item; signed runtime provenance, original license and
allowlisted file checks remain mandatory. These are native C++ runtime files,
not a requirement for testers to install the development environment.
Microsoft guidance: https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files
The other four material/review requirements are not cleared by this confirmation.

DEC-0188 (2026-10-05): policy `T-0104-notices-7` also requires
`licenses/vcpkg/tooling-materials.zip`. It contains the configured checkout's
committed scripts, triplets, bootstrap metadata, notices and helper port recipes,
plus installed vcpkg-cmake/config files checked against their SPDX receipts.
The manifest binds the application and tooling commits. No untracked files,
download cache sweep, compiler binaries or SDK are included. Bootstrap may
download the upstream tools referenced by the shipped metadata. Local export
contains917 files/1,674,684 bytes; source/tooling tamper and dirty-input tests pass.
Independent USB-stack rebuild, static MinGW evidence and remaining distribution
review are still required. The completed Qt replacement gate is not reopened.

Reproduce with `python scripts/vcpkg_tooling.py --config
build/runtime-inputs-Release.json --stage build/deploy_staging`.

DEC-0186 (2026-10-04): packages now require
`licenses/project/source-materials.zip`, containing the committed application,
build scripts, embedded sources/data and the exact miniaudio/mbelib/liquid-dsp
submodule objects. A nested manifest binds file hashes to the executable's
source revision. Untracked material and reference-only OP25/SDRTrunk clones
are excluded; their URLs/pins remain in the source Git configuration. Export
refuses dirty tracked inputs or mismatched dependencies. This closes the
missing application/embedded source bundle, not the remaining licensing review
or compiler/tooling rebuild gaps. Qt replacement rebuild passed already; only
its linked third-party distribution review is still listed as a blocker.

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

- DEC-0178 now provides exact Qt6.7.3/6.11.1 qtbase/qtsvg/qttools archives,
  verified against official SHA256 pins, source license/attribution catalogs,
  six SDK feature files and rebuild/replacement instructions. A source-built
  QtSvg replacement passes pixel/plugin/application tests. DEC-0179 additionally
  passes a local source-built Qtbase/QtSvg replacement of all16 packaged runtimes,
  actual CLI and four GUI no-RX profiles. CI6.7.3 qualification, configuration
  reproduction and linked third-party review remain distinct. A broad source
  catalog is not a linked-component SBOM.
- DEC-0175 now stages/verifies exact source archives and patched port recipes
  for ten configured vcpkg dependencies (local: ten archives,48 recipe files).
  Full vcpkg tooling/triplets/compiler reproduction, independent rebuild and
  combined-distribution review remain required. Matching receipts are evidence,
  not completion of those remaining obligations.
- MinGW static runtime versions, applicable notices and exception evidence for
  the RDS DLL. Microsoft exact DLL/bundle versions, signed original installer
  and embedded end-user terms now ship. Publisher redistribution entitlement
  was explicitly confirmed2026-10-05; see the DEC-0194 entry above.
- DEC-0177 now preserves ASN.1 per-file notices, including explicit accounting
  for3 upstream headerless files;488 C/headers are hashed. Pinned ICAO CC0 and
  provenance ship, with map/country inputs and channel-list copy verification.
  Remaining embedded/static/data coverage still needs full closure; this is
  selected source evidence, not a complete linker map or transitive attribution.
- Corresponding-source archive/rebuild gate, then a new Actions release and
  independent public download/loader/smoke acceptance. Do not overwrite 0.2.122.

Primary Qt distribution guidance:
https://www.qt.io/development/open-source-lgpl-obligations .
Original project MIT terms remain unchanged; unresolved legal questions need
qualified review rather than an invented all-clear.

## Commands

```text
python scripts/test_embedded_notices.py
python scripts/test_qt_sources.py
python scripts/test_msvc_materials.py
python scripts/test_package_inventory.py
python scripts/test_stage_runtime.py
python scripts/test_verify_release.py
./scripts/build_rtl_module.ps1
./scripts/build_sdrplay_module.ps1
cmake --build build --config Release --target deploy -j 4
python scripts/qt_sources.py --config build/runtime-inputs-Release.json --cache build/release-materials/qt --fetch
python scripts/stage_runtime.py qt --config build/runtime-inputs-Release.json --stage build/deploy_staging
python scripts/test_qt_replacement.py --config build/runtime-inputs-Release.json --stage build/deploy_staging --output build/qt-replacement-qa
python scripts/test_rtl_runtime_package.py --stage build/deploy_staging
python scripts/package_inventory.py generate --stage build/deploy_staging
python scripts/package_inventory.py verify --zip <portable.zip>
python scripts/package_inventory.py verify --zip <portable.zip> --require-publishable
```

DEC-0177 local evidence:11 notice/19 inventory/12 source/9 staging/17 release
tests PASS,16/16 native suites, actual deploy and isolated RTL loader PASS.
129-file ZIP verifies with unchanged application hash and the five independent
publication requirements. `licenses/embedded-inputs.json` records selected
source hashes and leading comments; `licenses/acars/ASN1-NOTICES.txt` groups
identical copyright notices without discarding file associations. Full BSD
terms were already in the packaged ACARS upstream README and remain there.
Changing packaged data or notice text fails even when regenerating the outer
inventory. These checks detect packaging mistakes, not a malicious publisher
rewriting all source/evidence. Source CI and complete-kit acceptance stay distinct.

`generate` requires final `build-info.json` after dependency/Qt deployment.
It is not a replacement for the release helper or workflow. `verify` validates
the bounded archive without extracting it. `--require-publishable` must fail
until the remaining materials above are implemented and reviewed; no override
flag exists. Test/build results belong in BUILD_NOTES and LOG.

DEC-0178 policy T-0104-notices-5 verifies Qt source pins and recomputes notice
catalogs inside the bounded nested ZIP. New mandatory Microsoft receipts tie
the embedded license and actual DLL versions to the signed original installer.
`msvcRedistVersion` is retained as the old directory label for compatibility;
`msvcRuntimeVersion` is the actual binary version. Local label14.44.35112 versus
binary14.44.35211.0 demonstrates why they cannot be conflated. The collector
executes only trusted tooling, never the redistributable installer. No source
archive is executed during inventory verification. CI runs the source-built
QtSvg replacement test and uploads its compact result alongside the inventory,
without uploading binaries while publication is blocked.

DEC-0179 adds `--full` to `scripts/test_qt_replacement.py`. It builds qtbase then
qtsvg from the verified sources using the configured MSVC compiler, installs
only into a fresh disposable directory and replaces every Qt runtime/plugin
in the package. A missing rebuilt plugin fails; there is no SDK fallback.
The executable's DLL search PATH excludes developer tools. SVG/image/icon/widget
pixels, native TLS backend, loopback HTTP, CLI and four actual GUI dry-run
profiles are checked. No RX or external HTTP is used by the rendering/network
probe. Original package hashes must be unchanged. Reports preserve exact input
and replacement hashes, resolved settings, SDK feature differences and failures.

Reproduce after staging:

```powershell
python scripts/test_qt_replacement.py --config build/runtime-inputs-Release.json `
  --stage build/deploy_staging --output build/qt-full-replacement-qa --full `
  --work-root D:/SDRTown-QtQA
```

Choose an existing spacious drive for `--work-root`; only fresh temporary
children are removed. If the actual package includes `tls/qopensslbackend.dll`
(CI6.7.3 does), also supply `--openssl-root` for its header SDK. Header hashes
are recorded and linked OpenSSL is forbidden. No OpenSSL DLL is added to the
package. These external build headers, compiler and Windows SDK are prerequisites,
not falsely represented as contained in the Qt source kit. Full runtime rebuild
does not mean all Qt modules, qttools, exact upstream build-farm reproduction,
or a completed combined-distribution review.

DEC-0180: CI uses `--headless-layout` because its large native-window capture
did not match the requested viewport. Native Windows probe/CLI/listening smoke
remain; all four full-size layouts run on an explicitly logged, source-built,
QA-only qoffscreen plugin with installed Windows fonts. The portable package
does not acquire that plugin or fonts. Local default still exercises all four
native Windows layouts. Geometry/no-RX assertions are unchanged; desktop metrics,
individual reports and screenshots are retained to diagnose runner differences.
