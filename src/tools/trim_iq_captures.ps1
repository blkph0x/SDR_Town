# Keep golden IQ dirs; delete the rest under AppData iq_test_captures.
# Default keep-set matches docs/BACKLOG B-0011. Override with -Keep.
param(
  [string[]]$Keep = @(
    '20260909_060036_708_iq_NFM_420_47500MHz_420.47500MHz_startstop',
    '20260909_094846_500_iq_NFM_420_47500MHz_420.47500MHz_startstop',
    '20260909_095846_023_iq_NFM_420_47500MHz_420.47500MHz_startstop'
  ),
  [switch]$AlsoKeepLatestLiveFollow,
  [switch]$WhatIf
)

$cap = Join-Path $env:APPDATA 'SDR_Town\SDR Town\iq_test_captures'
if (-not (Test-Path -LiteralPath $cap)) { throw "missing $cap" }

$keepSet = [System.Collections.Generic.HashSet[string]]::new([string[]]$Keep)
if ($AlsoKeepLatestLiveFollow) {
  $latest = Get-ChildItem -LiteralPath $cap -Directory |
    Where-Object { $_.Name -like '20*_p25_follow_*' } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
  if ($latest) { [void]$keepSet.Add($latest.Name) }
}

Get-ChildItem -LiteralPath $cap -Directory | ForEach-Object {
  $szMb = [math]::Round(((Get-ChildItem $_.FullName -Recurse -File -EA SilentlyContinue |
    Measure-Object Length -Sum).Sum) / 1MB, 1)
  if ($keepSet.Contains($_.Name)) {
    Write-Host "KEEP  $szMb MB $($_.Name)"
  } else {
    Write-Host "DELETE $szMb MB $($_.Name)"
    if (-not $WhatIf) {
      Remove-Item -LiteralPath $_.FullName -Recurse -Force
    }
  }
}

# Loose WAV/jsonl at captures root: leave manifest; delete orphan wavs
Get-ChildItem -LiteralPath $cap -File -Filter '*.wav' | ForEach-Object {
  Write-Host "DELETE loose WAV $($_.Name)"
  if (-not $WhatIf) { Remove-Item -LiteralPath $_.FullName -Force }
}

$freeGb = [math]::Round((Get-PSDrive C).Free / 1GB, 1)
Write-Host "free_GB=$freeGb"
