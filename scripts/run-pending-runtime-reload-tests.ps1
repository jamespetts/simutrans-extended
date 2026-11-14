<#
Simutrans-Extended pending-runtime reload fixture runner (Windows).

Purpose: run one pending-runtime reload case across a genuine save/reload,
matching scripts/run-pending-runtime-reload-tests.sh. The default case is the
forward Y division: the staged scenario tests/pending-runtime/reload/scenario.nut
runs the forward division case from tests/pending-runtime/harness.nut on a
loopback server. The paired H exchange reload is selected with

  -CaseName pair-reload
  -ScenarioName pending-runtime-pair-reload
  -Selector tests\pending-runtime\pair-reload\scenario.nut
  -Harness tests\pending-runtime\pair-harness.nut
  -HarnessName pair-harness

The rest of this comment describes the default division case. In that case,
once the split-off portion has formed at target entry 21, the case prints
"RUNTIME CASE reload READY" and holds before PASS. The runner then starts a
second process as a client (-load net:127.0.0.1:<port>). The join makes the
server save, transfer and reload its state (nwc_sync_t in
network/network_cmd_ingame.cc), and the server script's resume_game rebuilds
the fixture from the loaded world. The case must print
"RUNTIME CASE reload RESTORED" and then "RUNTIME CASE reload PASS" when the
existing split-off portion advances from target entry 21 to entry 22. The
server's save written by the join (server<port>-network.sve) is then loaded by
a fresh offline process, which must print RESTORED and PASS again. The pair
reload case uses the same phases with its own case name and asserts the
committed final consists and the laid-over vehicle.

Oracles (all must hold for PASS):
 1. the server prints READY, RESTORED and PASS, in that order, and the joined
    client logs its own unpause (network_game_set_pause ... pause=0);
 2. no desync markers: client "Lost synchronisation"/"checklist mismatch";
    server "kicking client"/"disconnect client" due to a checklist mismatch;
 3. at least one checklist pair is parsed from the client's
    "server=[...] client=[...]" lines and none of the parsed pairs mismatches;
 4. neither peer log holds a script error, the case's FAIL marker, or the
    configured failure markers (FATAL ERROR, sanitizer reports);
 5. both peers stay alive for ObserveSeconds beyond the server's PASS;
 6. the preserved join save loads offline and prints RESTORED then PASS, with
    no failure marker.

shunting_time_seconds is set in the user-directory simuconf.tab of each role's
work directory so that the case has not advanced past its reload point while the
client starts. This is a real settings value, not an artificial state:
settings_t::rdwr only reads shunting_time_seconds for Extended 15 and later
saves (dataobj/settings.cc), the fixture map
(tests/pending-runtime/empty-16x16.sve) is an Extended 14 save, and both the
server reload and the offline load read the value from the ex-15 save written
during the join.

Windows arrangement:
- Each role work directory (server, client, offline) gets junctions named
  <ObjectsName>, font, text, themes and script. The game resolves both the pakset and
  the scenario path through the <ObjectsName> junction.
- The scenario files are staged at <workdir>\staging. The role work
  directories contain no addon paks, so scenario_t::init and scenario_t::rdwr
  use the "<objects>/scenario/<name>/" path. This runner creates the junction
  <pakset>\scenario\pending-runtime-reload -> <workdir>\staging.
  Only that junction is written into the pakset; all staged files stay in the
  work directory. A pre-existing non-junction entry of that name is never
  deleted: the runner stops with an error instead.
- The junction is removed with "cmd /c rmdir", which removes the link only.
  Recursive PowerShell deletion is never used on a tree that still contains a
  junction; Remove-Tree first removes reparse points without following them.
- Game logs are polled with FileShare.ReadWrite because the game holds them
  open. Process death and all time limits are polled, so the runner does not
  rely on window responsiveness.
- Every game process is started with -nomidi. The graphical Debug binary is
  supported.

All transient state lives under ai/temp/ (gitignored; AGENTS.md rule 9).
#>
param(
  [string]$Exe,
  [string]$WorkDir,
  [string]$Pakset = "pak128.Britain-Ex",
  [string]$ObjectsName = "pak128.Britain-Ex",
  [int]$ServerPort = 13357,
  # Reload case selection. The defaults run the forward Y division reload; the
  # paired-exchange reload is selected with the values documented at the top.
  [string]$CaseName = "reload",
  [string]$ScenarioName = "pending-runtime-reload",
  [string]$Selector = "tests\pending-runtime\reload\scenario.nut",
  [string]$Harness = "tests\pending-runtime\harness.nut",
  [string]$HarnessName = "harness",
  # Seconds allowed for server setup up to the case's READY marker.
  [int]$ServerReadyTimeout = 900,
  # Seconds allowed for the whole client phase, from READY to ObserveSeconds
  # beyond the server's PASS.
  [int]$ClientTimeout = 420,
  # Seconds allowed for the fresh offline load of the join save.
  [int]$OfflineTimeout = 420,
  # Joined-play observation window after the server's PASS.
  [int]$ObserveSeconds = 20,
  # shunting_time_seconds written to each role's user-directory simuconf.tab.
  [int]$ShuntingSeconds = 600,
  [int]$DebugLevel = 2,
  [string[]]$Markers = @("FATAL ERROR", "AddressSanitizer", "ThreadSanitizer", "runtime error", "rendezvous released with work outstanding"),
  [switch]$Clean
)

$ErrorActionPreference = "Stop"
if (-not $PSScriptRoot) { Write-Output "FAIL: run via powershell -File"; exit 1 }
$repo = Split-Path -Parent $PSScriptRoot

function Resolve-RepoPath {
  param([string]$Path)
  if ([System.IO.Path]::IsPathRooted($Path)) { return [System.IO.Path]::GetFullPath($Path) }
  return [System.IO.Path]::GetFullPath((Join-Path $repo $Path))
}

if (-not $Exe)     { $Exe = Join-Path $repo "simutrans\Simutrans-Extended-debug.exe" }
if (-not $WorkDir) { $WorkDir = Join-Path $repo "ai\temp\pending-runtime-$CaseName" }
$Exe = Resolve-RepoPath $Exe
$WorkDir = Resolve-RepoPath $WorkDir

$ServerDir = Join-Path $WorkDir "server"
$ClientDir = Join-Path $WorkDir "client"
$OfflineDir = Join-Path $WorkDir "offline"
$StagingDir = Join-Path $WorkDir "staging"
$LogsDir = Join-Path $WorkDir "logs"
$PaksetDir = Join-Path $repo "simutrans\$Pakset"
$PaksetScenarioDir = Join-Path $PaksetDir "scenario"
$ScenarioLink = Join-Path $PaksetScenarioDir $ScenarioName
$PreservedSave = Join-Path $LogsDir "server$ServerPort-network.sve"

$HarnessFile = Resolve-RepoPath $Harness
$SelectorFile = Resolve-RepoPath $Selector
$MapFile = Join-Path $repo "tests\pending-runtime\empty-16x16.sve"

$ReadyMarker = "RUNTIME CASE $CaseName READY"
$RestoredMarker = "RUNTIME CASE $CaseName RESTORED"
$PassMarker = "RUNTIME CASE $CaseName PASS"
$FailMarker = "RUNTIME CASE $CaseName FAIL"
$JoinPattern = "network_game_set_pause.*pause=0"
$DesyncPattern = "Lost synchronisation|checklist mismatch|disconnect client|kicking client"
$ScriptErrorPattern = "Script: Error|</error>|Could not load scenario|Loading scenario script failed|No scenario map specified|Could not load scenario map"
$ActiveMarkerRegex = ""
foreach ($m in $Markers) {
  if ($m) {
    if ($ActiveMarkerRegex) { $ActiveMarkerRegex += "|" }
    $ActiveMarkerRegex += $m
  }
}

function Get-DirectoryEntry {
  param([string]$Directory, [string]$Name)
  # Enumerating the parent also lists a junction whose target is missing, so
  # this is used where a dangling link must still be recognised.
  foreach ($child in @(Get-ChildItem -LiteralPath $Directory -Force -ErrorAction SilentlyContinue)) {
    if ($child.Name -ieq $Name) {
      return [pscustomobject]@{
        Exists      = $true
        IsLink      = (($child.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)
        IsDirectory = $child.PSIsContainer
      }
    }
  }
  return [pscustomobject]@{ Exists = $false; IsLink = $false; IsDirectory = $false }
}

function Get-EntryInfo {
  param([string]$Path)
  # File.GetAttributes reports the reparse point itself and does not follow it,
  # so a dangling junction is still detected.
  try {
    $attributes = [System.IO.File]::GetAttributes($Path)
    return [pscustomobject]@{
      Exists      = $true
      IsLink      = (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)
      IsDirectory = (($attributes -band [System.IO.FileAttributes]::Directory) -ne 0)
    }
  }
  catch {
    return [pscustomobject]@{ Exists = $false; IsLink = $false; IsDirectory = $false }
  }
}

function Remove-Junction {
  param([string]$Path)
  # rmdir removes a directory junction itself and never touches the target's
  # contents. /c rmdir is used rather than Remove-Item, which can follow the
  # link when -Recurse is present anywhere in the call chain.
  & cmd.exe /c "rmdir `"$Path`"" 2>$null | Out-Null
  $info = Get-EntryInfo $Path
  if ($info.Exists) {
    try { [System.IO.Directory]::Delete($Path, $false) }
    catch {
      try { [System.IO.File]::Delete($Path) } catch {}
    }
  }
}

function Remove-ReparsePointsRecursive {
  param([string]$Root)
  if (-not (Get-EntryInfo $Root).Exists) { return }
  $stack = New-Object System.Collections.Generic.Stack[string]
  $stack.Push($Root)
  while ($stack.Count -gt 0) {
    $dir = $stack.Pop()
    foreach ($child in @(Get-ChildItem -LiteralPath $dir -Force -ErrorAction SilentlyContinue)) {
      if (($child.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        if ($child.PSIsContainer) { Remove-Junction $child.FullName }
        else { Remove-Item -LiteralPath $child.FullName -Force -ErrorAction SilentlyContinue }
      }
      elseif ($child.PSIsContainer) {
        $stack.Push($child.FullName)
      }
    }
  }
}

function Remove-Tree {
  param([string]$Path)
  $info = Get-EntryInfo $Path
  if (-not $info.Exists) { return }
  if ($info.IsLink) { Remove-Junction $Path; return }
  # Remove every junction without following it first; only then is a recursive
  # delete safe.
  Remove-ReparsePointsRecursive $Path
  Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction SilentlyContinue
}

function New-RoleJunction {
  param([string]$Link, [string]$Target)
  $info = Get-EntryInfo $Link
  if ($info.Exists) {
    if (-not $info.IsLink) { throw "unexpected non-junction entry $Link; refusing to delete it" }
    Remove-Junction $Link
  }
  New-Item -ItemType Junction -Path $Link -Target $Target | Out-Null
}

function Read-SharedText {
  param([string]$Path)
  if (-not (Get-EntryInfo $Path).Exists) { return "" }
  $stream = $null
  $reader = $null
  try {
    $stream = [System.IO.FileStream]::new($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    $reader = [System.IO.StreamReader]::new($stream)
    return $reader.ReadToEnd()
  }
  catch {
    return ""
  }
  finally {
    if ($reader) { $reader.Dispose() }
    elseif ($stream) { $stream.Dispose() }
  }
}

$script:LogCache = @{}
function Refresh-LogCache {
  param([string[]]$Paths)
  foreach ($p in $Paths) {
    $script:LogCache[$p] = Read-SharedText $p
  }
}

function Get-LogText {
  param([string]$Path)
  if ($script:LogCache.ContainsKey($Path)) { return $script:LogCache[$Path] }
  return (Read-SharedText $Path)
}

function Test-LogPattern {
  param([string]$Path, [string]$Pattern)
  if (-not $Pattern) { return $false }
  $text = Get-LogText $Path
  if (-not $text) { return $false }
  return [regex]::IsMatch($text, $Pattern)
}

function Test-LogPatternAny {
  param([string[]]$Paths, [string]$Pattern)
  foreach ($p in $Paths) {
    if (Test-LogPattern $p $Pattern) { return $true }
  }
  return $false
}

function Get-FirstMatchAny {
  param([string[]]$Paths, [string]$Pattern)
  foreach ($p in $Paths) {
    $text = Get-LogText $p
    if (-not $text) { continue }
    $match = [regex]::Match($text, $Pattern)
    if ($match.Success) { return $match.Value }
  }
  return ""
}

function Get-LogLineNumber {
  param([string]$Path, [string]$Pattern)
  $text = Get-LogText $Path
  if (-not $text) { return $null }
  $reader = [System.IO.StringReader]::new($text)
  $number = 0
  try {
    while ($null -ne ($line = $reader.ReadLine())) {
      $number++
      if ([regex]::IsMatch($line, $Pattern)) { return $number }
    }
  }
  finally {
    $reader.Dispose()
  }
  return $null
}

function Get-LogMarkerHits {
  param([string[]]$Paths, [string]$Pattern, [int]$Max = 3)
  $hits = New-Object System.Collections.Generic.List[string]
  if (-not $Pattern) { return ,$hits }
  foreach ($p in $Paths) {
    $found = 0
    $text = Get-LogText $p
    if (-not $text) { continue }
    $reader = [System.IO.StringReader]::new($text)
    try {
      while ($null -ne ($line = $reader.ReadLine())) {
        if ([regex]::IsMatch($line, $Pattern)) {
          $hits.Add($line.Trim())
          $found++
          if ($found -ge $Max) { break }
        }
      }
    }
    finally {
      $reader.Dispose()
    }
  }
  return ,$hits
}

function Get-ChecklistPairStats {
  param([string]$Path)
  # Mirrors the parsing in scripts/run-pending-runtime-reload-tests.sh: a pair
  # starts at a line containing "sync_step=N", the server half starts at
  # "server=[", the client half at "client=[", and a pair counts only when both
  # halves contain "sums=".
  $stats = [pscustomobject]@{ Pairs = 0; Mismatches = 0; First = $null }
  $text = Get-LogText $Path
  if (-not $text) { return $stats }
  $serverHalf = ""
  $clientHalf = ""
  $phase = 0
  $syncStep = $null
  $reader = [System.IO.StringReader]::new($text)
  try {
    while ($null -ne ($line = $reader.ReadLine())) {
      $stepMatch = [regex]::Match($line, "sync_step=([0-9]+)")
      if ($stepMatch.Success) {
        $syncStep = $stepMatch.Groups[1].Value
        $pos = $line.IndexOf("server=[")
        if ($pos -ge 0) { $serverHalf = $line.Substring($pos + 8) } else { $serverHalf = "" }
        $serverHalf += "`n"
        $clientHalf = ""
        $phase = 1
        continue
      }
      if ($phase -eq 1) {
        $serverHalf += $line + "`n"
        if ($line.Contains("]")) { $phase = 2 }
        continue
      }
      if ($phase -eq 2) {
        $clientLine = $line
        $pos = $clientLine.IndexOf("client=[")
        if ($pos -ge 0) { $clientLine = $clientLine.Substring($pos + 8) }
        $clientHalf += $clientLine + "`n"
        if ($line.Contains("]")) {
          if ($serverHalf.Contains("sums=") -and $clientHalf.Contains("sums=")) {
            $stats.Pairs++
            if ($serverHalf -cne $clientHalf) {
              $stats.Mismatches++
              if ($null -eq $stats.First) { $stats.First = $syncStep }
            }
          }
          $phase = 0
        }
        continue
      }
    }
  }
  finally {
    $reader.Dispose()
  }
  return $stats
}

function Start-GameProcess {
  param([string]$Dir, [string]$OutLog, [string]$ErrLog, [string]$ExtraArgs)
  # PS 5.1 joins -ArgumentList arrays without quoting; the repo path contains
  # spaces, so the argument line is built with explicit quotes.
  $argLine = "-set_workdir `"$Dir`" -singleuser -objects `"$ObjectsName`" $ExtraArgs -debug $DebugLevel -lang en -fps 100 -nosound -nomidi"
  Remove-Item -LiteralPath $OutLog, $ErrLog -Force -ErrorAction SilentlyContinue
  return Start-Process -FilePath $Exe -ArgumentList $argLine -WorkingDirectory $Dir -PassThru -WindowStyle Minimized -RedirectStandardOutput $OutLog -RedirectStandardError $ErrLog
}

function Get-ProcessExitCode {
  param($Process)
  if ($null -eq $Process) { return $null }
  try {
    if ($Process.HasExited) {
      $Process.WaitForExit(1000) | Out-Null
      return $Process.ExitCode
    }
  }
  catch {}
  return $null
}

function Stop-ProcessSafe {
  param($Process)
  if ($null -eq $Process) { return }
  try {
    if (-not $Process.HasExited) { $Process.Kill() }
  }
  catch {}
  try { $Process.WaitForExit(5000) | Out-Null } catch {}
}

function Write-LogTail {
  param([string]$Path, [int]$Count = 12)
  if (-not (Get-EntryInfo $Path).Exists) { return }
  $lines = @((Read-SharedText $Path) -split "`r?`n" | Where-Object { $_.Trim() -ne "" })
  if ($lines.Count -eq 0) { return }
  $start = [Math]::Max(0, $lines.Count - $Count)
  Write-Output "  --- $(Split-Path -Leaf $Path) (last $($lines.Count - $start) lines) ---"
  for ($i = $start; $i -lt $lines.Count; $i++) {
    Write-Output ("  " + $lines[$i])
  }
}

function Initialize-Role {
  param([string]$Dir)
  Remove-Tree $Dir
  New-Item -ItemType Directory -Path (Join-Path $Dir "config") -Force | Out-Null
  New-Item -ItemType Directory -Path (Join-Path $Dir "save") -Force | Out-Null
  New-RoleJunction (Join-Path $Dir $ObjectsName) $PaksetDir
  New-RoleJunction (Join-Path $Dir "font") (Join-Path $repo "simutrans\font")
  New-RoleJunction (Join-Path $Dir "text") (Join-Path $repo "simutrans\text")
  New-RoleJunction (Join-Path $Dir "themes") (Join-Path $repo "simutrans\themes")
  New-RoleJunction (Join-Path $Dir "script") (Join-Path $repo "simutrans\script")
  $configLines = @(
    "frames_per_second = 100"
    "fast_forward_frames_per_second = 100"
    "autosave = 0"
    "saveformat = bzip2"
    "listen = 127.0.0.1"
    "announce_server = 0"
    "pause_server_no_clients = 0"
    "server_save_game_on_quit = 1"
    "reload_and_save_on_quit = 0"
    "server_frames_per_step = 4"
    "server_frames_between_checks = 1"
  )
  Set-Content -LiteralPath (Join-Path $Dir "config\simuconf.tab") -Value ($configLines -join "`r`n") -Encoding ASCII
  # Read after the pakset's own simuconf, which also sets
  # shunting_time_seconds, so this value wins (simmain.cc parses the program
  # directory config first and the user directory config last).
  Set-Content -LiteralPath (Join-Path $Dir "simuconf.tab") -Value "shunting_time_seconds = $ShuntingSeconds" -Encoding ASCII
}

function Set-PaksetScenarioLink {
  $info = Get-DirectoryEntry $PaksetScenarioDir $ScenarioName
  if ($info.Exists) {
    if (-not $info.IsLink) {
      throw "a non-junction entry already exists at $ScenarioLink; refusing to delete a pre-existing directory. Remove or rename it and rerun."
    }
    Remove-Junction $ScenarioLink
  }
  New-Item -ItemType Junction -Path $ScenarioLink -Target $StagingDir | Out-Null
}

function Stage-Scenario {
  Remove-Tree $StagingDir
  New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null
  Copy-Item -LiteralPath $HarnessFile -Destination (Join-Path $StagingDir "$HarnessName.nut") -Force
  Copy-Item -LiteralPath $MapFile -Destination (Join-Path $StagingDir "empty-16x16.sve") -Force
  Copy-Item -LiteralPath $SelectorFile -Destination (Join-Path $StagingDir "scenario.nut") -Force
}

function Write-LogTails {
  param([string[]]$Paths)
  foreach ($p in $Paths) {
    if ((Get-EntryInfo $p).Exists) { Write-LogTail $p }
  }
}

if ($Clean) {
  $scenarioInfo = Get-DirectoryEntry $PaksetScenarioDir $ScenarioName
  if ($scenarioInfo.Exists -and $scenarioInfo.IsLink) {
    Remove-Junction $ScenarioLink
  }
  elseif ($scenarioInfo.Exists) {
    Write-Output "NOTE: $ScenarioLink is not a junction; leaving it in place"
  }
  Remove-Tree $WorkDir
  Write-Output "CLEAN: removed $WorkDir"
  exit 0
}

if ($DebugLevel -lt 2) {
  Write-Output "FAIL: -DebugLevel must be >= 2 (the checklist-pair oracle is logged only at -debug 2 or higher)"
  exit 1
}
foreach ($p in @($Exe, $HarnessFile, $SelectorFile, $MapFile)) {
  if (-not (Test-Path -LiteralPath $p)) { Write-Output "FAIL: missing $p"; exit 1 }
}
if (-not (Test-Path -LiteralPath $PaksetDir -PathType Container)) { Write-Output "FAIL: missing pakset $PaksetDir"; exit 1 }
foreach ($d in @("font", "text", "themes", "script")) {
  $resourceDir = Join-Path $repo "simutrans\$d"
  if (-not (Test-Path -LiteralPath $resourceDir -PathType Container)) { Write-Output "FAIL: missing resource directory $resourceDir"; exit 1 }
}

# ASan runtime DLL lives in the MSVC toolchain bin dir (same prerequisite as
# scripts/run-smoke-tests.ps1).
$asanDir = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64"
if (Test-Path -LiteralPath $asanDir) { $env:PATH = "$asanDir;$env:PATH" }
$env:ASAN_OPTIONS = "print_stacktrace=1 abort_on_error=1 detect_leaks=0"

$serverOut = Join-Path $ServerDir "server.out.log"
$serverErr = Join-Path $ServerDir "server.err.log"
$clientOut = Join-Path $ClientDir "client.out.log"
$clientErr = Join-Path $ClientDir "client.err.log"
$offlineOut = Join-Path $OfflineDir "offline.out.log"
$offlineErr = Join-Path $OfflineDir "offline.err.log"
$serverSave = Join-Path $ServerDir "server$ServerPort-network.sve"

$failures = New-Object System.Collections.Generic.List[string]
$server = $null
$client = $null
$offline = $null
$createdPaksetScenarioDir = $false

try {
  # ---- generated state reset (only directories this runner owns) ----
  foreach ($d in @($StagingDir, $LogsDir, $ServerDir, $ClientDir, $OfflineDir)) { Remove-Tree $d }
  New-Item -ItemType Directory -Path $LogsDir -Force | Out-Null
  Stage-Scenario
  if (-not (Test-Path -LiteralPath $PaksetScenarioDir -PathType Container)) {
    New-Item -ItemType Directory -Path $PaksetScenarioDir -Force | Out-Null
    $createdPaksetScenarioDir = $true
  }
  Set-PaksetScenarioLink
  Initialize-Role $ServerDir
  Initialize-Role $ClientDir
  Initialize-Role $OfflineDir
  Write-Output "Staged scenario: $StagingDir (via $ScenarioLink)"

  # ---- server phase: wait for READY ----
  Write-Output "Starting server: $Exe (scenario $ScenarioName, port $ServerPort, shunting ${ShuntingSeconds}s)"
  $server = Start-GameProcess -Dir $ServerDir -OutLog $serverOut -ErrLog $serverErr `
    -ExtraArgs "-scenario `"$ScenarioName`" -server $ServerPort"
  $serverWatch = [System.Diagnostics.Stopwatch]::StartNew()
  $readyWatch = [System.Diagnostics.Stopwatch]::StartNew()
  $ready = $false
  $readySeconds = -1
  while ($readyWatch.Elapsed.TotalSeconds -lt $ServerReadyTimeout) {
    Start-Sleep -Milliseconds 1000
    Refresh-LogCache @($serverOut, $serverErr)
    if (Test-LogPatternAny @($serverErr, $serverOut) $FailMarker) {
      $failures.Add("server : " + (Get-FirstMatchAny @($serverErr, $serverOut) $FailMarker))
      break
    }
    if (Test-LogPatternAny @($serverErr, $serverOut) $ScriptErrorPattern) {
      $failures.Add("server : scenario loading or script error -> " + (Get-FirstMatchAny @($serverErr, $serverOut) $ScriptErrorPattern))
      break
    }
    $serverCode = Get-ProcessExitCode $server
    if ($null -ne $serverCode) {
      $failures.Add("server : exited on its own (code $serverCode) before $ReadyMarker")
      break
    }
    if (Test-LogPatternAny @($serverErr, $serverOut) $ReadyMarker) {
      $ready = $true
      $readySeconds = [int][Math]::Floor($readyWatch.Elapsed.TotalSeconds)
      break
    }
  }
  if (-not $ready -and $failures.Count -eq 0) {
    $failures.Add("server : did not reach $ReadyMarker within ${ServerReadyTimeout}s")
  }

  if ($failures.Count -eq 0) {
    # ---- client phase: join, then observe beyond PASS ----
    Write-Output "Server reached $ReadyMarker after ~${readySeconds}s; starting client"
    Write-Output "Starting client: $Exe (net:127.0.0.1:$ServerPort)"
    $client = Start-GameProcess -Dir $ClientDir -OutLog $clientOut -ErrLog $clientErr `
      -ExtraArgs "-load `"net:127.0.0.1:$ServerPort`""
    $clientWatch = [System.Diagnostics.Stopwatch]::StartNew()
    # The server watchdog mirrors the POSIX runner's timeout(1) budget.
    $serverWatchdog = $ServerReadyTimeout + $ClientTimeout + $ObserveSeconds + 300
    $joinedSeconds = -1
    $restoredSeen = $false
    $passSeconds = -1
    $desynced = $false
    $clientExit = $null
    $serverExit = $null
    $clientTimedOut = $false
    $serverTimedOut = $false
    $endReason = "deadline"
    while ($clientWatch.Elapsed.TotalSeconds -lt $ClientTimeout) {
      Start-Sleep -Milliseconds 2000
      Refresh-LogCache @($serverOut, $serverErr, $clientOut, $clientErr)
      if (Test-LogPatternAny @($clientErr, $clientOut, $serverErr, $serverOut) $DesyncPattern) {
        $desynced = $true
        $endReason = "desync"
        Start-Sleep -Milliseconds 5000
        break
      }
      if (Test-LogPatternAny @($serverErr, $serverOut) $FailMarker) {
        $endReason = "case-failed"
        break
      }
      if (Test-LogPatternAny @($serverErr, $serverOut, $clientErr, $clientOut) $ScriptErrorPattern) {
        $endReason = "script-failed"
        break
      }
      $clientExit = Get-ProcessExitCode $client
      if ($null -ne $clientExit) {
        Write-Output "[client] exited (code $clientExit)"
        $endReason = "client-exited"
        break
      }
      $serverExit = Get-ProcessExitCode $server
      if ($null -ne $serverExit) {
        Write-Output "[server] exited during observation (code $serverExit)"
        $endReason = "server-exited"
        break
      }
      if ($serverWatch.Elapsed.TotalSeconds -ge $serverWatchdog) {
        $serverTimedOut = $true
        $endReason = "server-timeout"
        break
      }
      if ($joinedSeconds -lt 0 -and (Test-LogPatternAny @($clientOut, $clientErr) $JoinPattern)) {
        $joinedSeconds = [int][Math]::Floor($clientWatch.Elapsed.TotalSeconds)
        Write-Output "Client joined and unpaused after ~${joinedSeconds}s"
      }
      if (Test-LogPatternAny @($serverErr, $serverOut) $RestoredMarker) {
        $restoredSeen = $true
      }
      if ($passSeconds -lt 0 -and (Test-LogPatternAny @($serverErr, $serverOut) $PassMarker)) {
        $passSeconds = [int][Math]::Floor($clientWatch.Elapsed.TotalSeconds)
        Write-Output "Server printed $PassMarker after ~${passSeconds}s; observing for ${ObserveSeconds}s beyond PASS"
      }
      if ($passSeconds -ge 0 -and ($clientWatch.Elapsed.TotalSeconds - $passSeconds) -ge $ObserveSeconds) {
        $endReason = "observed"
        break
      }
    }
    if ($endReason -eq "deadline") { $clientTimedOut = $true }

    # Capture exit codes while the processes are still unreaped, then stop both.
    if ($null -eq $clientExit) { $clientExit = Get-ProcessExitCode $client }
    if ($null -eq $serverExit) { $serverExit = Get-ProcessExitCode $server }
    Stop-ProcessSafe $client
    Stop-ProcessSafe $server
    $client = $null
    $server = $null

    # Preserve the save written by the join before analysing the run.
    if (Test-Path -LiteralPath $serverSave) {
      Copy-Item -LiteralPath $serverSave -Destination $PreservedSave -Force
    }
    Refresh-LogCache @($serverOut, $serverErr, $clientOut, $clientErr)

    Write-Output "---- analysis: server and client ----"
    $serverFailMessage = Get-FirstMatchAny @($serverErr, $serverOut) $FailMarker
    if ($serverFailMessage) { $failures.Add("server : $serverFailMessage") }
    if ($desynced) {
      $failures.Add("join-sync : desync markers detected (client 'Lost synchronisation'/'checklist mismatch' or server kick)")
    }
    if ($joinedSeconds -lt 0 -and -not $desynced) {
      $failures.Add("join-sync : client never joined and unpaused within ${ClientTimeout}s (no 'network_game_set_pause ... pause=0' in the client log)")
    }
    if (-not $restoredSeen) {
      $failures.Add("server : no $RestoredMarker after the join (the save was not reloaded or the restored state was not re-asserted)")
    }
    if ($passSeconds -lt 0) {
      $failures.Add("server : no $PassMarker (the restored state did not advance as the case requires)")
    }
    $readyLine = Get-LogLineNumber $serverErr $ReadyMarker
    $restoredLine = Get-LogLineNumber $serverErr $RestoredMarker
    $passLine = Get-LogLineNumber $serverErr $PassMarker
    if (($null -ne $readyLine) -and ($null -ne $restoredLine) -and ($null -ne $passLine)) {
      if (-not (($readyLine -lt $restoredLine) -and ($restoredLine -lt $passLine))) {
        $failures.Add("server : markers are not in the order READY < RESTORED < PASS (READY line $readyLine, RESTORED line $restoredLine, PASS line $passLine)")
      }
    }
    elseif ($restoredSeen -and ($passSeconds -ge 0)) {
      $failures.Add("server : could not verify READY < RESTORED < PASS ordering (a marker is missing from server.err.log)")
    }
    if ($null -ne $clientExit) {
      if ($clientTimedOut) { $failures.Add("client : TIMEOUT after ${ClientTimeout}s - killed") }
      else { $failures.Add("client : exited on its own (code $clientExit) before the runner stopped it") }
    }
    if ($null -ne $serverExit) {
      if ($serverTimedOut) { $failures.Add("server : TIMEOUT after ${serverWatchdog}s - killed") }
      else { $failures.Add("server : exited on its own (code $serverExit) before the runner stopped it") }
    }
    if (($passSeconds -ge 0) -and (-not $desynced) -and ($null -eq $clientExit) -and ($null -eq $serverExit) `
      -and (-not $clientTimedOut) -and (-not $serverTimedOut) `
      -and (($clientWatch.Elapsed.TotalSeconds - $passSeconds) -lt $ObserveSeconds)) {
      $failures.Add("join-sync : observation ended before the requested ${ObserveSeconds}s beyond PASS")
    }
    if (Test-LogPatternAny @($serverErr, $serverOut, $clientErr, $clientOut) $ScriptErrorPattern) {
      $failures.Add("logs : script error -> " + (Get-FirstMatchAny @($serverErr, $serverOut, $clientErr, $clientOut) $ScriptErrorPattern))
    }
    $markerHits = Get-LogMarkerHits @($serverOut, $serverErr, $clientOut, $clientErr) $ActiveMarkerRegex
    if ($markerHits.Count -gt 0) {
      $failures.Add("logs : markers -> " + ($markerHits -join " | "))
    }
    $pairStats = Get-ChecklistPairStats $clientErr
    Write-Output "checklist pairs: $($pairStats.Pairs), mismatches: $($pairStats.Mismatches)"
    if ($pairStats.Pairs -lt 1) {
      $failures.Add("join-sync : no checklist pairs parsed from the client log (the client did not receive nwc_check_t or the log format changed)")
    }
    if ($pairStats.Mismatches -gt 0) {
      $failures.Add("join-sync : $($pairStats.Mismatches) of $($pairStats.Pairs) checklist pairs mismatch (first at sync_step $($pairStats.First))")
    }

    # ---- offline phase: fresh load of the preserved join save ----
    if ($failures.Count -eq 0) {
      if (-not (Test-Path -LiteralPath $PreservedSave)) {
        $failures.Add("offline : the preserved join save $PreservedSave does not exist")
      }
      else {
        Write-Output "SERVER: PASS (joined after ~${joinedSeconds}s; RESTORED and PASS; observed ${ObserveSeconds}s beyond PASS)"
        Initialize-Role $OfflineDir
        Copy-Item -LiteralPath $PreservedSave -Destination (Join-Path $OfflineDir "save\server$ServerPort-network.sve") -Force
        Write-Output "Starting offline reload: $Exe (-load server$ServerPort-network.sve)"
        $offline = Start-GameProcess -Dir $OfflineDir -OutLog $offlineOut -ErrLog $offlineErr `
          -ExtraArgs "-load `"server$ServerPort-network.sve`""
        $offlineWatch = [System.Diagnostics.Stopwatch]::StartNew()
        $offlineRestored = $false
        $offlinePass = $false
        $offlineRestoredSeconds = -1
        $offlineExit = $null
        $offlineTimedOut = $false
        $offlineEnd = "deadline"
        while ($offlineWatch.Elapsed.TotalSeconds -lt $OfflineTimeout) {
          Start-Sleep -Milliseconds 2000
          Refresh-LogCache @($offlineOut, $offlineErr)
          if ((Test-LogPatternAny @($offlineErr, $offlineOut) $FailMarker) -or
              (Test-LogPatternAny @($offlineErr, $offlineOut) $ScriptErrorPattern)) {
            $offlineEnd = "case-failed"
            break
          }
          $offlineExit = Get-ProcessExitCode $offline
          if ($null -ne $offlineExit) {
            Write-Output "[offline] exited (code $offlineExit)"
            $offlineEnd = "offline-exited"
            break
          }
          if (Test-LogPatternAny @($offlineErr, $offlineOut) $RestoredMarker) {
            if (-not $offlineRestored) {
              $offlineRestoredSeconds = [int][Math]::Floor($offlineWatch.Elapsed.TotalSeconds)
            }
            $offlineRestored = $true
          }
          if (Test-LogPatternAny @($offlineErr, $offlineOut) $PassMarker) {
            $offlinePass = $true
            $offlineEnd = "passed"
            break
          }
        }
        if ($offlineEnd -eq "deadline") { $offlineTimedOut = $true }
        if ($null -eq $offlineExit) { $offlineExit = Get-ProcessExitCode $offline }
        Stop-ProcessSafe $offline
        $offline = $null
        Refresh-LogCache @($offlineOut, $offlineErr)

        Write-Output "---- analysis: offline reload ----"
        $offlineFailMessage = Get-FirstMatchAny @($offlineErr, $offlineOut) $FailMarker
        if ($offlineFailMessage) { $failures.Add("offline : $offlineFailMessage") }
        if (-not $offlineRestored) {
          $failures.Add("offline : no $RestoredMarker after loading the join save")
        }
        if (-not $offlinePass) {
          $failures.Add("offline : no $PassMarker (the restored state did not advance as the case requires)")
        }
        $offlineRestoredLine = Get-LogLineNumber $offlineErr $RestoredMarker
        $offlinePassLine = Get-LogLineNumber $offlineErr $PassMarker
        if (($null -ne $offlineRestoredLine) -and ($null -ne $offlinePassLine)) {
          if ($offlineRestoredLine -gt $offlinePassLine) {
            $failures.Add("offline : $PassMarker appears before $RestoredMarker")
          }
        }
        if ($null -ne $offlineExit) {
          if ($offlineTimedOut) { $failures.Add("offline : TIMEOUT after ${OfflineTimeout}s - killed") }
          else { $failures.Add("offline : exited on its own (code $offlineExit) before the runner stopped it") }
        }
        if (Test-LogPatternAny @($offlineErr, $offlineOut) $ScriptErrorPattern) {
          $failures.Add("offline : script error -> " + (Get-FirstMatchAny @($offlineErr, $offlineOut) $ScriptErrorPattern))
        }
        $offlineMarkerHits = Get-LogMarkerHits @($offlineOut, $offlineErr) $ActiveMarkerRegex
        if ($offlineMarkerHits.Count -gt 0) {
          $failures.Add("offline : markers -> " + ($offlineMarkerHits -join " | "))
        }
        if ($failures.Count -eq 0) {
          Write-Output "OFFLINE: PASS (RESTORED after ~${offlineRestoredSeconds}s; PASS)"
        }
      }
    }
  }
}
catch {
  $failures.Add("runner : $($_.Exception.Message)")
}
finally {
  Stop-ProcessSafe $client
  Stop-ProcessSafe $server
  Stop-ProcessSafe $offline
  $scenarioInfo = Get-DirectoryEntry $PaksetScenarioDir $ScenarioName
  if ($scenarioInfo.Exists -and $scenarioInfo.IsLink) { Remove-Junction $ScenarioLink }
  if ($createdPaksetScenarioDir -and (Test-Path -LiteralPath $PaksetScenarioDir -PathType Container)) {
    # Non-recursive: removes the directory only if it is empty.
    & cmd.exe /c "rmdir `"$PaksetScenarioDir`"" 2>$null | Out-Null
  }
}

if ($failures.Count -gt 0) {
  Write-Output "RESULT: FAIL"
  foreach ($f in $failures) { Write-Output "  - $f" }
  Write-Output "Logs:"
  foreach ($p in @($serverErr, $serverOut, $clientErr, $clientOut, $offlineErr, $offlineOut)) {
    if ((Get-EntryInfo $p).Exists) { Write-Output "  $p" }
  }
  if (Test-Path -LiteralPath $PreservedSave) { Write-Output "Preserved join save: $PreservedSave" }
  Write-Output "---- log marker hits ----"
  $allHits = Get-LogMarkerHits @($serverOut, $serverErr, $clientOut, $clientErr, $offlineOut, $offlineErr) $ActiveMarkerRegex
  if ($allHits.Count -eq 0) { Write-Output "  (none)" }
  else { foreach ($h in $allHits) { Write-Output "  $h" } }
  Write-Output "---- log tails ----"
  Write-LogTails @($serverErr, $serverOut, $clientErr, $clientOut, $offlineErr, $offlineOut)
  exit 1
}

Write-Output "RESULT: PASS (join save $PreservedSave restored and continued normally offline)"
exit 0
