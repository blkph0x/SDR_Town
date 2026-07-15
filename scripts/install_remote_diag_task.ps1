param(
    [int]$Port = 8787,
    [Alias("Host")]
    [string]$BindHost = "0.0.0.0",
    [string]$TaskName = "SDR Town Remote Diagnostics",
    [switch]$OpenFirewall,
    [switch]$Remove
)

$ErrorActionPreference = "Stop"

$script = Join-Path $PSScriptRoot "start_remote_diag_server.ps1"
if (-not (Test-Path $script)) {
    throw "start_remote_diag_server.ps1 was not found next to this script."
}

if ($Remove) {
    $existing = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    if ($existing) {
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
        Write-Host "Removed scheduled task: $TaskName"
    } else {
        Write-Host "Scheduled task not installed: $TaskName"
    }
    return
}

$argList = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", "`"$script`"",
    "-Port", "$Port",
    "-Host", "`"$BindHost`""
)
if ($OpenFirewall) {
    $argList += "-OpenFirewall"
}

$action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument ($argList -join " ")
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
$settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit (New-TimeSpan -Hours 0) `
    -MultipleInstances IgnoreNew `
    -RestartCount 3 `
    -RestartInterval (New-TimeSpan -Minutes 1)

$existing = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($existing) {
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
}

Register-ScheduledTask `
    -TaskName $TaskName `
    -Action $action `
    -Trigger $trigger `
    -Settings $settings `
    -Description "Starts the SDR Town local remote-diagnostics JSONL collector at user logon." `
    -Force | Out-Null

Write-Host "Installed scheduled task: $TaskName"
Write-Host "It will start diagnostics on $BindHost`:$Port at user logon."
Write-Host "Run now with: powershell -ExecutionPolicy Bypass -File `"$script`" -Port $Port -Host `"$BindHost`""
