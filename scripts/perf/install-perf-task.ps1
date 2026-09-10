<#
One-time setup for automated profiling elevation (run once; one UAC prompt).

Registers the Scheduled Task "SimPerfEtw" that runs scripts\perf\etw-ctl.ps1
with highest privileges as the current user. Thereafter the non-elevated
performance suite triggers it via "schtasks /run /tn SimPerfEtw" with no
further prompts. ETW kernel sessions require elevation; this task is the only
elevated component.

Install (elevated console, or accept the single UAC prompt when launched via
Start-Process -Verb RunAs):

  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\perf\install-perf-task.ps1

Uninstall:  Unregister-ScheduledTask -TaskName SimPerfEtw -Confirm:$false   (elevated)
#>
#requires -RunAsAdministrator
param()

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ctl = Join-Path $repo "scripts\perf\etw-ctl.ps1"
if (-not (Test-Path $ctl)) { Write-Output "FAIL: missing $ctl"; exit 1 }

$stateDir = Join-Path $repo "ai\temp\perf"
if (-not (Test-Path $stateDir)) { New-Item -ItemType Directory -Path $stateDir -Force | Out-Null }

$action = New-ScheduledTaskAction -Execute "powershell.exe" `
  -Argument "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$ctl`""
$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" `
  -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
  -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Minutes 10)

Register-ScheduledTask -TaskName "SimPerfEtw" -Action $action -Principal $principal `
  -Settings $settings -Description "Elevated wpr start/stop helper for the Simutrans-Extended performance suite (command/status files in ai\temp\perf)." -Force | Out-Null

$task = Get-ScheduledTask -TaskName "SimPerfEtw"
Write-Output "OK: task 'SimPerfEtw' registered (state: $($task.State))"
Write-Output "Test from a NON-elevated console:  schtasks /run /tn SimPerfEtw"
Write-Output "Then check ai\temp\perf\etw-status-*.txt"
exit 0
