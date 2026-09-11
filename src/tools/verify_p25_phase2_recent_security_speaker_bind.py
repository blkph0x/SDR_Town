from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
# DEC-0040: search all orchestration TUs
MAIN_TEXT = orchestration_source_text()


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise SystemExit(f"missing {label}: {needle}")


def main() -> None:
    text = MAIN_TEXT
    require(
        text,
        "void p25BindPhase2RecentSecurityEvidenceToCall",
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
        "targetSessionClearThisWindow",
        "speaker emit refresh accepts current-window target session release proof",
    )
    require(
        speaker_emit,
        "targetEssKnownThisWindow",
        "speaker emit refresh requires current-window target ESS proof",
    )
    if "rx.p25Phase2RecentTargetSessionAudioRelease = true;" in speaker_emit:
        raise SystemExit("speaker emit must not create same-call clear continuation proof")
    if "targetSessionAudioRelease && targetSecurityStateFromPtt && !targetEssEncrypted" in text:
        raise SystemExit("target session release must not be gated on a same-window PTT flag")
    if "targetPttSessionClearThisWindow" in text:
        raise SystemExit("old PTT-only session clear helper is still present")
    print("P25 Phase 2 recent security speaker-bind regression: PASS")


if __name__ == "__main__":
    main()
