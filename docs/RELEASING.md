# Release gates

Version 0.2.56 is an experimental tester build. SSTV/satellite features remain
roadmap items. No Authenticode signature or universal hardware qualification
is implied by the signed updater manifest.

## Prepare

Review the source diff, update CMake project version/README/trackers, and commit
source before invoking `scripts/release.ps1 -Version X.Y.Z -Channel experimental`.
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

## Publish

`-SkipPush -SkipAssets` produces and commits metadata locally for inspection.
Then push the reviewed branch, create an annotated unused version tag and push
it, and create the GitHub release with `--verify-tag` and the assets below.
Default automatic publication performs those same steps on the current branch.
Do not replace an existing published tag or overwrite a previously released
asset; correct failures with a new version.

Assets: installer, installer SHA-256, portable ZIP, standalone versioned
SdrTownControl DLL and SHA-256, SHA256SUMS.txt, update.json and update.json.sig.
The experimental channel is in the signed manifest and release title. It is
published as GitHub Latest (not a GitHub prerelease) because the existing updater
reads `/releases/latest/download/update.json`; this preserves the tester update
channel. There is no new silent installer behavior.

No diagnostic token is automatically injected. Explicit remote diagnostic
configuration remains a separate release option; never commit keys or captures.
After upload, inspect the release asset list and verify downloaded asset hashes.
