# Unified validation: build, unit tests, static verify scripts, optional capture replay.
param(
    [switch]$ReplayLatest,
    [int]$ReplayTimeoutSec = 120
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

& "$Root\scripts\build_test_validate.ps1"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($ReplayLatest) {
    python "$Root\src\tools\run_p25_capture_replay_suite.py" --latest --timeout $ReplayTimeoutSec
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "test_all: PASS"
