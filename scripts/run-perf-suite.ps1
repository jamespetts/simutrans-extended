<#
Simutrans-Extended performance profiling suite (Windows).

Canonical fixture: the gargantuan Bridgewater-Brunel server savegame
bb-10-sep-2023.sve from the user's Simutrans save directory. Run modes:

  Load      - load the save, run at most one step, quit; wall time = load cost.
              (graphical Profile build)
  Capture   - load, then run the game as a server at its normal, realistic
              pace (FIX_RATIO; the savegame's own settings drive frame/step
              pacing; loopback-only, no clients, no announcements) for
              -WindowSec seconds and kill the process. Default exe: the
              graphical Profile build (headless crashes on this fixture,
              see ai/known-bugs.md; pass -Headless to use the headless
              "Profile (server)" build once that is fixed).
  CaptureGui - load, then run as an offline client at normal single-player
              pace (display + simulation, the way players actually run) for
              -WindowSec seconds and kill. Use this to profile graphics-code
              hotspots. (graphical Profile build)
  Times     - the built-in drawing micro-benchmarks (-times) after loading,
              then quit; results are parsed from the log. (graphical build)

For Capture/CaptureGui, profile the window with an external tool: Visual
Studio Performance Profiler (launch or attach; uses the Profile builds'
PDBs), or elevated "wpr" ETW capture via -Etw. Load completion is detected
from a working-set/read-I/O plateau, so the run stays at -debug 1 and
logging never perturbs the profile.

Fast-forward is NOT used for profiling: it distorts realistic pacing (and
never applies to network mode); it is only a tool for reaching a point in
game time quickly [RECOLLECTION:2026-09-09 user statement].

Requires the MSVC "Profile" configurations (release-like: optimised,
NDEBUG, no DEBUG/MSG_LEVEL so DBG-macro logging is compiled out entirely,
but PROFILE defined so -until/-times exist; PDBs generated for profiler
symbol resolution). DEBUG-defined builds ("Optimised debug") are for
optimised debugging, not unbiased profiling: the DEBUG define compiles
DBG-macro calls into hot paths, enables asserts and the DEBUG_FREELIST
allocator instrumentation.

The suite runs in a sandbox workdir (junctions + hardlinked fixture) and
never touches the user's Simutrans profile. All state lives under -WorkDir
(default ai\temp\perf, gitignored; persistent suite state, exempt from
session-end clearing). Pakset defaults per branch:
master -> pak128.Britain-Ex-0.9.4, ex-15 -> pak128.Britain-Ex.

Run via: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run-perf-suite.ps1 -Mode Capture
#>
param(
  [string]$Exe,
  [string]$Save = "$env:USERPROFILE\Documents\Simutrans\save\bb-10-sep-2023.sve",
  [string]$Pakset,
  [string]$WorkDir,
  [ValidateSet("Load","Capture","CaptureGui","Times")]
  [string]$Mode = "Capture",
  [int]$WindowSec = 120,
  [int]$Threads = 4,
  [int]$LoadTimeoutSec = 900,
  [int]$SettleSec = 20,
  [int]$LoadReadFloorMb = 400,
  [int]$ReadQuietMb = 4,
  [int]$LoadWsFloorMb = 2500,
  [int]$WsQuietMb = 64,
  [int]$MinLoadSec = 75,
  [int]$ServerPort = 13353,
  [switch]$Headless,
  [switch]$Etw,
  [switch]$Clean
)

$ErrorActionPreference = "Stop"
if (-not $PSScriptRoot) { Write-Output "FAIL: run via powershell -File"; exit 1 }
$repo = Split-Path -Parent $PSScriptRoot

function Resolve-RepoPath {
  param([string]$path)
  if ([System.IO.Path]::IsPathRooted($path)) { return [System.IO.Path]::GetFullPath($path) }
  return [System.IO.Path]::GetFullPath((Join-Path $repo $path))
}

if (-not $Pakset) {
  $branch = (& git rev-parse --abbrev-ref HEAD) 2>$null
  if ($branch -eq "ex-15") { $Pakset = "pak128.Britain-Ex" } else { $Pakset = "pak128.Britain-Ex-0.9.4" }
}
if (-not $Exe) {
  if ($Mode -eq "Capture" -and $Headless) { $Exe = Join-Path $repo "simutrans\Simutrans-Extended-Profile-server.exe" }
  else { $Exe = Join-Path $repo "simutrans\Simutrans-Extended-Profile.exe" }
}
if (-not $WorkDir) { $WorkDir = Join-Path $repo "ai\temp\perf" }
$Exe = Resolve-RepoPath $Exe
$WorkDir = Resolve-RepoPath $WorkDir
$Save = [System.IO.Path]::GetFullPath($Save)
$fixtureName = Split-Path $Save -Leaf
$saveDir = Join-Path $WorkDir "save"
$resultsRoot = Join-Path $WorkDir "results"
if ($Mode -eq "Capture") { $simLog = Join-Path $WorkDir "simu-server$ServerPort.log" }
else { $simLog = Join-Path $WorkDir "simu.log" }

function Remove-Junctions {
  # rmdir removes the junction link only; NEVER Remove-Item -Recurse through one.
  Get-ChildItem $args[0] -Force -ErrorAction SilentlyContinue |
    Where-Object { $_.Attributes -match "ReparsePoint" } |
    ForEach-Object { cmd /c rmdir $_.FullName }
}

if ($Clean) {
  if (Test-Path $WorkDir) { Remove-Junctions $WorkDir; Remove-Item $WorkDir -Recurse -Force }
  Write-Output "CLEAN: removed $WorkDir"
  exit 0
}

foreach ($path in @($Exe, $Save)) {
  if (-not (Test-Path $path)) { Write-Output "FAIL: missing $path"; exit 1 }
}
$pakTarget = Join-Path $repo "simutrans\$Pakset"
if (-not (Test-Path $pakTarget)) { Write-Output "FAIL: missing pakset $pakTarget"; exit 1 }

# The -until/-times options exist only in DEBUG/PROFILE builds; check the help text.
$helpFile = Join-Path $env:TEMP "sim-perf-help.txt"
cmd /c "`"$Exe`" -h > `"$helpFile`" 2>&1" | Out-Null
if (-not (Select-String -Path $helpFile -Pattern "-until" -Quiet)) {
  Write-Output "FAIL: $Exe has no -until option (not a DEBUG/PROFILE build); build the `"Profile`" configurations"
  exit 1
}

# Sandbox workdir: junctioned pakset/font/text/themes, suite simuconf, hardlinked fixture.
foreach ($d in @($saveDir, (Join-Path $WorkDir "config"), $resultsRoot)) {
  if (-not (Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
}
$junctions = [ordered]@{
  $Pakset = $pakTarget
  "font"  = (Join-Path $repo "simutrans\font")
  "text"  = (Join-Path $repo "simutrans\text")
  "themes" = (Join-Path $repo "simutrans\themes")
}
foreach ($name in $junctions.Keys) {
  $link = Join-Path $WorkDir $name
  if (Test-Path $link) {
    $item = Get-Item $link -Force
    if (($item.Attributes -match "ReparsePoint") -and (($item.Target -join "") -ne $junctions[$name])) {
      cmd /c rmdir $link
      New-Item -ItemType Junction -Path $link -Target $junctions[$name] | Out-Null
    }
  } else {
    New-Item -ItemType Junction -Path $link -Target $junctions[$name] | Out-Null
  }
}
$simuconfLines = @(
  "autosave = 0"
)
if ($Mode -eq "Capture") {
  # Server-paced capture: loopback-only, unpaused with no clients, no announcements.
  # Frame/step pacing intentionally NOT pinned: the savegame's own settings drive it.
  $simuconfLines += @(
    "listen = 127.0.0.1"
    "pause_server_no_clients = 0"
    "announce_server = 0"
    "server_save_game_on_quit = 0"
  )
}
Set-Content -Path (Join-Path $WorkDir "config\simuconf.tab") -Value ($simuconfLines -join "`n")
$fixturePath = Join-Path $saveDir $fixtureName
if (-not (Test-Path $fixturePath)) {
  try { New-Item -ItemType HardLink -Path $fixturePath -Target $Save | Out-Null }
  catch { Write-Output "NOTE: hardlink failed, copying the fixture..."; Copy-Item $Save $fixturePath }
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$res = Join-Path $resultsRoot "$stamp-$Mode"
New-Item -ItemType Directory -Path $res -Force | Out-Null

$commonArgs = "-set_workdir `"$WorkDir`" -singleuser -objects `"$Pakset/`" -load `"$fixtureName`" -threads $Threads -mute -nomidi -nosound -lang en -log"
switch ($Mode) {
  "Load"      { $argLine = "$commonArgs -until 0 -debug 3" }
  "Capture"   { $argLine = "$commonArgs -server $ServerPort -debug 1" }
  "CaptureGui"{ $argLine = "$commonArgs -debug 1" }
  "Times"     { $argLine = "$commonArgs -times -until 0 -debug 3" }
}

$etwOn = $false
if ($Etw -and ($Mode -eq "Capture" -or $Mode -eq "CaptureGui")) {
  # Requires this whole script to be run from an ELEVATED console (wpr needs it).
  & wpr -start CPU -filemode
  if ($LASTEXITCODE -eq 0) { $etwOn = $true } else {
    Write-Warning "wpr start failed (ETW needs an elevated console); continuing without ETW"
  }
}

Remove-Item $simLog -Force -ErrorAction SilentlyContinue
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $Exe -ArgumentList $argLine -WorkingDirectory $WorkDir -PassThru -WindowStyle Minimized
[void]$p.Handle
Write-Host "[$Mode] launched pid=$($p.Id): $Exe"

$loadSec = $null
$exitCode = $null
$failed = @()

if ($Mode -eq "Capture" -or $Mode -eq "CaptureGui") {
  # Load completion = working-set plateau (world memory allocated, growth quiet for
  # 2 samples) OR read-I/O plateau; page-fault image reads trickle on after load,
  # so the WS signal is the reliable one for this fixture.
  $prevMb = -1
  $prevWs = -1
  $quiet = 0
  while ($null -eq $loadSec -and $sw.Elapsed.TotalSeconds -lt $LoadTimeoutSec) {
    Start-Sleep -Seconds 3
    if ($p.HasExited) { $exitCode = $p.ExitCode; break }
    $w = Get-WmiObject Win32_Process -Filter "ProcessId=$($p.Id)"
    $mb = 0
    $wsMb = 0
    if ($w) {
      $mb = [math]::Round($w.ReadTransferCount / 1MB)
      $wsMb = [math]::Round($w.WorkingSetSize / 1MB)
    }
    $readPlateau = ($prevMb -ge 0 -and $mb -ge $LoadReadFloorMb -and ($mb - $prevMb) -lt $ReadQuietMb)
    $wsPlateau = ($prevWs -ge 0 -and $wsMb -ge $LoadWsFloorMb -and [math]::Abs($wsMb - $prevWs) -lt $WsQuietMb)
    if ($readPlateau -or $wsPlateau) {
      $quiet++
      # allocation bursts/lulls mid-load can look quiet; require a minimum load
      # time and three consecutive quiet samples before declaring load complete
      if ($quiet -ge 3 -and $sw.Elapsed.TotalSeconds -ge $MinLoadSec) { $loadSec = [int]$sw.Elapsed.TotalSeconds }
    } else { $quiet = 0 }
    $prevMb = $mb
    $prevWs = $wsMb
  }
  if ($exitCode -ne $null) { $failed += "process exited during load (code $exitCode)" }
  elseif ($null -eq $loadSec) { $failed += "load (WS/read plateau) not confirmed within ${LoadTimeoutSec}s" }
  else {
    Write-Host "[$Mode] load plateau at ${loadSec}s; settling ${SettleSec}s, then ${WindowSec}s window (profile NOW)"
    Start-Sleep -Seconds $SettleSec
    $windowStart = [int]$sw.Elapsed.TotalSeconds
    Start-Sleep -Seconds $WindowSec
    if (-not $p.HasExited) { try { $p.Kill() } catch {}; try { $p.WaitForExit(5000) | Out-Null } catch {} }
    else { $failed += "process exited during window (code $($p.ExitCode))" }
    Write-Host "[$Mode] window covered seconds $windowStart..$($windowStart + $WindowSec) of the run"
  }
  if ($etwOn) {
    $etl = Join-Path $res "trace.etl"
    & wpr -stop $etl
    if ($LASTEXITCODE -eq 0 -and (Test-Path $etl)) { Write-Host "[$Mode] ETW trace: $etl" } else { $failed += "wpr -stop did not produce $etl" }
  }
}
else {
  $timeoutMs = ($LoadTimeoutSec + 300) * 1000
  if (-not $p.WaitForExit($timeoutMs)) {
    $failed += "no clean exit within ${LoadTimeoutSec}s (+300s margin)"
    try { $p.Kill() } catch {}
  } else { $exitCode = $p.ExitCode }
}

$totalSec = [int]$sw.Elapsed.TotalSeconds
$branch = (& git rev-parse --abbrev-ref HEAD) 2>$null
$head = (& git rev-parse --short=7 HEAD) 2>$null
$summary = @(
  "mode:      $Mode"
  "date:      $stamp"
  "branch:    $branch @ $head"
  "exe:       $Exe ($($(Get-Item $Exe).LastWriteTime))"
  "pakset:    $Pakset"
  "fixture:   $Save"
  "threads:   $Threads"
  "load_s:    $loadSec$(if ($Mode -eq 'Capture' -or $Mode -eq 'CaptureGui') { ' (plateau approx, +' + $SettleSec + 's settle before window)' })"
  "window_s:  $(if ($Mode -eq 'Capture' -or $Mode -eq 'CaptureGui') { $WindowSec })"
  "port:      $(if ($Mode -eq 'Capture') { $ServerPort })"
  "total_s:   $totalSec"
  "exit:      $(if ($exitCode -ne $null) { $exitCode } else { 'killed' })"
)

if (Test-Path $simLog) {
  Copy-Item $simLog (Join-Path $res "simu.log") -Force
  $lines = @(Get-Content $simLog)
  $embedded = if ($lines.Count -gt 0 -and $lines[0] -match '#([0-9a-f]{7,12})') { $Matches[1] } else { "?" }
  $summary += "revision:  $embedded"
  $summary += "log_lines: $($lines.Count)"
  $summary += "errors:    $(@($lines | Select-String -Pattern '^Error|FATAL').Count) (startup menu errors expected)"
  if ($Mode -eq "Times") {
    $bench = $lines | Select-String -Pattern 'Message: ([\w>\(\)\-\.]+):\s+(\d+) iterations took (\d+) ms'
    if ($bench) {
      $summary += "benchmarks:"
      $bench | ForEach-Object { $summary += "  $($_.Matches[0].Groups[1].Value): $($_.Matches[0].Groups[2].Value) iterations, $($_.Matches[0].Groups[3].Value) ms" }
    } else { $failed += "no -times benchmark lines found in log" }
  }
}
else { $failed += "no simu log produced" }

if ($failed.Count -gt 0) { $summary += "failures:  $($failed -join ' | ')" }
Set-Content -Path (Join-Path $res "summary.txt") -Value ($summary -join "`n")
$summary | ForEach-Object { Write-Output $_ }
if ($failed.Count -gt 0) { Write-Output "RESULT: FAIL"; exit 1 }
Write-Output "RESULT: PASS ($res)"
exit 0
