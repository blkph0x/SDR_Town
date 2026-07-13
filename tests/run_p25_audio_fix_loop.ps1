# Build, unit-test, replay, and summarize P25 Phase 2 audio diagnostics.
param(
    [switch]$SkipBuild,
    [switch]$UseLatestCapture,
    [string]$CaptureDir = "",
    [int]$ReplayTimeoutSec = 90,
    [int]$MaxReplayCommands = 5
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $Repo "build"
$Exe = Join-Path $BuildDir "bin\Release\SDR_Town.exe"
$Tests = Join-Path $BuildDir "bin\Release\sdr_town_tests.exe"
$LogRoot = Join-Path $env:APPDATA "SDR_Town\SDR Town\iq_test_captures"
$Stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$OutDir = Join-Path $BuildDir "p25_audio_fix_loop\$Stamp"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function Write-Section([string]$Title) {
    Write-Host ""
    Write-Host "=== $Title ===" -ForegroundColor Cyan
}

function Save-Text([string]$Path, [string]$Text) {
    $Text | Out-File -FilePath $Path -Encoding utf8
}

Write-Section "P25 audio fix loop"
Write-Host "Repo: $Repo"
Write-Host "Output: $OutDir"

if (-not $SkipBuild) {
    Write-Section "Configure + build Release"
    if (-not (Test-Path $BuildDir)) {
        New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    }
    Push-Location $BuildDir
    try {
        cmake .. -DCMAKE_BUILD_TYPE=Release
        if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
        cmake --build . --config Release --target SDR_Town sdr_town_tests
        if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }
    } finally {
        Pop-Location
    }
}

if (-not (Test-Path $Exe)) { throw "Missing executable: $Exe" }
if (-not (Test-Path $Tests)) { throw "Missing tests: $Tests" }

Write-Section "Unit tests [p25]"
$testOut = & $Tests "[p25]" 2>&1 | Out-String
Save-Text (Join-Path $OutDir "unit_tests_p25.txt") $testOut
Write-Host $testOut
if ($LASTEXITCODE -ne 0) { throw "P25 unit tests failed" }

Write-Section "Replay suite"
$replayArgs = @(
    (Join-Path $Repo "src\tools\run_p25_capture_replay_suite.py"),
    "--timeout", "$ReplayTimeoutSec",
    "--max-commands", "$MaxReplayCommands",
    "--out-dir", (Join-Path $OutDir "replay_suite")
)
if ($UseLatestCapture -or [string]::IsNullOrWhiteSpace($CaptureDir)) {
    $replayArgs += "--latest"
} else {
    $replayArgs += $CaptureDir
}
$replayOut = & python @replayArgs 2>&1 | Out-String
Save-Text (Join-Path $OutDir "replay_suite.json") $replayOut
Write-Host $replayOut

Write-Section "Latest live capture log scan"
$latestCaptureDir = $null
if (Test-Path $LogRoot) {
    $latestCaptureDir = Get-ChildItem -Path $LogRoot | Where-Object { $_.PSIsContainer } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
}
$scan = [ordered]@{
    capture_dir = if ($latestCaptureDir) { $latestCaptureDir.FullName } else { $null }
    fed_gt0 = 0
    emit_gt0 = 0
    decoding_clear_voice = 0
    fed0_decoding_clear = 0
    slot_flip = 0
    dsp_slow = 0
    speaker_samples = 0
}
if ($latestCaptureDir) {
    $logCandidates = @(
        (Join-Path $latestCaptureDir.FullName "*_p25_log.txt"),
        (Join-Path $latestCaptureDir.FullName "*.log"),
        (Join-Path $latestCaptureDir.FullName "*.txt")
    )
    $logFiles = @()
    foreach ($pattern in $logCandidates) {
        $logFiles += Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue
    }
    $logFiles = $logFiles | Sort-Object FullName -Unique
    foreach ($log in $logFiles) {
        $text = Get-Content $log.FullName -Raw -ErrorAction SilentlyContinue
        if (-not $text) { continue }
        $scan.fed_gt0 += ([regex]::Matches($text, "fed=[1-9]\d*")).Count
        $scan.emit_gt0 += ([regex]::Matches($text, "emitPcm=[1-9]\d*|emit=[1-9]\d*")).Count
        $scan.decoding_clear_voice += ([regex]::Matches($text, "decoding clear voice")).Count
        $scan.fed0_decoding_clear += ([regex]::Matches($text, "decoding clear voice[^\r\n]*fed=0")).Count
        $scan.slot_flip += ([regex]::Matches($text, "slot auto-probe: switching slot")).Count
        $scan.dsp_slow += ([regex]::Matches($text, "dsp=(1\d{5,}|[2-9]\d{5,})\d*us")).Count
        $speakerPeak = ([regex]::Matches($text, "speaker=(\d+)", "IgnoreCase") |
            ForEach-Object { [int]$_.Groups[1].Value } |
            Measure-Object -Maximum).Maximum
        if ($speakerPeak -and $speakerPeak -gt $scan.speaker_samples) {
            $scan.speaker_samples = [int]$speakerPeak
        }
    }
}
$scanJson = ($scan | ConvertTo-Json -Depth 4)
Save-Text (Join-Path $OutDir "live_capture_scan.json") $scanJson
Write-Host $scanJson

Write-Section "Pass criteria"
$pass = $true
if ($scan.fed0_decoding_clear -gt 0) {
    Write-Host "WARN: saw decoding clear voice with fed=0 ($($scan.fed0_decoding_clear)x)" -ForegroundColor Yellow
    $pass = $false
}
if ($scan.fed_gt0 -eq 0 -and $scan.emit_gt0 -eq 0) {
    Write-Host "WARN: no fed/emit activity in latest capture logs" -ForegroundColor Yellow
}
if ($scan.speaker_samples -ge 960) {
    Write-Host "OK: speaker samples peak = $($scan.speaker_samples)" -ForegroundColor Green
} else {
    Write-Host "WARN: speaker samples peak = $($scan.speaker_samples) (want >= 960)" -ForegroundColor Yellow
    $pass = $false
}

$summary = @"
timestamp=$Stamp
exe=$Exe
unit_tests=PASS
replay_out=$(Join-Path $OutDir 'replay_suite.json')
live_scan=$(Join-Path $OutDir 'live_capture_scan.json')
speaker_peak=$($scan.speaker_samples)
fed_gt0=$($scan.fed_gt0)
fed0_decoding_clear=$($scan.fed0_decoding_clear)
overall=$(if ($pass) { 'PASS' } else { 'NEEDS_LIVE_RETST' })
"@
Save-Text (Join-Path $OutDir "summary.txt") $summary
Write-Host $summary

if (-not $pass) { exit 2 }
exit 0
