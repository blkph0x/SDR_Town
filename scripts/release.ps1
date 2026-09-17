# scripts/release.ps1
# One-command dev release helper for SDR Town
# Run from project root after a successful build/fix session.

param(
    [Parameter(Mandatory = $true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [ValidateSet("stable", "experimental")]
    [string]$Channel = "experimental",
    [string]$RemoteDiagnosticsUrl = "",
    [string]$RemoteDiagnosticsTokenFile = "$env:APPDATA\SDR_Town\remote_diagnostics_token.txt",
    [switch]$SkipPush,
    [switch]$SkipAssets
)

$ErrorActionPreference = "Stop"

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE; release stopped."
    }
}

Write-Host "=== SDR Town $Channel Release v$Version ===" -ForegroundColor Cyan

$root = $PSScriptRoot | Split-Path -Parent
Set-Location $root

$branch = git symbolic-ref --quiet --short HEAD
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($branch)) {
    throw "Release requires an attached source branch."
}
$dirty = git status --porcelain --untracked-files=normal
if ($LASTEXITCODE -ne 0 -or $dirty) {
    throw "Commit reviewed source changes and exclude local artifacts before releasing."
}
$existingTag = git tag --list "v$Version"
if ($LASTEXITCODE -ne 0 -or $existingTag) {
    throw "Tag v$Version already exists or could not be checked."
}

$cmakeText = Get-Content CMakeLists.txt -Raw
$escapedVersion = [regex]::Escape($Version)
if ($cmakeText -notmatch "project\(SDR_Town VERSION\s+$escapedVersion\s+LANGUAGES CXX\)") {
    throw "CMakeLists.txt project version must be $Version before packaging this release."
}

# 1. Ensure clean branded build
Write-Host "`n[1/6] Running clean deploy + windeployqt + cpack..." -ForegroundColor Yellow
Invoke-Checked cmake @('-S', '.', '-B', 'build', '-DSDR_TOWN_ENABLE_SSTV_IMAGES=ON')
Invoke-Checked cmake @('--build', 'build', '--config', 'Release', '--target', 'deploy', 'sdr_town_tests', 'sdr_town_workspace_tests', '-j', '4')
Invoke-Checked ctest @('--test-dir', 'build', '-C', 'Release', '--output-on-failure')

$qtWindeploy = "C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe"
if (Test-Path $qtWindeploy) {
    Push-Location build\bin\Release
    try {
        Invoke-Checked $qtWindeploy @('SDR_Town.exe', '--no-compiler-runtime', '--no-system-d3d-compiler')
    } finally { Pop-Location }
} else {
    throw "windeployqt is required for a verified release package."
}

Invoke-Checked cmake @('--build', 'build', '--config', 'Release', '--target', 'deploy', '-j', '4')

if (-not [string]::IsNullOrWhiteSpace($RemoteDiagnosticsUrl)) {
    $portableStaging = "build\deploy_staging"
    if (-not (Test-Path $portableStaging)) {
        throw "Deploy staging was not created; cannot inject remote diagnostics config."
    }
    if (-not (Test-Path $RemoteDiagnosticsTokenFile)) {
        throw "Remote diagnostics URL was supplied, but token file does not exist: $RemoteDiagnosticsTokenFile"
    }
    $diagToken = (Get-Content -Path $RemoteDiagnosticsTokenFile -Raw).Trim()
    if ([string]::IsNullOrWhiteSpace($diagToken)) {
        throw "Remote diagnostics token file is empty: $RemoteDiagnosticsTokenFile"
    }
    $diagConfig = [ordered]@{
        enabled = $true
        url = $RemoteDiagnosticsUrl
        token = $diagToken
        maxBytesPerMinute = 65536
        maxPayloadBytes = 49152
        minIntervalMs = 1000
        maxQueue = 128
    }
    $diagConfigPath = Join-Path $portableStaging "remote_diagnostics.json"
    $diagConfig | ConvertTo-Json -Depth 5 | Set-Content -Path $diagConfigPath -Encoding utf8
    Write-Host "  Injected packaged remote diagnostics config: $RemoteDiagnosticsUrl"
}

Invoke-Checked cpack @('-G', 'NSIS', '-C', 'Release', '--config', 'build/CPackConfig.cmake')

$setupName = "SDR_Town-$Version-win64-setup.exe"
$setup = @(
    $setupName,
    "build\$setupName"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $setup) {
    $setup = Get-ChildItem . -Recurse -Filter $setupName -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '\\_CPack_Packages\\' } |
        Select-Object -First 1 -ExpandProperty FullName
}

if (-not $setup -or -not (Test-Path $setup)) {
    throw "Could not find the generated setup.exe for v$Version"
}

$portable = "build\deploy_staging"
if (Test-Path $portable) {
    Compress-Archive -Path "$portable\*" -DestinationPath "SDR_Town-$Version-win64-portable.zip" -Force
}

# 2. Compute real hashes
Write-Host "`n[2/6] Computing SHA256 and updating manifests..." -ForegroundColor Yellow
$setupHash = (Get-FileHash $setup -Algorithm SHA256).Hash.ToLower()
$setupSize = (Get-Item $setup).Length
$setupShaFile = "SDR_Town-$Version-win64-setup.exe.sha256"
"$setupHash  $setupName" | Set-Content $setupShaFile -Encoding ascii

$portableZip = "SDR_Town-$Version-win64-portable.zip"
$portableHash = if (Test-Path $portableZip) { (Get-FileHash $portableZip -Algorithm SHA256).Hash.ToLower() } else { "" }
$controlDll = "SdrTownControl-$Version-win64.dll"
Copy-Item -LiteralPath "$portable\SdrTownControl.dll" -Destination $controlDll -Force
$controlHash = (Get-FileHash $controlDll -Algorithm SHA256).Hash.ToLower()
"$controlHash  $controlDll" | Set-Content "$controlDll.sha256" -Encoding ascii

# Update update.json
$manifest = Get-Content update.json | ConvertFrom-Json
$manifest.version = $Version
$manifest.tag = "v$Version"
$manifest.notes_url = "https://github.com/Blkph0x/SDR_Town/releases/tag/v$Version"
$manifest.channel = $Channel
$manifest.published = (Get-Date -Format "yyyy-MM-dd")
$manifest.installer.url = "https://github.com/Blkph0x/SDR_Town/releases/download/v$Version/SDR_Town-$Version-win64-setup.exe"
$manifest.installer.sha256 = $setupHash
$manifest.installer.size = $setupSize
$manifest | ConvertTo-Json -Depth 10 | Set-Content update.json -Encoding utf8

& "$PSScriptRoot\sign_update_manifest.ps1" -ManifestPath (Join-Path $root "update.json")

# Update SHA256SUMS.txt
@"
$setupHash  SDR_Town-$Version-win64-setup.exe
$portableHash  SDR_Town-$Version-win64-portable.zip
$controlHash  $controlDll
"@ | Set-Content SHA256SUMS.txt -Encoding ascii

Invoke-Checked python @('scripts/verify_release.py', '--version', $Version, '--installer', $setup)

Write-Host "  update.json and SHA256SUMS.txt updated with fresh hashes."

# 3. Commit metadata
Invoke-Checked git @('add', 'update.json', 'update.json.sig', 'SHA256SUMS.txt')
Invoke-Checked git @('commit', '-m', "release: v$Version experimental assets and signed manifest")

if (-not $SkipPush) {
    Write-Host "`n[3/6] Pushing code + tag..." -ForegroundColor Yellow
    if (git tag --list "v$Version") {
        throw "Tag v$Version already exists. Pick a new version instead of overwriting a published release."
    }
    Invoke-Checked git @('tag', '-a', "v$Version", '-m', "SDR Town $Version")
    Invoke-Checked git @('push', 'origin', $branch)
    Invoke-Checked git @('push', 'origin', "v$Version")
}

if (-not $SkipAssets) {
    Write-Host "`n[4/6] Uploading assets to GitHub Release (using gh)..." -ForegroundColor Yellow
    $gh = "C:\Program Files\GitHub CLI\gh.exe"
    if (-not (Test-Path $gh)) {
        $gh = (Get-Command gh -ErrorAction SilentlyContinue).Source
    }
    if ($gh -and (Test-Path $gh)) {
        $assets = @($setup, $setupShaFile, $portableZip, $controlDll, "$controlDll.sha256", "update.json", "update.json.sig", "SHA256SUMS.txt")
        $notes = if ($Channel -eq "experimental") { "Experimental tester build. Fresh installer, portable ZIP, manifest, and hashes for the in-app updater." } else { "Stable build. Fresh installer, portable ZIP, manifest, and hashes for the in-app updater." }
        $ghArgs = @("release", "create", "v$Version") + $assets + @("--title", "SDR Town $Version ($Channel)", "--notes", $notes, "--repo", "Blkph0x/SDR_Town", "--verify-tag", "--latest")
        Invoke-Checked $gh $ghArgs
        Write-Host "  Assets uploaded via gh."
    } else {
        throw "gh CLI not found; release assets have not been uploaded."
    }
}

Write-Host "`n=== Release v$Version ready ===" -ForegroundColor Green
Write-Host "Local fresh assets are in the root."
Write-Host "Run the app and test Help > Check for Updates."
