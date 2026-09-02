# P25 baseline: clear continuous audio (2026-08-10)

Recoverable “best so far” marker for Phase-2 clear continuous voice.
Do not regress isolation or invent opposite-slot/gap PLC when iterating past this point.

## Marker ID

`p25-clear-continuous-20260810`

## Binary fallback (preferred)

| Artifact | Path |
| --- | --- |
| App | `build/baselines/SDR_Town_p25_clear_continuous_20260810.exe` |
| Tests | `build/baselines/sdr_town_tests_p25_clear_continuous_20260810.exe` |
| SHA256 | `build/baselines/SDR_Town_p25_clear_continuous_20260810.sha256` |

- App SHA256: `fe2ebcc922f98c0af577197fe7f858a2f1cd1e62b6a4b7baf7d36175b2d6267e`
- Size: 3554304 bytes
- Copied from `build/bin/Release/SDR_Town.exe` at marker time
- Git HEAD at marker: `beeaea8445319504003868cbb754ecd0e5cbe752` (working tree had uncommitted P25 work; **use the binary copy**, not `git checkout` alone)

Restore:

```text
copy /Y build\baselines\SDR_Town_p25_clear_continuous_20260810.exe build\bin\Release\SDR_Town.exe
```

## Proof capture (live GUI)

- Folder: `...\iq_test_captures\20260810_134531_767_iq_NFM_420_35000MHz_420.35000MHz_startstop\`
- Log: `*_p25_log.txt` (~80 s)
- Calls: TG **10330** then TG **10120**, both slot **1** (sequential handoff via CC, not simultaneous mix)

## Contamination forensic (log evidence only)

Source: `20260810_134531_*_p25_log.txt`. Counted `gate=emit` audio-output lines only.

| Check | Result |
| --- | --- |
| Emit count | 69 (`TG 10330`×1 logged emit on **grant slot 0**; `TG 10120`×63 on **slot 1**) |
| Emit TG vs followed TG | Match only. **No cross-TG speaker contamination.** |
| Emit slot vs follow slot | Matches grant (10330→slot0, 10120→slot1). Not opposite-slot leakage. |
| `fed>0` and `wrongSlot>0` on emit | **0** — opposite VCWs rejected; not fed to speaker PCM |
| `emit` with `wrongSlot>0` | **0** |
| `COMPANION_PROMOTED` / `SLOT_CHANGED` | **0** |
| Opposite-slot RF during emit | 10/69 emits had `oppVcw>0` (companion/observe only; `wrongSlot=0` on those emits) |
| “Doubled” sensation | 24/69 emits had `dup>0` or `absDup>0` (overlap-window re-decode), not a second TG on the ring |
| Hiccups | Inter-emit gaps and long quiet after early 10330 activity + watchdog — continuity residual, not TG leak |

Verdict: **PARTIAL** on continuity (small hiccups / overlap dups); **PASS** on TG/slot isolation for the speaker path.

## Code marker

`SDR_TOWN_P25_AUDIO_BASELINE` in `src/main.cpp` (printed with `--version` and P25 capture log headers after this docs land). The frozen exe above is the authoritative fallback until a newer baseline is cut the same way.
