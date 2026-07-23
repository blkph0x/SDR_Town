param(
    [Parameter(Mandatory = $true)][string]$ManifestPath,
    [string]$KeyHex = "a4f2c91e8b7d603548e1f9a2b6c3d0e7f5a8192c4e6b0d3f7a9281c5e0b4d6f8"
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path $ManifestPath)) {
    throw "Manifest not found: $ManifestPath"
}

$bytes = [System.IO.File]::ReadAllBytes($ManifestPath)
$keyBytes = [byte[]]::new($KeyHex.Length / 2)
for ($i = 0; $i -lt $keyBytes.Length; $i++) {
    $keyBytes[$i] = [Convert]::ToByte($KeyHex.Substring($i * 2, 2), 16)
}
$hmac = New-Object System.Security.Cryptography.HMACSHA256 (, $keyBytes)
$hashBytes = $hmac.ComputeHash($bytes)
$sigPath = "$ManifestPath.sig"
[Convert]::ToBase64String($hashBytes) | Set-Content $sigPath -Encoding ascii -NoNewline
Write-Host "Wrote manifest signature: $sigPath"
