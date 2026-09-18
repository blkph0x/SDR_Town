param(
    [Parameter(Mandatory = $true)][string]$ManifestPath,
    [string]$PrivateKeyPath = $env:SDR_TOWN_UPDATE_SIGNING_KEY,
    [string]$PublicKeyIncPath = "",
    [switch]$GenerateKeyPair
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($PublicKeyIncPath)) {
    $PublicKeyIncPath = Join-Path $root "resources\update_manifest_ed25519_pub.inc"
}

function Find-OpenSsl {
    $candidates = @(
        "openssl",
        "C:\Program Files\Git\usr\bin\openssl.exe",
        "C:\Program Files\OpenSSL-Win64\bin\openssl.exe"
    )
    foreach ($candidate in $candidates) {
        $resolved = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($resolved) { return $resolved.Source }
    }
    return $null
}

function Invoke-OpenSsl {
    param([string[]]$Arguments)
    & $openssl @Arguments
    if ($LASTEXITCODE -ne 0) { throw "OpenSSL failed ($LASTEXITCODE)." }
}

function Write-PublicKeyInc {
    param([string]$PubDerPath, [string]$IncPath)
    $der = [System.IO.File]::ReadAllBytes($PubDerPath)
    if ($der.Length -lt 32) {
        throw "Unexpected Ed25519 public key DER length: $($der.Length)"
    }
    $pub = $der[-32..-1]
    $hex = -join ($pub | ForEach-Object { $_.ToString("x2") })
    @"
// Embedded Ed25519 public key (32 bytes hex). Updated by scripts/sign_update_manifest.ps1.
// Private signing key must NEVER be committed.
"$hex"

"@ | Set-Content -Path $IncPath -Encoding ascii -NoNewline
    Write-Host "Updated embedded public key: $IncPath"
}

$openssl = Find-OpenSsl
if (-not $openssl) {
    throw "OpenSSL not found. Install OpenSSL or Git for Windows to sign update manifests."
}

if ($GenerateKeyPair) {
    if ([string]::IsNullOrWhiteSpace($PrivateKeyPath)) {
        $PrivateKeyPath = Join-Path $env:APPDATA "SDR_Town\update_manifest_ed25519.pem"
    }
    $keyDir = Split-Path -Parent $PrivateKeyPath
    if (-not (Test-Path $keyDir)) {
        New-Item -ItemType Directory -Path $keyDir | Out-Null
    }
    Invoke-OpenSsl @('genpkey', '-algorithm', 'ED25519', '-out', $PrivateKeyPath)
    $pubDer = [System.IO.Path]::GetTempFileName()
    Invoke-OpenSsl @('pkey', '-in', $PrivateKeyPath, '-pubout', '-outform', 'DER', '-out', $pubDer)
    Write-PublicKeyInc -PubDerPath $pubDer -IncPath $PublicKeyIncPath
    Remove-Item $pubDer -Force
    Write-Host "Generated signing keypair."
    Write-Host "  Private key (keep secret): $PrivateKeyPath"
    Write-Host "  Public key embedded in:    $PublicKeyIncPath"
    return
}

if (-not (Test-Path $ManifestPath)) {
    throw "Manifest not found: $ManifestPath"
}
if ([string]::IsNullOrWhiteSpace($PrivateKeyPath)) {
    $PrivateKeyPath = Join-Path $env:APPDATA "SDR_Town\update_manifest_ed25519.pem"
}
if (-not (Test-Path $PrivateKeyPath)) {
    throw "Private signing key not found: $PrivateKeyPath`nRun: scripts/sign_update_manifest.ps1 -GenerateKeyPair"
}

# DEC-0090: ordinary signing must not rotate the application's trust anchor.
$pubDer = [System.IO.Path]::GetTempFileName()
try {
    Invoke-OpenSsl @('pkey', '-in', $PrivateKeyPath, '-pubout', '-outform', 'DER', '-out', $pubDer)
    $der = [System.IO.File]::ReadAllBytes($pubDer)
    if ($der.Length -ne 44) { throw "Unexpected Ed25519 public key length." }
    $hex = -join ($der[-32..-1] | ForEach-Object { $_.ToString('x2') })
    $embedded = Get-Content $PublicKeyIncPath -Raw
    if ($embedded -notmatch ('"' + [regex]::Escape($hex) + '"')) {
        throw "Signing key does not match embedded public key. Rebuild after an explicit key rotation."
    }
} finally { Remove-Item -LiteralPath $pubDer -Force }

$sigBin = [System.IO.Path]::GetTempFileName()
try {
    Invoke-OpenSsl @('pkeyutl', '-sign', '-inkey', $PrivateKeyPath, '-rawin', '-in', $ManifestPath, '-out', $sigBin)
    $sigBytes = [System.IO.File]::ReadAllBytes($sigBin)
    if ($sigBytes.Length -ne 64) {
        throw "Expected 64-byte Ed25519 signature, got $($sigBytes.Length)"
    }
    $sigPath = "$ManifestPath.sig"
    [Convert]::ToBase64String($sigBytes) | Set-Content $sigPath -Encoding ascii -NoNewline
    Write-Host "Wrote manifest signature: $sigPath"
} finally {
    if (Test-Path $sigBin) { Remove-Item $sigBin -Force }
}
