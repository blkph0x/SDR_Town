# Release gates

`DEVELOPMENT_RULES.md` section 11 is mandatory: push completed changes, publish
application assets through Actions, wait for successful checks, download and
verify the public assets and smoke-test the shipped executable. Failures block
completion. No RF/hardware qualification is implied by a successful build.

## Licensing gate (2026-10-03)

Original SDR Town contributions now have a standard MIT grant, but that does
not relicense actual third-party code or the combined binary. Learning from
reference principles alone does not establish source reuse; the earlier
P25-specific block in ISS-0059 was withdrawn under DEC-0170. Before another
binary publication, resolve exact-artifact notice/source coverage in ISS-0060. Follow
[LICENSING.md](../LICENSING.md#binary-release-gate). A green build is not license
clearance; credits or repository URLs alone do not satisfy all obligations.
Do not overwrite existing public assets. Documentation-only corrections may
be pushed and checked without a version bump or new binary release.

DEC-0172 enforces this gate: `package_inventory.py` stages notices and hashes
the final contents, then verifies the compressed ZIP. While source/notice
requirements remain open, master CI builds/tests in the runner but uploads only
`package-inventory.json`; binary artifact upload is disabled. Release branches,
the local helper, direct CPack and signed verifier fail closed. No metadata
boolean can approve an unresolved component. See
[package ledger](PACKAGE_HARDENING_20261003.md) for the remaining source kit.
CI also loads the staged RTL/Soapy module in isolated Windows processes and
proves that withholding libusb fails, without opening hardware.

## Mandatory CI portable release

For 0.2.98 the user requested a regular Releases entry: use `release/v0.2.98`.
Non-prerelease portable publications explicitly use `--latest=false` to preserve
the signed updater's Latest URL. The 0.2.97 entry was promoted from prerelease
on 26 September without changing its tag or assets. A normal Releases entry
does not by itself certify stable RF reception or an in-app update package.

1. Review source, run local gates, bump the CMake/SoT version and write
   `docs/RELEASE_X.Y.Z.md`. Keep README and trackers current.
2. Commit and push the source. Push the same commit to
   `release/vX.Y.Z-experimental` using a new, unused version.
3. Watch all relevant Actions runs for that exact commit. Inspect logs and fix
   failures; never weaken gates or overwrite an already-published version.
4. Actions builds/tests the app, stages dependencies, publishes the ZIP and
   checksum, then anonymously downloads them, checks hashes and source/run
   provenance, and smoke-runs the shipped CLI.
5. Repeat public download/hash/extraction/smoke checks locally. Record source
   SHA, successful Actions URLs, release URL and hardware limits in the trackers.

```powershell
git push origin master HEAD:refs/heads/release/vX.Y.Z-experimental
gh run list --branch release/vX.Y.Z-experimental
gh run watch RUN_ID --exit-status
gh release view vX.Y.Z-experimental
gh release download vX.Y.Z-experimental --dir DOWNLOAD_DIRECTORY
```

The current workflow publishes a **portable prerelease**, not an installer or
in-app update. It must not displace the previous signed installer release at
`/releases/latest/download/update.json`. Run the extracted folder's executable,
not an EXE copied without its dependencies. CI packages do not automatically
inherit local diagnostics credentials; preserve explicit tester consent.

## Local installer qualification (not CI publication)

Review the source diff, update CMake project version/README/trackers, and commit
source before invoking `scripts/release.ps1 -Version X.Y.Z -Channel experimental -SkipPush -SkipAssets`.
The helper requires a clean attached branch; it never assumes master. Signing
requires the existing private Ed25519 key outside the repository, matching
`resources/update_manifest_ed25519_pub.inc`. Ordinary signing never rotates it.

Fresh checkout: initialize submodules, install the project's MSVC/Qt/vcpkg build
dependencies, and provide x86_64-w64-mingw32 GCC/G++ and make for RDS. See
[RDS build details](RDS.md). `_codex_refs/redsea` is not a build dependency;
the pinned licensed subset and short reference fixtures are committed.

## Gates

The helper checks native exit codes, builds the pinned SDR modules, deploy and
all native test targets, runs CTest, then deploys configured Qt/MSVC runtimes
directly into the clean stage. It creates NSIS/ZIP assets, signs and verifies
only after the publication gate passes. It does not deploy Qt into the developer
output folder or recopy that folder into staging.
`python scripts/verify_release.py --version X.Y.Z --installer <setup.exe>` checks
installer size/hash/URL, all three asset checksums, detached Ed25519 signature
against the embedded public key, required ZIP runtimes, staging equality and
the configured RTL runtime. It rejects private/debug/capture artifacts and
unsafe ZIP paths. This is packaging verification, not an RF acceptance test.

Before publication also run RDS/tone/registry CLI tests and GUI layout smoke
tests. Hardware acceptance is separate: live RDS station identity/parity and
GUI shutdown under CDB are documented in NATIVE_RUNTIME_QA.md. Do not label
stub-device or synthetic tests as successful RF reception.

### Common runtime staging

`cmake/StageRuntime.cmake` generates `build/runtime-inputs-Release.json` from
configured targets and dependency paths. Both Actions and the local helper use
`scripts/stage_runtime.py`. For a local runtime-only qualification after CMake
configuration and compilation:

```powershell
./scripts/build_rtl_module.ps1
./scripts/build_sdrplay_module.ps1
cmake --build build --config Release --target deploy -j 4
python scripts/stage_runtime.py qt --config build/runtime-inputs-Release.json --stage build/deploy_staging
python scripts/test_rtl_runtime_package.py --stage build/deploy_staging
python scripts/test_workspace_gui.py --exe build/deploy_staging/SDR_Town.exe --output build/package-workspace
```

Missing inputs are rejected before the old stage is removed. Linked or ambiguous
paths are rejected. Only the exact configured `build/deploy_staging` directory
may be recreated; developer binaries/captures are left alone. Re-running deploy
requires re-running the Qt step, provenance creation and inventory generation.
There is no raw `bin/Release` CPack fallback.

`licenses/runtime-deployment.json` records configured Qt identity, deployment-tool
hash, MSVC redistributable version and exact Qt/MSVC file hashes, without host
paths. App-local CRT DLLs are included; a separate `vc_redist` installer is not.
This evidence does not replace the source/build/replacement instructions or
redistribution terms still required by ISS-0060. Optional D3D12/graphics backends
are not qualified by the QWidget smoke tests.

## Exact vcpkg source materials

The deploy target runs `scripts/vcpkg_sources.py` using the vcpkg root derived
from the configured toolchain. It requires the matching `ports` recipes and
`downloads` archives; a directory with only installed binaries is insufficient.
If relocating a local toolchain, reconfigure explicitly rather than changing
unrelated applications' global environment. Custom download caches can be passed
to the exporter with `--downloads`; normal CI uses the configured root's cache.
CI disables vcpkg binary-cache retrieval during dependency configuration to
retain the exact source downloads. It does not fetch newer sources as a fallback.

`licenses/vcpkg/source-materials.zip` contains exact upstream archive bytes,
port recipes/patches, notices and SPDX receipts. Recipe SHA256, upstream SHA512
and installed DLL SHA256 must match those receipts. Export is bounded/atomic;
verification checks nested ZIP paths, membership and hashes without extracting
or executing upstream code. Unsupported/missing materials fail packaging.
The inventory exposes sourceMaterials counts; eleven source-kit negative tests
and nested-tamper/DLL-receipt inventory tests run in CI. Full vcpkg tooling,
independent rebuild, Qt and other component requirements remain open in ISS-0060.

GUI package QA runs with dry-run and diagnostics disabled. Saved satellite
auto-capture is suppressed for that process without rewriting preferences;
the smoke gate rejects transient RX startup as well as a streaming end state.
Device enumeration and audio prewarm still occur: this is not a hardware sandbox.

## Signed installer channel

The legacy helper's direct upload mode is not a substitute for the mandatory
Actions build/publication gate. Extending Actions to produce the complete
signed installer set remains separate work; do not call a portable release an
installer update. Never upload private signing keys to CI. Do not replace an
existing published tag or asset; correct failures with a new version.

Assets: installer, installer SHA-256, portable ZIP, standalone versioned
SdrTownControl DLL and SHA-256, SHA256SUMS.txt, update.json and update.json.sig.
The experimental channel is in the signed manifest and release title. It is
published as GitHub Latest only after the entire signed set is verified (not a GitHub prerelease) because the existing updater
reads `/releases/latest/download/update.json`; this preserves the tester update
channel. There is no new silent installer behavior.

No diagnostic token is automatically injected. Explicit remote diagnostic
configuration remains a separate release option; never commit keys or captures.
After upload, inspect the release asset list and verify downloaded asset hashes.
