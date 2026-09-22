[CmdletBinding()]
param(
    [string]$AppRoot = "",
    [int]$TimeoutSeconds = 12,
    [switch]$ProbeDevice,
    [string]$OutputJson = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Add-UniquePath {
    param([System.Collections.Generic.List[string]]$List, [string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) { return }
    try { $expanded = [Environment]::ExpandEnvironmentVariables($Path.Trim('"')) } catch { $expanded = $Path }
    if ([string]::IsNullOrWhiteSpace($expanded)) { return }
    $full = $expanded
    try { $full = [IO.Path]::GetFullPath($expanded) } catch {}
    foreach ($existing in $List) {
        if ([string]::Equals($existing, $full, [StringComparison]::OrdinalIgnoreCase)) { return }
    }
    $List.Add($full)
}

function Get-PeArchitecture {
    param([Parameter(Mandatory=$true)][string]$Path)
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        try {
            $reader = New-Object IO.BinaryReader($stream)
            if ($reader.ReadUInt16() -ne 0x5A4D) { return "not-pe" }
            $stream.Position = 0x3C
            $peOffset = $reader.ReadInt32()
            if ($peOffset -lt 0 -or $peOffset -gt ($stream.Length - 6)) { return "invalid-pe" }
            $stream.Position = $peOffset
            if ($reader.ReadUInt32() -ne 0x00004550) { return "invalid-pe" }
            switch ($reader.ReadUInt16()) {
                0x014c { return "x86" }
                0x8664 { return "x64" }
                0xAA64 { return "arm64" }
                default { return ("machine-0x{0:X4}" -f $_) }
            }
        } finally { $stream.Dispose() }
    } catch {
        return "unreadable"
    }
}

function Get-FileFact {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    $item = Get-Item -LiteralPath $Path
    [pscustomobject]@{
        path = $item.FullName
        architecture = Get-PeArchitecture -Path $item.FullName
        fileVersion = $item.VersionInfo.FileVersion
        productVersion = $item.VersionInfo.ProductVersion
        size = $item.Length
        modifiedUtc = $item.LastWriteTimeUtc.ToString("o")
    }
}

function Get-RegistryStrings {
    $results = New-Object 'System.Collections.Generic.List[string]'
    $keys = @(
        'HKLM:\SOFTWARE\SDRplay\Service\API',
        'HKLM:\SOFTWARE\WOW6432Node\SDRplay\Service\API',
        'HKCU:\SOFTWARE\SDRplay\Service\API',
        'HKCU:\SOFTWARE\WOW6432Node\SDRplay\Service\API'
    )
    foreach ($key in $keys) {
        if (-not (Test-Path -LiteralPath $key)) { continue }
        try {
            $props = Get-ItemProperty -LiteralPath $key
            foreach ($property in $props.PSObject.Properties) {
                if ($property.Name -like 'PS*') { continue }
                if ($property.Value -is [string]) { Add-UniquePath -List $results -Path $property.Value }
            }
        } catch {}
    }
    return @($results)
}

function Invoke-BoundedProcess {
    param(
        [Parameter(Mandatory=$true)][string]$FilePath,
        [Parameter(Mandatory=$true)][string[]]$Arguments,
        [int]$Timeout = 12
    )
    $stdout = [IO.Path]::GetTempFileName()
    $stderr = [IO.Path]::GetTempFileName()
    try {
        $process = Start-Process -FilePath $FilePath -ArgumentList $Arguments -NoNewWindow -PassThru `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $completed = $process.WaitForExit([Math]::Max(1, $Timeout) * 1000)
        if (-not $completed) {
            try { $process.Kill() } catch {}
            try { $process.WaitForExit(2000) | Out-Null } catch {}
        }
        $outText = if (Test-Path $stdout) { Get-Content -LiteralPath $stdout -Raw -ErrorAction SilentlyContinue } else { "" }
        $errText = if (Test-Path $stderr) { Get-Content -LiteralPath $stderr -Raw -ErrorAction SilentlyContinue } else { "" }
        [pscustomobject]@{
            command = $FilePath
            arguments = $Arguments
            completed = [bool]$completed
            timedOut = -not [bool]$completed
            exitCode = if ($completed) { $process.ExitCode } else { $null }
            stdout = [string]$outText
            stderr = [string]$errText
        }
    } finally {
        Remove-Item -LiteralPath $stdout,$stderr -Force -ErrorAction SilentlyContinue
    }
}

if ([string]::IsNullOrWhiteSpace($AppRoot)) {
    $AppRoot = Split-Path -Parent $PSScriptRoot
}
try { $AppRoot = [IO.Path]::GetFullPath($AppRoot) } catch {}
$TimeoutSeconds = [Math]::Max(2, $TimeoutSeconds)

$roots = New-Object 'System.Collections.Generic.List[string]'
Add-UniquePath $roots $AppRoot
Add-UniquePath $roots $env:SDRPLAY_API_DIR
Add-UniquePath $roots $env:SDRPLAY_ROOT
Add-UniquePath $roots $env:SOAPY_SDR_ROOT
Add-UniquePath $roots $env:POTHOS_ROOT
foreach ($registryPath in Get-RegistryStrings) { Add-UniquePath $roots $registryPath }
Add-UniquePath $roots (Join-Path $env:ProgramFiles 'SDRplay\API')
if (${env:ProgramFiles(x86)}) { Add-UniquePath $roots (Join-Path ${env:ProgramFiles(x86)} 'SDRplay\API') }
Add-UniquePath $roots (Join-Path $env:ProgramFiles 'PothosSDR')
if (${env:ProgramFiles(x86)}) { Add-UniquePath $roots (Join-Path ${env:ProgramFiles(x86)} 'PothosSDR') }
if ($env:ProgramData) { Add-UniquePath $roots (Join-Path $env:ProgramData 'radioconda\Library') }

$apiCandidates = New-Object 'System.Collections.Generic.List[string]'
$moduleCandidates = New-Object 'System.Collections.Generic.List[string]'
$soapyUtilCandidates = New-Object 'System.Collections.Generic.List[string]'
foreach ($root in $roots) {
    foreach ($relative in @(
        'sdrplay_api.dll','bin\sdrplay_api.dll','x64\sdrplay_api.dll','x86\sdrplay_api.dll','arm64\sdrplay_api.dll',
        'API\sdrplay_api.dll','API\x64\sdrplay_api.dll','API\x86\sdrplay_api.dll','API\arm64\sdrplay_api.dll',
        'Library\bin\sdrplay_api.dll'
    )) { Add-UniquePath $apiCandidates (Join-Path $root $relative) }
    foreach ($relative in @(
        'sdrPlaySupport.dll','bin\sdrPlaySupport.dll',
        'lib\SoapySDR\modules0.8\sdrPlaySupport.dll','lib64\SoapySDR\modules0.8\sdrPlaySupport.dll',
        'Library\lib\SoapySDR\modules0.8\sdrPlaySupport.dll',
        'Library\lib64\SoapySDR\modules0.8\sdrPlaySupport.dll'
    )) { Add-UniquePath $moduleCandidates (Join-Path $root $relative) }
    foreach ($relative in @('SoapySDRUtil.exe','bin\SoapySDRUtil.exe','Library\bin\SoapySDRUtil.exe')) {
        Add-UniquePath $soapyUtilCandidates (Join-Path $root $relative)
    }
}

$pathUtil = Get-Command SoapySDRUtil.exe -ErrorAction SilentlyContinue
if ($pathUtil) { Add-UniquePath $soapyUtilCandidates $pathUtil.Source }

$apiFiles = @($apiCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | ForEach-Object { Get-FileFact $_ })
$moduleFiles = @($moduleCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | ForEach-Object { Get-FileFact $_ })
$utilFiles = @($soapyUtilCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | ForEach-Object { Get-FileFact $_ })

$processArchitecture = if ([Environment]::Is64BitProcess) {
    if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }
} else { 'x86' }
$architectureWarnings = New-Object 'System.Collections.Generic.List[string]'
foreach ($fact in @($apiFiles + $moduleFiles + $utilFiles)) {
    if ($fact.architecture -in @('x86','x64','arm64') -and $fact.architecture -ne $processArchitecture) {
        $architectureWarnings.Add("Architecture mismatch: $($fact.path) is $($fact.architecture), process is $processArchitecture")
    }
}

$services = @()
try {
    $services = @(Get-CimInstance Win32_Service | Where-Object {
        $_.Name -match 'SDRplay' -or $_.DisplayName -match 'SDRplay'
    } | ForEach-Object {
        [pscustomobject]@{
            name = $_.Name
            displayName = $_.DisplayName
            state = $_.State
            startMode = $_.StartMode
            pathName = $_.PathName
        }
    })
} catch {}

$soapyInfo = $null
$soapyFind = $null
$soapyProbe = $null
if ($utilFiles.Count -gt 0) {
    $util = $utilFiles[0].path
    $soapyInfo = Invoke-BoundedProcess -FilePath $util -Arguments @('--info') -Timeout $TimeoutSeconds
    $soapyFind = Invoke-BoundedProcess -FilePath $util -Arguments @('--find=driver=sdrplay') -Timeout $TimeoutSeconds
    if ($ProbeDevice) {
        $soapyProbe = Invoke-BoundedProcess -FilePath $util -Arguments @('--probe=driver=sdrplay') -Timeout $TimeoutSeconds
    }
}

$issues = New-Object 'System.Collections.Generic.List[string]'
if ($apiFiles.Count -eq 0) { $issues.Add('sdrplay_api.dll was not found in registry, environment, application, SDRplay, PothosSDR, or radioconda layouts.') }
if ($moduleFiles.Count -eq 0) { $issues.Add('sdrPlaySupport.dll was not found in a SoapySDR modules0.8 or bin layout.') }
if ($utilFiles.Count -eq 0) { $issues.Add('SoapySDRUtil.exe was not found; module and device discovery could not be verified.') }
foreach ($warning in $architectureWarnings) { $issues.Add($warning) }
if ($services.Count -eq 0) { $issues.Add('No Windows service with an SDRplay name/display-name was found.') }
elseif (-not ($services | Where-Object { $_.State -eq 'Running' })) { $issues.Add('An SDRplay service was found but none is running.') }
if ($soapyInfo -and ($soapyInfo.timedOut -or $soapyInfo.exitCode -ne 0)) { $issues.Add('SoapySDRUtil --info failed or timed out.') }
if ($soapyFind -and ($soapyFind.timedOut -or $soapyFind.exitCode -ne 0)) { $issues.Add('SoapySDRUtil SDRplay discovery failed or timed out.') }
if ($soapyProbe -and ($soapyProbe.timedOut -or $soapyProbe.exitCode -ne 0)) { $issues.Add('The isolated SDRplay device probe failed or timed out; no main-process open was attempted.') }

$result = [ordered]@{
    schema = 1
    timestampUtc = [DateTime]::UtcNow.ToString('o')
    computer = $env:COMPUTERNAME
    osArchitecture = $env:PROCESSOR_ARCHITECTURE
    processArchitecture = $processArchitecture
    appRoot = $AppRoot
    timeoutSeconds = $TimeoutSeconds
    probeRequested = [bool]$ProbeDevice
    searchRoots = @($roots)
    apiLibraries = $apiFiles
    soapyModules = $moduleFiles
    soapyUtilities = $utilFiles
    services = $services
    soapyInfo = $soapyInfo
    soapyFind = $soapyFind
    soapyProbe = $soapyProbe
    issues = @($issues)
    ready = ($issues.Count -eq 0)
}

$json = $result | ConvertTo-Json -Depth 8
if (-not [string]::IsNullOrWhiteSpace($OutputJson)) {
    $parent = Split-Path -Parent $OutputJson
    if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    [IO.File]::WriteAllText($OutputJson, $json, [Text.UTF8Encoding]::new($false))
}

Write-Host "SDRplay runtime preflight"
Write-Host "  Process architecture: $processArchitecture"
Write-Host "  API libraries:        $($apiFiles.Count)"
Write-Host "  Soapy modules:        $($moduleFiles.Count)"
Write-Host "  Soapy utilities:      $($utilFiles.Count)"
Write-Host "  SDRplay services:     $($services.Count)"
Write-Host "  Ready:                $($result.ready)"
if ($issues.Count -gt 0) {
    Write-Host "Issues:"
    foreach ($issue in $issues) { Write-Host "  - $issue" }
}
Write-Output $json

if ($result.ready) { exit 0 }
exit 2
