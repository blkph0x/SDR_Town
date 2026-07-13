# Continuous P25 Phase-2 voice replay automation (SDRTrunk-style stream hops).
# Example:
#   .\scripts\p25_stream_voicetest.ps1 `
#     -Meta "C:\Users\...\capture.sigmf-meta" `
#     -VoiceMhz 421.975 -Slot 1 -Tg 30003 `
#     -Nac 0x2dc -Wacn 0xbee00 -System 0x2d1 `
#     -SkipMs 648400 -Ms 5000 -Wav ".\build\out.wav"

param(
    [Parameter(Mandatory = $true)][string]$Meta,
    [Parameter(Mandatory = $true)][double]$VoiceMhz,
    [int]$Slot = 1,
    [uint32]$Tg = 30003,
    [string]$Nac = "0x2dc",
    [string]$Wacn = "0xbee00",
    [string]$System = "0x2d1",
    [double]$SkipMs = 0,
    [double]$Ms = 5000,
    [double]$HopMs = 40,
    [double]$WindowMs = 360,
    [double]$VoiceCenterMhz = 0,
    [string]$Wav = "",
    [long]$MinFrames = 40,
    [double]$MinAudio = 1.0,
    [switch]$Clear = $true,
    [string]$Exe = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Exe) {
    $Exe = Join-Path $root "build\bin\Release\SDR_Town.exe"
}
if (-not (Test-Path $Exe)) { throw "SDR_Town.exe not found: $Exe" }
if (-not (Test-Path $Meta)) { throw "meta not found: $Meta" }

if (-not $VoiceCenterMhz -or $VoiceCenterMhz -le 0) { $VoiceCenterMhz = $VoiceMhz }
if (-not $Wav) {
    $Wav = Join-Path $root ("build\vt_stream_{0}_{1}.wav" -f $Tg, (Get-Date -Format "yyyyMMdd_HHmmss"))
}

$grant = if ($Clear) { "clear" } else { "enc" }
$cmd = @(
    "p25 voicetest `"$Meta`" $VoiceMhz $Ms",
    "skip=$SkipMs",
    "voicecenter=$VoiceCenterMhz",
    "slot=$Slot",
    "tg=$Tg",
    "nac=$Nac",
    "wacn=$Wacn",
    "system=$System",
    $grant,
    "stream",
    "hopms=$HopMs",
    "windowms=$WindowMs",
    "wav=`"$Wav`"",
    "minframes=$MinFrames",
    "minaudio=$MinAudio",
    "quit"
) -join " "

Write-Host "Running: $Exe --cli"
Write-Host $cmd

$out = $cmd | & $Exe --cli 2>&1
$out | ForEach-Object { $_ }

$resultLine = $out | Where-Object { $_ -match "P25 voicetest result=" } | Select-Object -Last 1
if (-not $resultLine) { throw "no result line from voicetest" }
Write-Host "`nRESULT: $resultLine"
if ($resultLine -notmatch "PASS_") {
    exit 2
}
if (Test-Path $Wav) {
    $len = (Get-Item $Wav).Length
    Write-Host "WAV: $Wav ($len bytes)"
}
exit 0
