<#
Simutrans-Extended local test runner (Windows): smoke + network determinism.

Default mode is network: run a loopback-only server with -fast-network-sync against
tests/demo.sve and compare the final server<port>-restore.sve written at the -until
horizon. This exercises the network server code path, where deterministic lockstep is
expected.

Optional singleuser mode fast-forwards and compares monthly autosaves. It exercises the
single-player code path; byte-identical determinism is NOT expected there.

Run via: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run-smoke-tests.ps1
All transient state lives under ai/temp/ (gitignored; AGENTS.md rule 8); -Clean removes it.

Requires a DEBUG or PROFILE build (-until is compiled only in those builds) and a pakset
matching the branch's Extended object version (master 14.x: pak128.Britain-Ex-0.9.4;
an ex-15 pakset fatals in the object readers on master).
#>
param(
  [string]$Exe,
  [string]$WorkDir,
  [string]$Pakset = "pak128.Britain-Ex-0.9.4",
  [string]$ObjectsName = "pak128.Britain-Ex",
  [string]$Fixture,
  [string]$Until = "1945.6",
  [string]$RoundtripAutosave = "autosave06.sve",
  [int]$TimeoutMs = 240000,
  [string]$AutosaveFormat = "zipped",
  [string]$SaveFormat = "zipped",
  [string]$Mode = "network",
  [int]$FastNetworkSync = 100,
  [int]$ServerPort = 13353,
  [string[]]$Markers = @("FATAL ERROR", "AddressSanitizer", "runtime error"),
  [switch]$SkipRoundtrip,
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

if ($Mode -ne "singleuser" -and $Mode -ne "network") {
  Write-Output "FAIL: -Mode must be 'singleuser' or 'network'"
  exit 1
}
if ($Mode -eq "network" -and $FastNetworkSync -lt 1) {
  Write-Output "FAIL: network mode requires -FastNetworkSync >= 1"
  exit 1
}

if (-not $Exe)     { $Exe = Join-Path $repo "simutrans\Simutrans-Extended-debug.exe" }
if (-not $WorkDir) { $WorkDir = Join-Path $repo "ai\temp\simutest" }
if (-not $Fixture) { $Fixture = Join-Path $repo "tests\demo.sve" }
$Exe = Resolve-RepoPath $Exe
$WorkDir = Resolve-RepoPath $WorkDir
$Fixture = Resolve-RepoPath $Fixture
$save = Join-Path $WorkDir "save"
$logs = Join-Path $WorkDir "logs"
$res  = Join-Path $WorkDir "results"
$stateFilter = "autosave*.sve"
if ($Mode -eq "network") { $stateFilter = "final.sve" }

function Remove-Junctions {
  param([string]$dir)
  # Remove junctions with rmdir, which removes the link only. NEVER use
  # Remove-Item -Recurse on a directory containing junctions: PowerShell 5.1
  # can delete through them into the target.
  Get-ChildItem $dir -Force -ErrorAction SilentlyContinue |
    Where-Object { $_.Attributes -match "ReparsePoint" } |
    ForEach-Object { cmd /c rmdir $_.FullName }
}

if ($Clean) {
  if (Test-Path $WorkDir) {
    Remove-Junctions $WorkDir
    Remove-Item $WorkDir -Recurse -Force
  }
  Write-Output "CLEAN: removed $WorkDir"
  exit 0
}

foreach ($p in @($Exe, $Fixture)) {
  if (-not (Test-Path $p)) { Write-Output "FAIL: missing $p"; exit 1 }
}
$pakTarget = Join-Path $repo "simutrans\$Pakset"
if (-not (Test-Path $pakTarget)) { Write-Output "FAIL: missing pakset $pakTarget"; exit 1 }

# Idempotent workdir setup (all under ai/temp/, gitignored).
# Missing font/text/themes = fatal error = blocking MessageBox on Windows.
foreach ($d in @($save, $logs, (Join-Path $WorkDir "config"))) {
  if (-not (Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
}
foreach ($d in @("runA", "runB", "runC")) {
  $p = Join-Path $res $d
  if (Test-Path $p) { Remove-Item $p -Recurse -Force }
  New-Item -ItemType Directory -Path $p -Force | Out-Null
}
$junctions = [ordered]@{
  $ObjectsName = $pakTarget
  "font"       = (Join-Path $repo "simutrans\font")
  "text"       = (Join-Path $repo "simutrans\text")
  "themes"     = (Join-Path $repo "simutrans\themes")
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
  "frames_per_second = 100"
  "fast_forward_frames_per_second = 100"
)
if ($Mode -eq "network") {
  $simuconfLines += @(
    "autosave = 0"
    "saveformat = $SaveFormat"
    "listen = 127.0.0.1"
    "announce_server = 0"
    "pause_server_no_clients = 0"
    "server_save_game_on_quit = 1"
    "reload_and_save_on_quit = 0"
    "server_frames_per_step = 4"
  )
} else {
  $simuconfLines += @(
    "autosave = 1"
    "autosaveformat = $AutosaveFormat"
  )
}
Set-Content -Path (Join-Path $WorkDir "config\simuconf.tab") -Value ($simuconfLines -join "`n")

$fixtureName = Split-Path $Fixture -Leaf
if (-not (Test-Path (Join-Path $save $fixtureName))) { Copy-Item $Fixture $save }

# ASan runtime DLL lives in the MSVC toolchain bin dir.
$asanDir = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64"
if (Test-Path $asanDir) { $env:PATH = "$asanDir;$env:PATH" }
$env:ASAN_OPTIONS = "print_stacktrace=1 abort_on_error=1 detect_leaks=0"

function Invoke-SimRun {
  param([string]$load, [string]$tag, [int]$port)
  $outLog = Join-Path $logs "$tag.out.log"
  $errLog = Join-Path $logs "$tag.err.log"
  $serverLog = Join-Path $logs "$tag.server.log"
  Remove-Item $outLog, $errLog, $serverLog -Force -ErrorAction SilentlyContinue

  $modeArgs = ""
  if ($Mode -eq "network") { $modeArgs = " -server $port -fast-network-sync $FastNetworkSync" }

  # PS 5.1 joins -ArgumentList arrays without quoting; the repo path contains spaces,
  # so build the argument line with explicit quoting.
  $argLine = "-set_workdir `"$WorkDir`" -singleuser -objects `"$ObjectsName`" -load `"$load`" -until $Until -debug 2 -lang en -fps 100 -nosound$modeArgs"
  $p = Start-Process -FilePath $Exe -ArgumentList $argLine -WorkingDirectory $WorkDir -PassThru -WindowStyle Minimized -RedirectStandardOutput $outLog -RedirectStandardError $errLog
  [void]$p.Handle
  $timedOut = $false
  if (-not $p.WaitForExit($TimeoutMs)) {
    Write-Host "[$tag] TIMEOUT after $TimeoutMs ms - killing"
    $timedOut = $true
    try { $p.Kill() } catch {}
    try { $p.WaitForExit(5000) | Out-Null } catch {}
  }

  $rawServerLog = Join-Path $WorkDir "simu-server$port.log"
  if (Test-Path $rawServerLog) { Move-Item $rawServerLog $serverLog -Force }
  if ($Mode -eq "network") { Start-Sleep -Milliseconds 1000 }

  if ($timedOut) { return -1 }
  Write-Host "[$tag] exit code: $($p.ExitCode)"
  return $p.ExitCode
}

function Clear-RunState {
  Remove-Item (Join-Path $save "autosave*.sve") -Force -ErrorAction SilentlyContinue
  Remove-Item (Join-Path $WorkDir "settings-extended*.xml") -Force -ErrorAction SilentlyContinue
  Remove-Item (Join-Path $WorkDir "server*-restore.sve") -Force -ErrorAction SilentlyContinue
  Remove-Item (Join-Path $WorkDir "server*-restore.sv_") -Force -ErrorAction SilentlyContinue
  Remove-Item (Join-Path $WorkDir "server*-pwdhash.sve") -Force -ErrorAction SilentlyContinue
  Remove-Item (Join-Path $WorkDir "server*-network.sve") -Force -ErrorAction SilentlyContinue
  Remove-Item (Join-Path $WorkDir "server*-network.sv_") -Force -ErrorAction SilentlyContinue
}

function Move-RunState {
  param([string]$destDir, [int]$port)
  if ($Mode -eq "network") {
    $final = Join-Path $WorkDir "server$port-restore.sve"
    if (Test-Path $final) { Move-Item $final (Join-Path $destDir "final.sve") -Force }
    Get-ChildItem (Join-Path $WorkDir "server$port-*.sve") -ErrorAction SilentlyContinue | Move-Item -Destination $destDir -Force
    Get-ChildItem (Join-Path $WorkDir "server$port-*.sv_") -ErrorAction SilentlyContinue | Move-Item -Destination $destDir -Force
  } else {
    Get-ChildItem (Join-Path $save "autosave*.sve") -ErrorAction SilentlyContinue | Move-Item -Destination $destDir -Force
  }
}

function Get-LogMarkers {
  param([string]$tag)
  $bad = @()
  foreach ($f in @((Join-Path $logs "$tag.err.log"), (Join-Path $logs "$tag.out.log"), (Join-Path $logs "$tag.server.log"))) {
    if (Test-Path $f) {
      $hits = Select-String -Path $f -Pattern $Markers -ErrorAction SilentlyContinue
      if ($hits) { $bad += ($hits | Select-Object -First 3 | ForEach-Object { $_.Line.Trim() }) }
    }
  }
  return ,$bad
}

function Test-FastNetworkSyncLogged {
  param([string]$tag)
  foreach ($f in @((Join-Path $logs "$tag.server.log"), (Join-Path $logs "$tag.err.log"), (Join-Path $logs "$tag.out.log"))) {
    if ((Test-Path $f) -and (Select-String -Path $f -Pattern "Fast network sync test mode enabled" -Quiet -ErrorAction SilentlyContinue)) {
      return $true
    }
  }
  return $false
}

$failures = @()

function Invoke-TestRun {
  param([string]$load, [string]$tag, [string]$resultsSubdir, [int]$port)
  Clear-RunState
  $code = Invoke-SimRun -load $load -tag $tag -port $port
  Move-RunState (Join-Path $res $resultsSubdir) $port
  $logBad = Get-LogMarkers $tag
  if ($code -ne 0) { $script:failures += "smoke [$tag] : exit code $code" }
  if ($logBad.Count -gt 0) { $script:failures += "smoke [$tag] : log markers -> $($logBad -join ' | ')" }
  if (($Mode -eq "network") -and (-not (Test-FastNetworkSyncLogged $tag))) {
    $script:failures += "network [$tag] : fast network sync test mode not logged"
  }
}

$portA = $ServerPort
$portB = $ServerPort
$portC = $ServerPort

Invoke-TestRun -load $fixtureName -tag "runA" -resultsSubdir "runA" -port $portA
Invoke-TestRun -load $fixtureName -tag "runB" -resultsSubdir "runB" -port $portB

$aFiles = @(Get-ChildItem (Join-Path $res "runA") -Filter $stateFilter -ErrorAction SilentlyContinue | Sort-Object Name)
$bCount = @(Get-ChildItem (Join-Path $res "runB") -Filter $stateFilter -ErrorAction SilentlyContinue).Count
Write-Host "runA state files: $($aFiles.Count)  runB state files: $bCount"
if ($aFiles.Count -eq 0) { $failures += "determinism : runA produced no comparable state files" }
$detFail = @()
foreach ($f in $aFiles) {
  $bf = Join-Path $res "runB\$($f.Name)"
  if (-not (Test-Path $bf)) { $detFail += "$($f.Name) missing in B"; continue }
  if ((Get-FileHash $f.FullName).Hash -ne (Get-FileHash $bf).Hash) { $detFail += $f.Name }
}
if ($detFail.Count -gt 0) { $failures += "determinism : differing state files -> $($detFail -join ', ')" }
elseif ($aFiles.Count -gt 0) { Write-Output "DETERMINISM: PASS ($($aFiles.Count) state files byte-identical)" }

if (-not $SkipRoundtrip) {
  if ($Mode -eq "network") {
    Write-Output "ROUND-TRIP: SKIPPED (network final-save mode)"
  } else {
    $rtSource = Join-Path $res "runA\$RoundtripAutosave"
    if (Test-Path $rtSource) {
      Copy-Item $rtSource (Join-Path $save "rt.sve") -Force
      Invoke-TestRun -load "rt.sve" -tag "runC" -resultsSubdir "runC" -port $portC
      $cFiles = @(Get-ChildItem (Join-Path $res "runC") -Filter "autosave*.sve" -ErrorAction SilentlyContinue | Sort-Object Name)
      $rtFail = @(); $rtCount = 0
      foreach ($f in $cFiles) {
        $af = Join-Path $res "runA\$($f.Name)"
        if (-not (Test-Path $af)) { $rtFail += "$($f.Name) missing in A"; continue }
        $rtCount++
        if ((Get-FileHash $f.FullName).Hash -ne (Get-FileHash $af).Hash) { $rtFail += $f.Name }
      }
      if ($rtFail.Count -gt 0) { $failures += "round-trip : differing autosaves -> $($rtFail -join ', ')" }
      elseif ($rtCount -eq 0) { $failures += "round-trip : runC produced no comparable autosaves" }
      else { Write-Output "ROUND-TRIP: PASS ($rtCount autosaves byte-identical)" }
    } else {
      $failures += "round-trip : $rtSource not found (runA did not reach that month?)"
    }
  }
}

if ($failures.Count -gt 0) {
  Write-Output "RESULT: FAIL"
  $failures | ForEach-Object { Write-Output "  - $_" }
  exit 1
}
if ($Mode -eq "network") {
  Write-Output "RESULT: PASS (smoke + network determinism)"
} else {
  Write-Output "RESULT: PASS (smoke + determinism + round-trip)"
}
exit 0
