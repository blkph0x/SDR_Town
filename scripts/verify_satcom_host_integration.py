#!/usr/bin/env python3
# Static contract checks for the Satcom/MainWindow host integration.

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="strict")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"Satcom host integration check failed: {message}")


def main() -> int:
    engine_h = source("include/SatcomScannerEngine.h")
    engine_cpp = source("src/SatcomScannerEngine.cpp")
    main_cpp = source("src/MainWindow.cpp")
    widget_cpp = source("src/SatcomScannerWidget.cpp")
    hub_cpp = source("src/SatcomHubWidget.cpp")
    host_h = source("include/SatcomHostServices.h")

    require("class SatcomHostServices" in host_h, "host service header missing")
    require("beginReceiverTakeover" in host_h, "receiver takeover callback missing")
    require("acquireAudioEngine" in host_h, "shared audio callback missing")
    require("publishSpectrum" in host_h, "shared spectrum callback missing")

    require("AudioEngine* audio_ = nullptr;" in engine_h, "engine does not hold borrowed host audio")
    require("std::unique_ptr<AudioEngine> fallbackAudio_;" in engine_h,
            "standalone fallback audio missing")
    require("beginHostTakeover" in engine_h and "endHostTakeover" in engine_h,
            "host lifecycle declarations missing")
    require("SatcomHostServices::instance().acquireAudioEngine" in engine_cpp,
            "engine does not acquire MainWindow audio")
    require("SatcomHostServices::instance().publishSpectrum" in engine_cpp,
            "engine does not publish shared spectrum")
    require("if (!usingSharedAudio_.load(std::memory_order_acquire))" in engine_cpp,
            "shared MainWindow audio queue can still be trimmed by Satcom")
    require("snapshot.sharedMainAudio = usingSharedAudio_.load(std::memory_order_acquire);" in engine_cpp,
            "shared audio status is not read atomically")
    require("restorePreviousDeviceState();\n    endHostTakeover();" in engine_cpp,
            "receiver restoration is not followed by host restoration")

    require('#include "SatcomHostServices.h"' in main_cpp,
            "MainWindow does not include host services")
    require(main_cpp.count("// SATCOM_HOST_INTEGRATION_BEGIN") >= 2,
            "MainWindow integration markers missing")
    require("SatcomHostServices::instance().install" in main_cpp,
            "MainWindow does not install Satcom host callbacks")
    shutdown = main_cpp.split("void MainWindow::stopAllStreaming()", 1)[1]
    shutdown = shutdown.split("// SATCOM_HOST_INTEGRATION_END", 1)[0]
    require("SatcomScannerEngine::stopAll();" in shutdown and
            "SatcomHostServices::instance().clear();" in shutdown and
            shutdown.index("SatcomScannerEngine::stopAll();") <
            shutdown.index("SatcomHostServices::instance().clear();"),
            "MainWindow shutdown does not stop/clear Satcom first")
    require('ensureAudioOutputActive("Satcom monitor")' in main_cpp,
            "Satcom is not routed through configured MainWindow audio")
    require("rx->active = false;" in main_cpp and "rx->active = saved.active;" in main_cpp,
            "ordinary Listen receiver pause/restore missing")

    require('modeCombo_->addItems({"NFM", "WFM", "AM", "USB", "LSB", "APT", "APRS"});'
            in widget_cpp, "LSB SSTV/manual mode is missing")
    require("configured SDR Town playback output" in widget_cpp,
            "Satcom audio tooltip still describes isolated output")
    require("If Satcom selected the active Listen receiver" in widget_cpp,
            "takeover behavior is not explained in the UI")

    require("retuneWithLease(autoDeviceIndex_" not in hub_cpp,
            "hub still performs a second independent receiver restore")
    require("satcom_->autoCaptureTick();" in hub_cpp and "engine.armPass(" not in hub_cpp,
            "default hub bypasses the shared automatic capture policy")
    automatic = widget_cpp.split("void SatcomScannerWidget::updateAutoCapture(", 1)[1]
    automatic = automatic.split("void SatcomScannerWidget::refreshPassesTable", 1)[0]
    require("engine.armPass(best->satId, best->downlinkId, true, false, &error)" in automatic and
            "position.armable" in automatic and "sstvOnly" in automatic,
            "automatic capture can force takeover or ignore satellite eligibility")

    print("Satcom/MainWindow host integration contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
