# Build notes

Newest entry at the top. Record facts, not hopes.

---

## BN-0008 — DEC-0012 companion-louder mixed MAC-dead skip (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS compile; dual-slot / session-release / sticky-ESS string
  verifiers PASS
- **Voicetest (file `--center`, streaming DDC unset):**
  - 041716 TG 10330 slot 1 skip=32111 center=421.21375:
    `PASS_CONTINUOUS_AUDIO drop=ok duty=0.87`. Companion-louder mixed
    `p2mac=0` hops `unknown-waiting-clear` (seq=134 class).
  - 073304 TG 10330 slot 1 skip=107597 center=420.975:
    `PASS_CONTINUOUS_AUDIO duty=0.795`
  - 105622 TG 30003 slot 0 skip=97334 center=421.725:
    `PASS_PARTIAL_AUDIO drop=D duty=0.645` (was 0.685). Slot 1 duty=0.09.
- **Not proven:** live CADENCE on a new GUI follow (T-0010 drop D).

---

## BN-0007 — DEC-0017 revert + DEC-0018 streaming lattice (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS compile
- **Voicetest 105622 TG 30003 slot 0 skip=97334 center=421.725:**
  - Default block 80+280: `PASS_CONTINUOUS_AUDIO drop=ok duty=0.685` (DEC-0017 360/0 had 0.35)
  - `SDR_TOWN_P25_STREAMING_DDC=1`: duty 0.16 (80 ms), 0.09 (hopms=160), 0.045 (hopms=360)
- **Not proven:** live CADENCE vs 123525 duty 0.34. Streaming DDC stays opt-in.

---

## BN-0006 — DEC-0016 voice-park follow LO (2026-09-07)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS compile; `sdr_town_tests.exe "[tune]"` 18/5; one-RTL verifier PASS
- **Voicetest:** 105622 TG 30003 slot 0 skip=97334 `PASS_CONTINUOUS_AUDIO duty=0.685`
- **Not proven:** live listen on 115315-style 1.5 MHz grant (T-0010). File IQ was
  recorded at 420.97773 so replay cannot un-do the 997 kHz edge.

## BN-0005 — DEC-0015 SDRTrunk tuner center (2026-09-07)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC `cl` via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS compile (`main.cpp` rebuilt); `sdr_town_tests.exe "[tune][dec0015]"` 14/4; one-RTL / force-retune / inband string verifiers PASS
- **Voicetest (file `--center` geometry, not live LO):**
  - 105622 TG 30003 slot 0 skip=97334: `PASS_CONTINUOUS_AUDIO drop=ok duty=0.685`
  - 073304 TG 10330 slot 1 skip=107597 center=420.975: `PASS_CONTINUOUS_AUDIO duty=0.76 timelineOk`
- **Not proven:** live waterfall on a follow (close/reopen `build\bin\Release\SDR_Town.exe`). Expect log `P25 tuner center aligned (SDRTrunk CenterFrequencyCalculator)` and voice ~11 kHz right of center, not 250 kHz off.

## BN-0004 — DEC-0014 CADENCE + 80 ms stream slice; default DDC rejected (2026-09-07)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC `cl` via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS compile; streaming DSP verifier PASS; `[drop]` 14/8
- **Voicetest:**
  - Default-on streaming DDC: 105622 TG 30003 slot 0 skip=97334
    `PASS_PARTIAL_AUDIO drop=A duty=0.095` — reverted
  - After revert: 105622 `PASS_CONTINUOUS_AUDIO drop=ok duty=0.685`
  - 073304 TG 10330 slot 1 skip=107597 center=420.975:
    `PASS_CONTINUOUS_AUDIO duty=0.76 timelineOk`
  - 095450 TG 30013 slot 1 skip=3741: `PASS_PARTIAL_AUDIO duty=0.625`
    `oppAmbe=759/920 concealmentOk=no`; companion slot 0 duty=0.58
- **Not proven:** live CADENCE listen on 095450-style dual-TG (T-0010)

## BN-0003 — DEC-0013 lattice overlap de-dupe (2026-09-07)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC `cl` via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS
  - Verifiers: fresh/context, overlap de-dupe, dual-slot garble
  - Voicetest 105622 TG 30003 slot 0 skip=97334 8000 ms:
    `PASS_CONTINUOUS_AUDIO drop=ok duty=0.685`
  - Voicetest 073304 TG 10330 slot 1 skip=107597 8000 ms:
    `PASS_CONTINUOUS_AUDIO drop=ok duty=0.76 timelineOk`
- **Not proven:** live CADENCE after this binary (T-0010). Mixed MAC-dead
  garble on 073304 first 30302 / last dual-TG concealment (T-0004).

## BN-0002 — DEC-0008/0009 Phase 2 extract (2026-09-07)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC `cl` via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target sdr_town_tests SDR_Town`
- **Result:** PASS
  - `sdr_town_tests.exe "[drop]"` — 14 assertions / 8 cases
  - Verifiers: dual-slot garble, block-channelize continuity, fresh/context
    gate, overlap decode window, speaker sustain overlap
  - Voicetest 105622 TG 30003 slot 0 skip=97334 8000 ms:
    `PASS_CONTINUOUS_AUDIO drop=ok duty=0.735`
  - Same IQ slot=1: `PASS_PARTIAL_AUDIO duty=0.11` (isolation)
- **Not proven:** live CADENCE on 161748 after this binary (T-0010)

## BN-0001 — Method desk + drop classifier (2026-09-07)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC `cl` 19.44.35227 for x64 (VS 2022 Community), SDK 10.0.26100.0
- **CMake:** Visual Studio 17 2022 / x64, config Release, `BUILD_TESTS=ON`, `SDR_TOWN_ENABLE_MBELIB=ON`
- **Command:** `cmake --build build --config Release --target sdr_town_tests SDR_Town`
- **Result:** PASS
  - `sdr_town_tests.exe "[drop]"` — 14 assertions / 8 cases
  - `sdr_town_tests.exe` — 10166 assertions / 206 cases
  - `build/bin/Release/SDR_Town.exe` linked (main.cpp compiled with `P25AudioDropClass`)
- **Not proven:** live or IQ `drop=` on a clear Phase 2 call (ISS-0001 / ISS-0007)
