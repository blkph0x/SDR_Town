param(
    [int]$Port = 8787,
    [Alias("Host")]
    [string]$BindHost = "0.0.0.0",
    [string]$OutDir = "$env:APPDATA\SDR_Town\remote_diagnostics",
    [string]$TokenFile = "$env:APPDATA\SDR_Town\remote_diagnostics_token.txt",
    [string]$AdminTokenFile = "$env:APPDATA\SDR_Town\remote_diagnostics_admin_token.txt",
    [string]$ConfigFile = "$env:APPDATA\SDR_Town\SDR Town\remote_diagnostics.json",
    [string]$CollectorUrl = "",
    [switch]$OpenFirewall,
    [switch]$ForceRestart
)

$ErrorActionPreference = "Stop"

$root = $PSScriptRoot | Split-Path -Parent
$server = Join-Path $root "src\tools\remote_diag_server.py"
if (-not (Test-Path $server)) {
    $server = Join-Path $root "tools\diagnostics\remote_diag_server.py"
}
if (-not (Test-Path $server)) {
    throw "remote_diag_server.py was not found under source or packaged tools."
}

function New-DiagnosticsToken {
    $bytes = New-Object byte[] 32
    $rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    try {
        $rng.GetBytes($bytes)
    } finally {
        $rng.Dispose()
    }
    return ([BitConverter]::ToString($bytes) -replace "-", "").ToLowerInvariant()
}

New-Item -ItemType Directory -Force -Path (Split-Path $TokenFile -Parent) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path $AdminTokenFile -Parent) | Out-Null
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path $ConfigFile -Parent) | Out-Null

if (-not (Test-Path $TokenFile)) {
    New-DiagnosticsToken | Set-Content -Path $TokenFile -Encoding ascii
}
$token = (Get-Content -Path $TokenFile -Raw).Trim()
if ([string]::IsNullOrWhiteSpace($token)) {
    $token = New-DiagnosticsToken
    $token | Set-Content -Path $TokenFile -Encoding ascii
}
if (-not (Test-Path $AdminTokenFile)) {
    New-DiagnosticsToken | Set-Content -Path $AdminTokenFile -Encoding ascii
}
$adminToken = (Get-Content -Path $AdminTokenFile -Raw).Trim()
if ([string]::IsNullOrWhiteSpace($adminToken)) {
    $adminToken = New-DiagnosticsToken
    $adminToken | Set-Content -Path $AdminTokenFile -Encoding ascii
}

if ([string]::IsNullOrWhiteSpace($CollectorUrl)) {
    $CollectorUrl = "http://127.0.0.1:$Port/ingest"
}

if ($OpenFirewall) {
    $ruleName = "SDR Town Remote Diagnostics $Port"
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    $isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        Write-Warning "Skipping firewall rule because this PowerShell session is not elevated. Re-run as Administrator with -OpenFirewall if remote clients cannot connect."
    } else {
        $existing = netsh advfirewall firewall show rule name="$ruleName" 2>$null | Out-String
        if ($existing -notmatch $ruleName) {
            netsh advfirewall firewall add rule name="$ruleName" dir=in action=allow protocol=TCP localport=$Port | Out-Null
            if ($LASTEXITCODE -ne 0) {
                Write-Warning "Could not add Windows Firewall rule '$ruleName'. Remote clients may need the port opened manually."
            }
        }
    }
}

$listeners = @(Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue)
if ($listeners.Count -gt 0) {
    $matching = $false
    foreach ($listener in $listeners) {
        $proc = Get-CimInstance Win32_Process -Filter "ProcessId=$($listener.OwningProcess)" -ErrorAction SilentlyContinue
        if ($proc -and $proc.CommandLine -like "*remote_diag_server.py*") {
            $matching = $true
            if ($ForceRestart) {
                Stop-Process -Id $listener.OwningProcess -Force -ErrorAction SilentlyContinue
                $matching = $false
            }
        }
    }
    if ($matching) {
        Write-Host "SDR Town diagnostics server is already listening on port $Port."
    } elseif (-not $ForceRestart) {
        throw "Port $Port is already in use by another process. Pick another -Port or free it first."
    }
}

$logsDir = Join-Path $OutDir "_server_logs"
New-Item -ItemType Directory -Force -Path $logsDir | Out-Null
$stdout = Join-Path $logsDir "server.out.log"
$stderr = Join-Path $logsDir "server.err.log"

$listeners = @(Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue)
if ($listeners.Count -eq 0) {
    $python = (Get-Command python -ErrorAction Stop).Source
    $args = @($server, "--host", $BindHost, "--port", "$Port", "--token", $token, "--admin-token", $adminToken, "--out", $OutDir)
    $proc = Start-Process -FilePath $python -ArgumentList $args -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    Start-Sleep -Milliseconds 500
    Write-Host "Started SDR Town diagnostics server pid=$($proc.Id) on $BindHost`:$Port"
}

$config = [ordered]@{
    enabled = $true
    url = $CollectorUrl
    token = $token
    maxBytesPerMinute = 65536
    maxPayloadBytes = 16384
    minIntervalMs = 1000
    maxQueue = 128
}
$config | ConvertTo-Json -Depth 5 | Set-Content -Path $ConfigFile -Encoding utf8

$healthUrl = "http://127.0.0.1:$Port/health"
$healthy = $false
for ($i = 0; $i -lt 20; ++$i) {
    try {
        $response = Invoke-WebRequest -UseBasicParsing -Uri $healthUrl -TimeoutSec 2
        if ($response.StatusCode -eq 200) {
            $healthy = $true
            break
        }
    } catch {
        Start-Sleep -Milliseconds 250
    }
}
if (-not $healthy) {
    throw "Diagnostics server did not answer $healthUrl"
}

Write-Host "Diagnostics server health OK: $healthUrl"
Write-Host "JSONL output: $OutDir"
Write-Host "App auto-config: $ConfigFile"
Write-Host "Bearer token file: $TokenFile"
Write-Host "Admin token file: $AdminTokenFile"
Write-Host "Admin UI: http://127.0.0.1:$Port/admin?token=$adminToken"
