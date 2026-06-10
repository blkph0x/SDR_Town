# Quick launcher for SDR Town dev releases
# Usage (in a fresh PowerShell):
#   .\release.ps1          # uses 0.2.7 (or edit default)
#   .\release.ps1 0.2.8    # for next dev build

param([string]$Version = "0.2.7")

& "$PSScriptRoot\scripts\release.ps1" -Version $Version

Write-Host "`nTip: For even faster future runs, add this folder to PATH or create an alias." -ForegroundColor DarkGray
Write-Host "First time only: run 'gh auth login' in a new shell after gh is installed." -ForegroundColor DarkGray
