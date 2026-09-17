$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'release.ps1'), [ref]$tokens, [ref]$errors)
if ($errors) { throw $errors }
$function = $ast.Find({ param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Invoke-Checked'
}, $true)
if (-not $function) { throw 'Checked native command wrapper missing' }
. ([scriptblock]::Create($function.Extent.Text))
Invoke-Checked $env:COMSPEC @('/c', 'exit', '0')
$failed = $false
try { Invoke-Checked $env:COMSPEC @('/c', 'exit', '7') }
catch { $failed = $_.Exception.Message -match 'exit code 7' }
if (-not $failed) { throw 'Nonzero native result was not rejected' }

$temp = Join-Path ([System.IO.Path]::GetTempPath()) ('sdr-sign-test-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temp | Out-Null
try {
    $manifest = Join-Path $temp 'update.json'
    '{}' | Set-Content -LiteralPath $manifest -Encoding ascii
    $public = Join-Path $root 'resources/update_manifest_ed25519_pub.inc'
    $before = (Get-FileHash -LiteralPath $public).Hash
    & "$PSScriptRoot/sign_update_manifest.ps1" -ManifestPath $manifest
    if ((Get-Item -LiteralPath "$manifest.sig").Length -ne 88) { throw 'Signature missing' }
    if ((Get-FileHash -LiteralPath $public).Hash -ne $before) { throw 'Trust anchor changed' }
    $wrong = Join-Path $temp 'wrong.inc'
    ('"' + ('00' * 32) + '"') | Set-Content -LiteralPath $wrong -Encoding ascii
    $rejected = $false
    try { & "$PSScriptRoot/sign_update_manifest.ps1" -ManifestPath $manifest -PublicKeyIncPath $wrong }
    catch { $rejected = $_.Exception.Message -match 'does not match embedded public key' }
    if (-not $rejected) { throw 'Mismatched trust anchor was not rejected' }
    Write-Output 'PASS native command failure, real signing, unchanged trust anchor and mismatched-key rejection'
} finally {
    # Delete only this explicitly constructed temporary directory's known files.
    foreach ($name in @('update.json', 'update.json.sig', 'wrong.inc')) {
        $file = Join-Path $temp $name
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force }
    }
    Remove-Item -LiteralPath $temp
}
