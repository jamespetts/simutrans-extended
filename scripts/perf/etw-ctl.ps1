<#
Elevated ETW control helper for the performance suite.

This script is designed to be run ONLY by the "SimPerfEtw" Scheduled Task
(registered once, elevated, by install-perf-task.ps1). It exists because ETW
kernel sessions require elevation, while the agent/suite shell is
non-elevated. Protocol:

  1. Caller (non-elevated) writes a command file:
       ai\temp\perf\etw-command.txt
       with key: value lines:
         id:      <unique id chosen by the caller>
         action:  start | stop | cancel
         etl:     <target .etl path>     (required for stop)
         profile: <.wprp path>           (optional for start; default cpu-sample.wprp)
  2. Caller triggers the task:  schtasks /run /tn SimPerfEtw
  3. This script executes the command with WPT wpr.exe and writes:
       ai\temp\perf\etw-status-<id>.txt
     which the caller polls for.

Uses the WPT wpr.exe; the inbox wpr (System32) has a broken -stop
(RPC_E_CHANGED_MODE) on this machine — do not use it.
#>
param()

$ErrorActionPreference = "Continue"
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$stateDir = Join-Path $repo "ai\temp\perf"
$wpr = "C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\wpr.exe"
$cmdFile = Join-Path $stateDir "etw-command.txt"
$defaultProfile = Join-Path $repo "scripts\perf\cpu-sample.wprp"

function Write-Status([string]$id, [string[]]$lines) {
  $sf = Join-Path $stateDir "etw-status-$id.txt"
  Set-Content -Path $sf -Value ($lines -join "`n") -Encoding utf8
}

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  Write-Output "FATAL: etw-ctl must run elevated (via the SimPerfEtw task)"
  exit 1
}
if (-not (Test-Path $cmdFile)) { Write-Output "FATAL: no command file $cmdFile"; exit 1 }
if (-not (Test-Path $wpr)) { Write-Output "FATAL: WPT wpr.exe not found at $wpr"; exit 1 }

$cmd = @{}
foreach ($line in (Get-Content $cmdFile)) {
  if ($line -match '^\s*(\w+)\s*:\s*(.*)$') { $cmd[$Matches[1].ToLower()] = $Matches[2].Trim() }
}
Remove-Item $cmdFile -Force -ErrorAction SilentlyContinue

$id = $cmd["id"]
if (-not $id) { Write-Output "FATAL: command file has no id"; exit 1 }
$action = $cmd["action"]
$status = @("id: $id", "action: $action", "time: $(Get-Date -Format o)")

switch ($action) {
  "start" {
    $profile = if ($cmd["profile"]) { $cmd["profile"] } else { $defaultProfile }
    $out = & $wpr -start "$profile!CpuSample.Light" -filemode 2>&1 | Out-String
    $status += "exit: $LASTEXITCODE"
    $status += $out.Trim()
  }
  "stop" {
    $etl = $cmd["etl"]
    if (-not $etl) { $status += "exit: 1"; $status += "FATAL: stop requires etl:"; Write-Status $id $status; exit 1 }
    $out = & $wpr -stop $etl 2>&1 | Out-String
    $status += "exit: $LASTEXITCODE"
    $status += "etl_exists: $(Test-Path $etl)"
    if (Test-Path $etl) { $status += "etl_mb: $([math]::Round((Get-Item $etl).Length/1MB,1))" }
    $status += $out.Trim()
  }
  "cancel" {
    $out = & $wpr -cancel 2>&1 | Out-String
    $status += "exit: $LASTEXITCODE"
    $status += $out.Trim()
  }
  default {
    $status += "exit: 1"
    $status += "FATAL: unknown action '$action'"
  }
}
Write-Status $id $status
$status | ForEach-Object { Write-Output $_ }
exit 0
