#pragma once

// Purpose: CLI batch command helpers and runCLI entrypoint.
// Spec:    docs/DECISIONS.md DEC-0040 / ISS-0004 Phase 7 (mechanical extract from main.cpp)
// Invariants: no hop/TTL/CADENCE/feed-gate behavior changes — move-only.

#include "Demod.h"
#include "P25VoiceDecode.h"

#include <cstdint>
#include <string>
#include <vector>

struct GuiRuntimeConfig {
    bool requested = false;
    bool startDevice = false;
    bool deviceIndexSet = false;
    size_t deviceIndex = 0;
    bool defaultAudio = false;
    bool autoFollow = false;
    bool p25Monitor = false;
    bool p25GrantTest = false;
    bool openP25Log = false;
    bool p25LateEntryAudioProbe = kP25Phase2AllowUnknownGrantFieldAudioProbe;
    bool iqCapture = false;
    bool dryRun = false;
    bool selfTest = false;
    bool requireClearAudio = false;
    bool iqReplay = false;
    bool iqReplayAutoPlay = false;
    bool iqReplayStt = true;
    bool iqReplayClearGrant = false;
    bool iqReplayEncryptedGrant = false;
    bool controlServer = true;
    bool controlAuthRequired = false;
    double frequencyHz = 0.0;
    double p25ControlHz = 0.0;
    double iqReplayTargetHz = 0.0;
    double iqReplayCenterHz = 0.0;
    double iqReplayVoiceCenterHz = 0.0;
    int iqCaptureDurationMs = 0;
    int exitAfterMs = 0;
    int clearAudioTimeoutMs = 0;
    int iqReplayStartMs = 0;
    int iqReplayDurationMs = 5000;
    int iqReplayWindowMs = 720;
    int iqReplayHopMs = 0; // 0 = CLI-compatible streaming auto cadence.
    int iqReplayTalkgroup = 0;
    int iqReplaySlot = -1;
    int iqReplayNac = -1;
    int controlPort = 8765;
    int64_t iqReplayWacn = -1;
    int iqReplaySystemId = -1;
    std::string iqCaptureLabel;
    std::string iqCaptureRoot;
    std::string selfTestPath;
    std::string workspacePreset;
    std::string bandPlanId;
    std::string screenshotPath;
    int windowWidth = 0;
    int windowHeight = 0;
    std::string debugStage;
    std::string iqReplayPath;
    std::string iqReplayWavPath;
    std::string iqReplayResultPath;
    std::string controlToken;
    std::vector<std::string> warnings;

    bool hasStartupWork() const noexcept
    {
        return requested || startDevice || defaultAudio || autoFollow || p25Monitor ||
            p25GrantTest || openP25Log || iqCapture || iqReplay || selfTest ||
            exitAfterMs > 0;
    }
};

struct SavedFrequency {
    std::string name;
    double freqHz = 100e6;
    DemodMode mode = DemodMode::AUTO;
    double bandwidthHz = 180000.0;
    double lpfHz = 15000.0;
    bool lpfEnabled = true;
    double squelchDb = -105.0;
    std::string tags;
};

GuiRuntimeConfig parseGuiRuntimeConfig(int argc, char* argv[]);

int runCLI(int argc, char* argv[]);
