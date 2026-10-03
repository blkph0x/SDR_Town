param([Parameter(Mandatory=$true)][string]$Path)
$ErrorActionPreference = 'Stop'
# A Python child of PowerShell 7 can inherit incompatible PSModulePath entries.
Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Security/Microsoft.PowerShell.Security.psd1')
$signature = Get-AuthenticodeSignature -LiteralPath $Path
if ($signature.Status -ne 'Valid' -or
    $signature.SignerCertificate.Subject -notmatch '(^|, )O=Microsoft Corporation(,|$)') {
    throw 'Redistributable must have a valid Microsoft Authenticode signature'
}
[ordered]@{
    status = 'Valid'
    subject = $signature.SignerCertificate.Subject
    thumbprint = $signature.SignerCertificate.Thumbprint
} | ConvertTo-Json -Compress
