# scripts/release.ps1
# One-command dev release helper for SDR Town
# Run from project root after a successful build/fix session.

param(
    [string]$Version = "0.2.1",
    [switch]$SkipPush,
    [switch]$SkipAssets
)

$ErrorActionPreference = "Stop"

Write-Host "=== SDR Town Fast Dev Release v$Version ===" -ForegroundColor Cyan

$root = $PSScriptRoot | Split-Path -Parent
Set-Location $root

# 1. Ensure clean branded build
Write-Host "`n[1/6] Running clean deploy + windeployqt + cpack..." -ForegroundColor Yellow
cmake --build build --config Release --target deploy | Out-Null

$qtWindeploy = "C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe"
if (Test-Path $qtWindeploy) {
    Push-Location build\bin\Release
    & $qtWindeploy SDR_Town.exe --no-compiler-runtime --no-system-d3d-compiler | Out-Null
    Pop-Location
} else {
    Write-Warning "windeployqt not found at expected path. Using existing deployed tree."
}

cmake --build build --config Release --target deploy | Out-Null
cpack -G NSIS -C Release --config build/CPackConfig.cmake | Out-Null

$setup = "build\SDR_Town-$Version-win64-setup.exe"
if (-not (Test-Path $setup)) {
    # fallback if cpack put it in build root
    $setup = Get-ChildItem build -Recurse -Filter "*$Version*setup*.exe" | Select-Object -First 1 -Expand FullName
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

$portableZip = "SDR_Town-$Version-win64-portable.zip"
$portableHash = if (Test-Path $portableZip) { (Get-FileHash $portableZip -Algorithm SHA256).Hash.ToLower() } else { "" }

# Update update.json
$manifest = Get-Content update.json | ConvertFrom-Json
$manifest.version = $Version
$manifest.tag = "v$Version"
$manifest.published = (Get-Date -Format "yyyy-MM-dd")
$manifest.installer.url = "https://github.com/Blkph0x/SDR_Town/releases/download/v$Version/SDR_Town-$Version-win64-setup.exe"
$manifest.installer.sha256 = $setupHash
$manifest.installer.size = $setupSize
$manifest | ConvertTo-Json -Depth 10 | Set-Content update.json -Encoding utf8

# Update SHA256SUMS.txt
@"
$setupHash  SDR_Town-$Version-win64-setup.exe
$portableHash  SDR_Town-$Version-win64-portable.zip
"@ | Set-Content SHA256SUMS.txt -Encoding ascii

Write-Host "  update.json and SHA256SUMS.txt updated with fresh hashes."

# 3. Commit metadata
git add update.json SHA256SUMS.txt
git commit -m "release: v$Version dev build - updated manifests + SHA256SUMS" --allow-empty | Out-Null

if (-not $SkipPush) {
    Write-Host "`n[3/6] Pushing code + tag..." -ForegroundColor Yellow
    git tag -f -a "v$Version" -m "SDR Town $Version (dev release for updater testing)"
    git push origin master --tags
}

if (-not $SkipAssets) {
    Write-Host "`n[4/6] Uploading assets to GitHub Release (using gh)..." -ForegroundColor Yellow
    $gh = "C:\Program Files\GitHub CLI\gh.exe"
    if (-not (Test-Path $gh)) {
        $gh = (Get-Command gh -ErrorAction SilentlyContinue).Source
    }
    if ($gh -and (Test-Path $gh)) {
        & $gh release create "v$Version" --title "SDR Town $Version" --prerelease --notes "Dev build. See DESIGN.md for changes. Fresh assets for in-app updater testing." --repo Blkph0x/SDR_Town $setup $portableZip update.json SHA256SUMS.txt --clobber 2>&1 | Out-Null
        Write-Host "  Assets uploaded via gh."
    } else {
        Write-Host "  gh CLI not found in expected locations. Please run the manual upload or install gh."
        Write-Host "  Direct link: https://github.com/Blkph0x/SDR_Town/releases/edit/v$Version"
    }
}

Write-Host "`n=== Release v$Version ready ===" -ForegroundColor Green
Write-Host "Local fresh assets are in the root."
Write-Host "Run the app and test Help > Check for Updates."
