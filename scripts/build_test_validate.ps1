# Build Release targets and run core P25 validation suite.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

if (-not (Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
}

cmake --build build --config Release --target SDR_Town sdr_town_tests
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& "$Root\build\bin\Release\sdr_town_tests.exe" "[p25]"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$verify = @(
    "verify_p25_phase2_same_call_mhz_hop.py",
    "verify_p25_phase2_clear_grant_op02_preservation.py"
)
foreach ($script in $verify) {
    python "$Root\src\tools\$script"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "build_test_validate: PASS"
