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

For Capture/CaptureGui, -Trace profiles the run automatically: the suite
starts a sampled-CPU ETW capture (WPT wpr + scripts\perf\cpu-sample.wprp)
around the measurement window, stops it, extracts function-level hotspots
with the TraceEvent analyzer (ai\tools\perf\etl-hotspots.exe) into
hotspots-self.csv / hotspots-incl.csv in the results dir, and deletes the
raw ETL on success (kept on failure). ETW needs elevation: that is handled
by the "SimPerfEtw" scheduled task (one-time elevated install:
scripts\perf\install-perf-task.ps1), so the suite itself runs non-elevated.
-TracePhase Window (default) traces the steady-state window after the load
plateau + settle; -TracePhase Load traces the load phase itself.
Load completion is detected from a working-set/read-I/O plateau, so the run
stays at -debug 1 and logging never perturbs the profile. Without -Trace,
an external profiler (Visual Studio Performance Profiler, attach; PDBs sit
next to the exe) can still be used manually during the window.

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
Traced:  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run-perf-suite.ps1 -Mode Capture -Trace
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
  [switch]$Trace,
  [ValidateSet("Window","Load")]
  [string]$TracePhase = "Window",
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
  # git -C: the suite must work from any working directory (e.g. elevated launches from System32)
  $branch = (& git -C $repo rev-parse --abbrev-ref HEAD 2>$null | Out-String).Trim()
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

# -Trace preflight: WPT wpr, the SimPerfEtw elevation task, and the analyzer.
$analyzer = Join-Path $repo "ai\tools\perf\etl-hotspots.exe"
if ($Trace) {
  if ($Mode -ne "Capture" -and $Mode -ne "CaptureGui") { Write-Output "FAIL: -Trace only applies to Capture/CaptureGui"; exit 1 }
  if (-not (Test-Path "C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\wpr.exe")) {
    Write-Output "FAIL: WPT wpr.exe not found"; exit 1
  }
  & schtasks /query /tn SimPerfEtw 2>$null | Out-Null
  if ($LASTEXITCODE -ne 0) {
    Write-Output "FAIL: scheduled task SimPerfEtw missing; run scripts\perf\install-perf-task.ps1 elevated once"
    exit 1
  }
  if (-not (Test-Path $analyzer)) { Write-Output "FAIL: analyzer missing at $analyzer"; exit 1 }
}

function Invoke-Etw {
  # Command/status-file protocol with the elevated SimPerfEtw task (scripts\perf\etw-ctl.ps1).
  # Returns $null on success, an error string otherwise.
  param([string]$Action, [string]$Etl = "")
  $id = "$stamp-" + [guid]::NewGuid().ToString("N").Substring(0,6)
  $lines = @("id: $id", "action: $Action")
  if ($Etl) { $lines += "etl: $Etl" }
  Set-Content -Path (Join-Path $WorkDir "etw-command.txt") -Value ($lines -join "`n") -Encoding utf8
  & schtasks /run /tn SimPerfEtw | Out-Null
  $sf = Join-Path $WorkDir "etw-status-$id.txt"
  $waited = 0
  while (-not (Test-Path $sf) -and $waited -lt 600) { Start-Sleep -Seconds 2; $waited += 2 }
  if (-not (Test-Path $sf)) { return "TIMEOUT waiting for etw-status-$id.txt" }
  $content = @(Get-Content $sf)
  Remove-Item $sf -Force -ErrorAction SilentlyContinue
  $exitLine = $content | Select-String -Pattern '^exit: (-?\d+)' | Select-Object -First 1
  $code = 0
  if ($exitLine) { $code = [int]$exitLine.Matches[0].Groups[1].Value }
  if ($code -ne 0) { return "wpr $Action exit ${code}: $($content -join ' | ')" }
  return $null
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

# Load-phase tracing starts before the process launches; window-phase tracing
# starts after the load plateau + settle (see below).
$traceOn = $false
$failed = @()
$etlPath = Join-Path $res "trace.etl"
if ($Trace -and $TracePhase -eq "Load") {
  $err = Invoke-Etw "start"
  if ($err) { $failed += "trace start: $err" } else { $traceOn = $true }
}

Remove-Item $simLog -Force -ErrorAction SilentlyContinue
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $Exe -ArgumentList $argLine -WorkingDirectory $WorkDir -PassThru -WindowStyle Minimized
[void]$p.Handle
Write-Host "[$Mode] launched pid=$($p.Id): $Exe"

$loadSec = $null
$exitCode = $null

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
    if ($Trace -and $TracePhase -eq "Load") {
      # Load-phase trace: stop at the plateau; no steady-state window.
      $err = Invoke-Etw "stop" $etlPath
      if ($err) { $failed += "trace stop: $err" }
      if (-not $p.HasExited) { try { $p.Kill() } catch {}; try { $p.WaitForExit(5000) | Out-Null } catch {} }
      Write-Host "[$Mode] load-phase trace stopped at plateau (${loadSec}s)"
    }
    else {
      Write-Host "[$Mode] load plateau at ${loadSec}s; settling ${SettleSec}s, then ${WindowSec}s window"
      Start-Sleep -Seconds $SettleSec
      if ($Trace) {
        $err = Invoke-Etw "start"
        if ($err) { $failed += "trace start: $err" } else { $traceOn = $true }
      }
      $windowStart = [int]$sw.Elapsed.TotalSeconds
      Start-Sleep -Seconds $WindowSec
      if ($traceOn) {
        $err = Invoke-Etw "stop" $etlPath
        if ($err) { $failed += "trace stop: $err" }
      }
      if (-not $p.HasExited) { try { $p.Kill() } catch {}; try { $p.WaitForExit(5000) | Out-Null } catch {} }
      else { $failed += "process exited during window (code $($p.ExitCode))" }
      Write-Host "[$Mode] window covered seconds $windowStart..$($windowStart + $WindowSec) of the run"
    }
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
$branch = (& git -C $repo rev-parse --abbrev-ref HEAD 2>$null | Out-String).Trim()
$head = (& git -C $repo rev-parse --short=7 HEAD 2>$null | Out-String).Trim()
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

# Hotspot extraction from the trace (TraceEvent analyzer). The raw ETL is
# deleted after successful analysis (user decision 2026-09-10); kept on failure.
if ($traceOn -and (Test-Path $etlPath)) {
  $env:_NT_SYMBOL_PATH = "srv*$repo\ai\tools\symbols*https://msdl.microsoft.com/download/symbols;$(Split-Path $Exe -Parent)"
  $procFilter = [System.IO.Path]::GetFileNameWithoutExtension($Exe)
  Write-Host "[$Mode] analysing trace (can take several minutes)..."
  $anaOut = & $analyzer $etlPath $procFilter (Join-Path $res "hotspots") 2>&1 | Out-String
  Write-Host $anaOut
  if ($LASTEXITCODE -eq 0 -and (Test-Path (Join-Path $res "hotspots-self.csv"))) {
    $summary += "profile:   hotspots-self.csv / hotspots-incl.csv / hotspots-threads.csv"
    $topSelf = @(); $topThreads = @(); $inThreads = $false
    foreach ($l in ($anaOut -split "`r?`n")) {
      if ($l -match '^top self:') { $inThreads = $false; continue }
      if ($l -match '^top thread attribution:') { $inThreads = $true; continue }
      if ($l -match '^\s+\d+\.\d+%') {
        if ($inThreads) { if ($topThreads.Count -lt 8) { $topThreads += $l.Trim() } }
        elseif ($topSelf.Count -lt 10) { $topSelf += $l.Trim() }
      }
    }
    if ($topSelf) { $summary += "top_self:"; $summary += ($topSelf | ForEach-Object { "  $_" }) }
    if ($topThreads) { $summary += "top_threads:"; $summary += ($topThreads | ForEach-Object { "  $_" }) }
    Remove-Item $etlPath -Force -ErrorAction SilentlyContinue
    Remove-Item ([System.IO.Path]::ChangeExtension($etlPath, "etlx")) -Force -ErrorAction SilentlyContinue
    Remove-Item "$etlPath.NGENPDB" -Recurse -Force -ErrorAction SilentlyContinue
  }
  else { $failed += "analyzer failed (exit $LASTEXITCODE); trace kept at $etlPath" }
}

if ($failed.Count -gt 0) { $summary += "failures:  $($failed -join ' | ')" }
Set-Content -Path (Join-Path $res "summary.txt") -Value ($summary -join "`n")
$summary | ForEach-Object { Write-Output $_ }
if ($failed.Count -gt 0) { Write-Output "RESULT: FAIL"; exit 1 }
Write-Output "RESULT: PASS ($res)"
exit 0
