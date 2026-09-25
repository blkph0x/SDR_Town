# Release gates

`DEVELOPMENT_RULES.md` section 11 is mandatory: push completed changes, publish
application assets through Actions, wait for successful checks, download and
verify the public assets and smoke-test the shipped executable. Failures block
completion. No RF/hardware qualification is implied by a successful build.

## Mandatory CI portable release

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

The helper checks native exit codes, builds deploy and both test targets, runs
CTest, deploys Qt, rebuilds staging, creates NSIS/ZIP assets, signs and verifies.
`python scripts/verify_release.py --version X.Y.Z --installer <setup.exe>` checks
installer size/hash/URL, all three asset checksums, detached Ed25519 signature
against the embedded public key, required ZIP runtimes, staging equality and
the configured RTL runtime. It rejects private/debug/capture artifacts and
unsafe ZIP paths. This is packaging verification, not an RF acceptance test.

Before publication also run RDS/tone/registry CLI tests and GUI layout smoke
tests. Hardware acceptance is separate: live RDS station identity/parity and
GUI shutdown under CDB are documented in NATIVE_RUNTIME_QA.md. Do not label
stub-device or synthetic tests as successful RF reception.

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
