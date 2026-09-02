# PowerShell GUI startup/self-test harness for SDR Town.
# Runs the Windows GUI executable with scriptable runtime arguments and verifies
# the JSON self-test record emitted by the GUI path.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\tests\run_gui_startup_tests.ps1
#   powershell -ExecutionPolicy Bypass -File .\tests\run_gui_startup_tests.ps1 -LiveClearAudio -P25ControlMHz 420.350

param(
    [string]$ExePath = (Join-Path $PSScriptRoot "..\build\bin\Release\SDR_Town.exe"),
    [double]$P25ControlMHz = 420.350,
    [int]$TimeoutSec = 30,
    [switch]$LiveClearAudio,
    [int]$ClearAudioTimeoutSec = 300,
    [string]$IqReplayCapture = "",
    [double]$IqReplayTargetMHz = 0.0,
    [double]$IqReplayCenterMHz = 0.0,
    [double]$IqReplayVoiceCenterMHz = 0.0,
    [int]$IqReplayStartMs = 0,
    [int]$IqReplayDurationMs = 1800,
    [int]$IqReplayWindowMs = 720,
    [int]$IqReplayHopMs = 0,
    [int]$IqReplayTimeoutSec = 0,
    [int]$IqReplayTalkgroup = 0,
    [int]$IqReplaySlot = -1,
    [string]$IqReplayNac = "",
    [string]$IqReplayWacn = "",
    [string]$IqReplaySystem = "",
    [switch]$IqReplayClear,
    [switch]$IqReplayEncrypted,
    [switch]$IqReplayRequireAudio,
    [switch]$IqReplayWithStt,
    [switch]$IqReplayRequireStt,
    # Match AI clear-audio pipeline STT defaults: chars=12 words=3, backend auto
    # (SDR_TOWN_STT_BACKEND or scripts/stt_transcribe_wav.py auto order).
    [string]$SttBackend = $(if ($env:SDR_TOWN_STT_BACKEND) { $env:SDR_TOWN_STT_BACKEND } else { "auto" }),
    [int]$IqReplayMinTranscriptChars = 12,
    [int]$IqReplayMinTranscriptWords = 3,
    [string]$TestRoot = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $ExePath)) {
    Write-Error "Exe not found at $ExePath. Build first with: cmake --build build --config Release --target SDR_Town"
    exit 1
}

$ExePath = (Resolve-Path $ExePath).Path
$ExeDir = Split-Path -Parent $ExePath
if ([string]::IsNullOrWhiteSpace($TestRoot)) {
    $TestRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("sdrtown_gui_tests_" + [guid]::NewGuid().ToString("N"))
}
New-Item -ItemType Directory -Path $TestRoot -Force | Out-Null
$TestRoot = (Resolve-Path -LiteralPath $TestRoot).Path

function Quote-Arg {
    param([string]$Value)
    if ($null -eq $Value) { return '""' }
    if ($Value -match '[\s"]') {
        return '"' + ($Value -replace '"', '\"') + '"'
    }
    return $Value
}

function Start-GuiCase {
    param(
        [string]$Name,
        [string[]]$Arguments,
        [string]$ResultPath,
        [int]$LocalTimeoutSec = $TimeoutSec
    )

    Write-Host "Running GUI startup test: $Name" -ForegroundColor Cyan

    if (Test-Path $ResultPath) {
        Remove-Item -LiteralPath $ResultPath -Force
    }

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $ExePath
    $psi.WorkingDirectory = $ExeDir
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.Arguments = (($Arguments | ForEach-Object { Quote-Arg $_ }) -join " ")

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    [void]$proc.Start()

    if (-not $proc.WaitForExit($LocalTimeoutSec * 1000)) {
        try { $proc.Kill() } catch {}
        throw "$Name timed out after $LocalTimeoutSec seconds"
    }

    $exitCode = $proc.ExitCode
    $proc.Close()

    if ($exitCode -ne 0) {
        throw "$Name exited with code $exitCode"
    }
    if (-not (Test-Path $ResultPath)) {
        throw "$Name did not write self-test JSON at $ResultPath"
    }

    return (Get-Content -LiteralPath $ResultPath -Raw | ConvertFrom-Json)
}

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Assert-Near {
    param([double]$Actual, [double]$Expected, [double]$Tolerance, [string]$Message)
    if ([math]::Abs($Actual - $Expected) -gt $Tolerance) {
        throw "$Message actual=$Actual expected=$Expected tolerance=$Tolerance"
    }
}

Write-Host "=== SDR Town GUI Startup Test Suite ===" -ForegroundColor Green
Write-Host "Exe: $ExePath"
Write-Host "Temp: $TestRoot"

$p25Hz = $P25ControlMHz * 1000000.0

$grantJsonPath = Join-Path $TestRoot "gui_grant_startup.json"
$grant = Start-GuiCase -Name "dry-run P25 grant startup" -ResultPath $grantJsonPath -Arguments @(
    "--gui-p25-control", $P25ControlMHz.ToString([System.Globalization.CultureInfo]::InvariantCulture),
    "--gui-grant-test",
    "--gui-default-audio",
    "--gui-startup-dry-run",
    "--gui-startup-self-test", $grantJsonPath,
    "--gui-exit-after-ms", "1600"
)

Assert-True ($grant.schema -eq "sdr-town-gui-runtime-startup-v1") "unexpected grant schema"
Assert-True ([bool]$grant.ok) "dry-run P25 grant startup reported not ok"
Assert-True ([bool]$grant.arguments.dryRun) "dry-run flag not reflected"
Assert-True ([bool]$grant.arguments.p25GrantTest) "grant-test flag not reflected"
Assert-True ([bool]$grant.arguments.defaultAudio) "default-audio flag not reflected"
Assert-True ([bool]$grant.p25.autoFollowEnabled) "P25 auto-follow was not enabled"
Assert-True ([bool]$grant.p25.independentTrafficEnabled) "P25 grant-test did not force the traffic source on"
Assert-Near ([double]$grant.p25.controlHz) $p25Hz 1.0 "P25 control frequency mismatch"
Assert-Near ([double]$grant.monitor.frequencyHz) $p25Hz 1.0 "monitor frequency mismatch"
Assert-Near ([double]$grant.monitor.channelBwHz) 12500.0 0.1 "P25 channel bandwidth mismatch"
Assert-True ($grant.monitor.mode -eq "NFM") "P25 monitor mode should be NFM"
Assert-True ($grant.receivers.Count -ge 1) "no receiver row emitted"
Assert-True (-not [bool]$grant.receivers[0].active) "dry-run receiver should not be active"
Assert-True ([bool]$grant.receivers[0].p25ControlMute) "control-channel audio mute was not enabled"
Assert-True ($grant.errors.Count -eq 0) ("unexpected errors: " + ($grant.errors -join "; "))
Write-Host "  PASS dry-run P25 grant startup" -ForegroundColor Green

$plainJsonPath = Join-Path $TestRoot "gui_plain_startup.json"
$plainHz = 476.4625 * 1000000.0
$plain = Start-GuiCase -Name "dry-run plain tune startup" -ResultPath $plainJsonPath -Arguments @(
    "--gui-frequency", "476.4625",
    "--gui-start-device",
    "--gui-startup-dry-run",
    "--gui-startup-self-test", $plainJsonPath,
    "--gui-exit-after-ms", "1200"
)

Assert-True ([bool]$plain.ok) "dry-run plain tune startup reported not ok"
Assert-True ([bool]$plain.arguments.startDevice) "start-device flag not reflected"
Assert-Near ([double]$plain.monitor.frequencyHz) $plainHz 1.0 "plain tune frequency mismatch"
Assert-True ($plain.receivers.Count -ge 1) "no receiver row emitted for plain tune"
Assert-True (-not [bool]$plain.receivers[0].active) "dry-run plain receiver should not be active"
Assert-True ($plain.errors.Count -eq 0) ("unexpected errors: " + ($plain.errors -join "; "))
Write-Host "  PASS dry-run plain tune startup" -ForegroundColor Green

if (-not [string]::IsNullOrWhiteSpace($IqReplayCapture)) {
    if (-not (Test-Path $IqReplayCapture)) {
        throw "IQ replay capture path does not exist: $IqReplayCapture"
    }
    if ($IqReplayTargetMHz -le 0.0) {
        throw "-IqReplayTargetMHz is required when -IqReplayCapture is supplied"
    }

    $replayJsonPath = Join-Path $TestRoot "gui_iq_replay.json"
    $replayStartupJsonPath = Join-Path $TestRoot "gui_iq_replay_startup.json"
    $replayWavPath = Join-Path $TestRoot "gui_iq_replay.wav"
    $replayExitMs = if ($IqReplayTimeoutSec -gt 0) { $IqReplayTimeoutSec * 1000 } else { $IqReplayDurationMs + 6000 }
    $replayArgs = @(
        "--allow-multiple",
        "--gui-iq-replay", (Resolve-Path $IqReplayCapture).Path,
        "--gui-iq-replay-target", $IqReplayTargetMHz.ToString([System.Globalization.CultureInfo]::InvariantCulture),
        "--gui-iq-replay-start-ms", $IqReplayStartMs.ToString(),
        "--gui-iq-replay-ms", $IqReplayDurationMs.ToString(),
        "--gui-iq-replay-window-ms", $IqReplayWindowMs.ToString(),
        "--gui-iq-replay-hop-ms", $IqReplayHopMs.ToString(),
        "--gui-iq-replay-autoplay",
        "--gui-iq-replay-result", $replayJsonPath,
        "--gui-iq-replay-wav", $replayWavPath,
        "--gui-startup-self-test", $replayStartupJsonPath,
        "--gui-exit-after-ms", ([Math]::Max(8000, $replayExitMs)).ToString()
    )
    if ($IqReplayCenterMHz -gt 0.0) {
        $replayArgs += @("--gui-iq-replay-center", $IqReplayCenterMHz.ToString([System.Globalization.CultureInfo]::InvariantCulture))
    }
    if ($IqReplayVoiceCenterMHz -gt 0.0) {
        $replayArgs += @("--gui-iq-replay-voice-center", $IqReplayVoiceCenterMHz.ToString([System.Globalization.CultureInfo]::InvariantCulture))
    }
    if ($IqReplayTalkgroup -gt 0) {
        $replayArgs += @("--gui-iq-replay-tg", $IqReplayTalkgroup.ToString())
    }
    if ($IqReplaySlot -ge 0) {
        $replayArgs += @("--gui-iq-replay-slot", $IqReplaySlot.ToString())
    }
    if (-not [string]::IsNullOrWhiteSpace($IqReplayNac)) {
        $replayArgs += @("--gui-iq-replay-nac", $IqReplayNac)
    }
    if (-not [string]::IsNullOrWhiteSpace($IqReplayWacn)) {
        $replayArgs += @("--gui-iq-replay-wacn", $IqReplayWacn)
    }
    if (-not [string]::IsNullOrWhiteSpace($IqReplaySystem)) {
        $replayArgs += @("--gui-iq-replay-system", $IqReplaySystem)
    }
    if ($IqReplayClear) {
        $replayArgs += "--gui-iq-replay-clear"
    }
    if ($IqReplayEncrypted) {
        $replayArgs += "--gui-iq-replay-enc"
    }
    if (-not $IqReplayWithStt) {
        $replayArgs += "--gui-iq-replay-no-stt"
    }

    $replayTimeout = if ($IqReplayTimeoutSec -gt 0) { $IqReplayTimeoutSec } else { [int](($IqReplayDurationMs / 1000) + 20) }
    $replay = Start-GuiCase -Name "GUI IQ replay" -ResultPath $replayJsonPath -LocalTimeoutSec ([Math]::Max($TimeoutSec, $replayTimeout)) -Arguments $replayArgs
    Assert-True (Test-Path $replayStartupJsonPath) "GUI IQ replay startup self-test JSON was not written"
    $replayStartup = Get-Content -LiteralPath $replayStartupJsonPath -Raw | ConvertFrom-Json
    Assert-True ($replay.schema -eq "sdr-town-gui-iq-replay-v1") "unexpected IQ replay schema"
    Assert-True ($replayStartup.schema -eq "sdr-town-gui-runtime-startup-v1") "unexpected IQ replay startup schema"
    Assert-True ([bool]$replayStartup.arguments.iqReplay) "IQ replay flag not reflected"
    Assert-True ([int64]$replay.windows -gt 0) "IQ replay decoded zero windows"
    Assert-True (Test-Path $replayWavPath) "IQ replay WAV was not written"
    if ($IqReplayRequireAudio) {
        Assert-True ([int64]$replay.audioEvents -gt 0) "IQ replay did not emit speaker-gated audio"
        Assert-True ([int64]$replay.audioSamples -gt 0) "IQ replay speaker sample count was zero"
    }
    if ($IqReplayRequireStt) {
        $projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
        $sttScript = Resolve-Path (Join-Path $projectRoot "scripts\stt_transcribe_wav.py")
        $sttPython = $env:SDR_TOWN_STT_PYTHON
        if ([string]::IsNullOrWhiteSpace($sttPython)) {
            $venvPython = Join-Path $projectRoot ".venv-stt\Scripts\python.exe"
            if (Test-Path $venvPython) {
                $sttPython = (Resolve-Path $venvPython).Path
            } else {
                $sttPython = "python"
            }
        }
        $oldErrorActionPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        $sttOutput = (& $sttPython $sttScript --backend $SttBackend $replayWavPath 2>$null)
        $sttExitCode = $LASTEXITCODE
        $ErrorActionPreference = $oldErrorActionPreference
        Assert-True ($sttExitCode -eq 0) "IQ replay STT exited with code $sttExitCode using $sttPython"
        $transcript = (($sttOutput | Out-String).Trim())
        Assert-True ($transcript.Length -ge $IqReplayMinTranscriptChars) `
            "IQ replay STT did not produce enough text; transcript='$transcript'"
        $wordCount = ([regex]::Matches($transcript, "[A-Za-z0-9']+")).Count
        Assert-True ($wordCount -ge $IqReplayMinTranscriptWords) `
            "IQ replay STT did not produce enough words; words=$wordCount transcript='$transcript'"
        Write-Host "  STT transcript: $transcript" -ForegroundColor Green
    }
    Write-Host ("  PASS GUI IQ replay windows={0} audioEvents={1} samples={2} wav={3}" -f `
        [int64]$replay.windows,
        [int64]$replay.audioEvents,
        [int64]$replay.audioSamples,
        $replayWavPath) -ForegroundColor Green
}

if ($LiveClearAudio) {
    $liveJsonPath = Join-Path $TestRoot "gui_live_clear_audio.json"
    $live = Start-GuiCase -Name "live P25 clear-audio gate" -ResultPath $liveJsonPath -LocalTimeoutSec ($ClearAudioTimeoutSec + 45) -Arguments @(
        "--gui-p25-control", $P25ControlMHz.ToString([System.Globalization.CultureInfo]::InvariantCulture),
        "--gui-grant-test",
        "--gui-default-audio",
        "--gui-require-clear-audio",
        "--gui-clear-audio-timeout-ms", ($ClearAudioTimeoutSec * 1000).ToString(),
        "--gui-startup-self-test", $liveJsonPath
    )

    Write-Host "  Live result JSON: $liveJsonPath"
    Write-Host ("  Live summary: ok={0} clearAudio={1} events={2} samples={3} decodedFrames={4} acceptedAmbe={5} outputCount={6} followTg={7} voiceMHz={8:N5} lastGrantAgeMs={9} trafficActive={10}" -f `
        [bool]$live.ok,
        [bool]$live.clearAudio.detected,
        [int64]$live.clearAudio.events,
        [int64]$live.clearAudio.samples,
        [int64]$live.clearAudio.p25DecodedFrames,
        [int64]$live.clearAudio.p25AcceptedAmbeFrames,
        [int]$live.audio.activeOutputCount,
        [int64]$live.p25.followTalkgroupId,
        ([double]$live.p25.voiceHz / 1000000.0),
        [int64]$live.p25.lastGrantAgeMs,
        [bool]$live.p25.independentTrafficActive)

    Assert-True ([bool]$live.ok) "live clear-audio gate reported not ok; inspect $liveJsonPath"
    Assert-True ([bool]$live.clearAudio.detected) "GUI did not detect accepted P25 decoder audio; inspect $liveJsonPath"
    Assert-True ([int64]$live.clearAudio.samples -ge [int64]$live.clearAudio.requiredSamples) "clear-audio sample count was below threshold; inspect $liveJsonPath"
    Assert-True ([int64]$live.clearAudio.p25AcceptedAmbeFrames -ge [int64]$live.clearAudio.requiredAcceptedFrames) "accepted AMBE frame count was below threshold; inspect $liveJsonPath"
    Assert-True ([int]$live.audio.activeOutputCount -gt 0) "no active output was selected; inspect $liveJsonPath"
    Write-Host "  PASS live P25 clear-audio gate" -ForegroundColor Green
}

Write-Host "All GUI startup tests passed." -ForegroundColor Green
Write-Host "Results kept at: $TestRoot"
