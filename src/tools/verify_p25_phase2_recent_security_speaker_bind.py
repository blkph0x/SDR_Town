from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "src" / "main.cpp"


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise SystemExit(f"missing {label}: {needle}")


def main() -> None:
    text = MAIN.read_text(encoding="utf-8", errors="ignore")
    require(
        text,
        "static void p25BindPhase2RecentSecurityEvidenceToCall",
        "recent-security call binding helper",
    )
    require(
        text,
        "p25BindPhase2RecentSecurityEvidenceToCall(rx, key);",
        "refresh path uses shared binding helper",
    )
    speaker_emit = text[text.find("if (speakerEmitted) {") :]
    require(
        speaker_emit,
        "const P25P2CallAudioKey key = p25CurrentPhase2AudioKey(rx, out.effectiveTargetFreqHz);",
        "speaker emit derives current call key",
    )
    require(
        speaker_emit,
        "targetPttSessionClearThisWindow",
        "speaker emit refresh requires current-window target PTT clear proof",
    )
    require(
        speaker_emit,
        "targetEssKnownThisWindow",
        "speaker emit refresh requires current-window target ESS proof",
    )
    if "rx.p25Phase2RecentTargetSessionAudioRelease = true;" in speaker_emit:
        raise SystemExit("speaker emit must not create same-call clear continuation proof")
    print("P25 Phase 2 recent security speaker-bind regression: PASS")


if __name__ == "__main__":
    main()
