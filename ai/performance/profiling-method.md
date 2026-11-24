---
status: draft
verified: master @ 3f5046b79a plus scripts/perf/ and run-perf-suite.ps1 additions; ex-15 @ 1ddc256 (2026-11-24, paced-server metric and forced window)
---
# Profiling and hot-path measurement method

**Covers:** the workflow for diagnosing CPU hotspots, verifying memory-layout changes, and running valid A/B performance comparisons on Windows using `scripts/run-perf-suite.ps1`, `scripts/perf/etl-lines.cs` (`ai/tools/perf/etl-lines.exe`), `scripts/perf/etl-hotspots.cs` (`ai/tools/perf/etl-hotspots.exe`), `scripts/perf/pdb-sizes.cs` (`ai/tools/perf/pdb-sizes.exe`), and MSVC `dumpbin.exe`. Parent document (canonical fixture, build configurations, Event Tracing for Windows (ETW) setup, and hotspot inventory): [performance](../performance.md). Determinism and network-sync rules: [sync-and-determinism](../sync-and-determinism.md). Automated test suites referenced in verification: [scripting-and-tests](../scripting-and-tests.md).

## Two-tier profiling workflow

Do not guess which statements inside a hot function cause stalls, and do not run full 120-second stack-resolved captures during exploratory tuning. Split performance work into two tiers:

1. **Fast diagnosis tier (source-line and instruction attribution, ~2 minutes per run).** Run `scripts/run-perf-suite.ps1` with `-Mode Capture -Trace -WindowSec 30 -SettleSec 10 -FastLinesOnly -LineFilter <substrings>`, where `<substrings>` is a comma-separated PowerShell list of case-insensitive function-name substrings (for example `-LineFilter following_limit,private_car_t`). Pass `-KeepEtl` to keep the raw Event Trace Log (`.etl`) file so additional functions can be queried without re-running the game. At the default 1 kHz ETW CPU sampling rate, a 30-second window yields roughly 30,000 main-thread samples, giving a standard error below 0.3 percentage points on a dominant hotspot. `etl-lines.exe` analyses the raw `.etl` in under one second.
2. **Canonical verification tier (final A/B proof, ~4 minutes per run).** Run `scripts/run-perf-suite.ps1 -Mode Capture -Trace -WindowSec 120` (default `-SettleSec 20`, `-Threads 4`) on clean `Profile (server)|x64` builds containing no temporary `TEST` or `TESTING` code, for both the unmodified baseline and the candidate change. Use this full stack-resolved capture (`etl-hotspots.exe`) for inclusive call-tree shares and recorded A/B numbers.

Always pass `-nomidi` in any custom game command line (the suite script does so automatically).

## Source-line and instruction-level diagnosis

Function-level totals from `etl-hotspots.exe` do not distinguish arithmetic work from memory stalls, un-inlined calls, or specific branches inside a large hot function.

### Line and instruction-offset sampling (`etl-lines`)

Source: `scripts/perf/etl-lines.cs`. Deployed binary: `ai/tools/perf/etl-lines.exe`, placed beside `ai/tools/perf/Microsoft.Diagnostics.Tracing.TraceEvent.dll` and its native Debug Interface Access (DIA) helper DLLs (`ai/tools/perf/amd64/msdia140.dll`, etc.), matching the layout of `ai/tools/perf/etl-hotspots.exe`. Build command (from repository root):

```
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /optimize+ /platform:x64 ^
  /r:System.Net.Http.dll ^
  /r:ai\tools\perf\Microsoft.Diagnostics.Tracing.TraceEvent.dll ^
  /out:ai\tools\perf\etl-lines.exe scripts\perf\etl-lines.cs
```

Usage on a kept `.etl` trace:

```
ai\tools\perf\etl-lines.exe <trace.etl> <exeBaseName> <pdbPath> [funcSubstringFilter...]
```

Mechanism [CODE]: `etl-lines.exe` streams the raw `.etl` directly through `ETWTraceEventSource`, skipping both TraceEvent's `.etlx` index conversion and symbol-server lookups. It records the game module's address range (`ImageDCStart` / `ImageLoad`), counts `PerfInfoSample` instruction pointers inside that module by Relative Virtual Address (RVA, the byte offset from the module's load base address), and resolves each sampled RVA against the local Program Database (`.pdb`) via `SymbolReader.OpenNativeSymbolFile` (`NativeSymbolModule.FindNameForRva` and `SourceLocationForRva`). Output lists per-thread sample counts, the top 30 self functions, the top 60 `function | file:line` entries matching the filters, and the top 40 `function+offset (RVA)` entries. Analysis completes in under one second on a 30-second trace [EXECUTION-VERIFIED:2026-11-23].

### Matching RVAs to disassembly (`dumpbin`)

To determine whether a hot source line stalls on a pointer chase, a branch, an un-inlined call, or arithmetic, disassemble the address range around the hot function using the MSVC x64 `dumpbin.exe`:

```
dumpbin.exe /disasm /range:<imageBase+startRva>,<imageBase+endRva> simutrans\Simutrans-Extended-Profile-server.exe
```

Obtain `<imageBase>` from `dumpbin.exe /headers <exe>` (typically `0x140000000` for x64 builds) or from the `base=` field printed by `etl-lines.exe`.

**Skid rule:** on out-of-order x86-64 processors, a sampled-CPU interrupt triggered while the core is stalled on a cache-missing memory load typically attributes the sample to the instruction immediately following that load (one-instruction skid). When a hot RVA lands on a cheap register instruction (`test`, `cmp`, `mov reg, reg`, or a conditional jump), inspect the immediately preceding memory load instruction: that load is the cache miss.

## Interpreting CPU-bound server captures

On the canonical `bb-10-sep-2023.sve` fixture (defined in [performance](../performance.md)), when `karte_t::sync_step()` takes longer than `fix_ratio_frame_time` (the target milliseconds per frame in `FIX_RATIO` server pacing in `simworld.cc`), the server's main thread runs at 100% of one CPU core continuously (`Capture` mode does not sleep between steps). This creates two measurement traps:

1. **Total process CPU percentage does not drop while the main thread remains saturated.** Speeding up a main-thread hot path causes the server to complete more simulation steps in the fixed capture window rather than using less CPU.
2. **Individual function self percentages are not directly comparable across builds.** Inlining a callee moves its samples into the caller; conversely, enlarging a function can stop the compiler from inlining a callee (such as `karte_t::lookup`), moving samples out of the caller. Speeding up one function also raises the relative share of every unchanged function.

### Valid throughput comparison in equal-window captures

Compare two canonical captures taken on the same machine, fixture, thread count, settle time, and window duration using unchanged per-step and per-sync-step functions as throughput proxies [EXECUTION-VERIFIED:2026-11-23]:

- **Unchanged per-step functions:** `karte_t::step`, `step_passengers_and_mail_threaded`, `convoi_t::step`, `karte_t::find_destination`, `weighted_vector_tpl<gebaeude_t *>::at_weight`.
- **Unchanged per-sync-step functions:** `convoi_t::calc_acceleration`, `float32e8_t::operator*`, `convoy_t::calc_move`, `rail_vehicle_t::can_enter_tile`.

When untouched by the change, each of these functions performs the same amount of work per step or per sync step, so the ratio of its inclusive sample count in the candidate capture to its inclusive sample count in the baseline capture measures the ratio of simulation steps completed per second. Check at least four independent unchanged functions together; close agreement across them confirms the whole-simulation speedup factor `S` and flags any run-to-run phase skew from periodic work such as `path_explorer_t`.

To compute the per-step speedup of an optimised function itself, compare `samples_base / (samples_candidate / S)` (equivalently, compare the ratio `optimised_samples / unchanged_samples` between baseline and candidate). If an optimisation makes `karte_t::sync_step()` fast enough that the server catches up to real-time pacing (`fix_ratio_frame_time`) and begins calling `dr_sleep()`, main-thread CPU samples will fall below 100% of the window duration; in that regime, compare total main-thread CPU time per unit of simulation progress instead.

A metric that stays comparable across both regimes is **inclusive samples of an unchanged per-step function per 1000 process CPU samples**. For example, divide the inclusive `karte_t::step` sample count by the total process sample count and multiply by 1000; a rise means the simulation completes more steps per unit of CPU. Because both counts come from the same capture, the result is independent of how much the process slept. On the canonical 120 s capture the value is about 34 for the gargantuan fixture.

## One-shot runtime distribution census

Sampled CPU profiles show where time is spent, not how often each branch, occupancy count, or early-exit condition occurs. Collect those runtime distributions in a single short run instead of guessing:

1. Add `#ifdef PROFILE` counters marked with `// TESTING` (per [AGENTS.md](../../AGENTS.md) rule 10) gated by a static invocation counter inside `karte_t::sync_step()` (for example invocations 50..149 after load, skipping 50 warmup sync steps and measuring elapsed wall milliseconds with `dr_time()`). Do not compare against `karte_t::sync_steps == 50`: `karte_t::sync_steps` is persisted in the savegame and starts at the saved step number on load.
2. Print the summary through `dbg->error` (not `dbg->warning`, which `-debug 1` suppresses) and call `_exit(0)` immediately at the end of the counted window so the whole run finishes in ~80-90 seconds including save load.
3. Revert all `TESTING` code immediately after reading the log, and rebuild clean binaries before running any A/B capture or test suite.

## Inspecting object sizes from the PDB (`pdb-sizes`)

Memory bandwidth and RAM footprint are primary constraints (see [project-notes](../project-notes.md)). Check exact C++ class and struct sizes before and after layout changes directly from the compiled `.pdb` files without editing game code.

Source: `scripts/perf/pdb-sizes.cs`. Deployed binary: `ai/tools/perf/pdb-sizes.exe`. Build command (from repository root):

```
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /optimize+ /platform:x64 ^
  /r:System.Net.Http.dll ^
  /r:ai\tools\perf\Microsoft.Diagnostics.Tracing.TraceEvent.dll ^
  /r:ai\tools\perf\Dia2Lib.dll ^
  /out:ai\tools\perf\pdb-sizes.exe scripts\perf\pdb-sizes.cs
```

Usage:

```
ai\tools\perf\pdb-sizes.exe simutrans\Simutrans-Extended-Profile-server.pdb grund_t objlist_t vehicle_base_t private_car_t
```

MSVC and GCC/Clang differ in `GCC_PACKED` (empty on MSVC) and `std::string` layouts (for example, `sizeof(grund_t)` is 48 bytes on MSVC x64 and 40 bytes on GCC/Clang x64). Verify packing assumptions for both compilers when placing fields in alignment padding.

## A/B build and runner hygiene

- **Build baselines from clean Git state.** Never reconstruct a baseline by copying selected source files over a modified working tree. MSBuild silently skips any `.cc` file whose modification timestamp is older than its `.obj` (`Copy-Item` preserves timestamps), and leaving modified headers or companion `.cc` files behind produces a hybrid binary. Use `git stash` or a clean `git worktree` for the baseline binary, save both the `.exe` and `.pdb` together, check the `#<rev>` line in the run log, and confirm expected or absent symbols in the baseline `.pdb` before measuring.
- **Keep `.exe` and `.pdb` paired.** `etl-lines.exe`, `etl-hotspots.exe`, and `pdb-sizes.exe` require the exact `.pdb` produced when linking the profiled `.exe`; rebuilding overwrites `simutrans\Simutrans-Extended-Profile-server.pdb`, breaking symbol resolution for earlier traces unless the matching `.pdb` was archived alongside the `.exe`.
- **Server log filename.** In `-server <port>` mode, Simutrans writes `simu-server<port>.log` in the working directory rather than `simu.log`.
- **GUI-subsystem wait in PowerShell.** On Windows, `Simutrans-Extended-Profile-server.exe` is linked as a GUI-subsystem executable, so invoking it directly with `&` in PowerShell returns immediately without waiting for exit. Use `Start-Process -PassThru` and wait on the process handle (or pass `-Wait`).
- **Avoid concurrent heavy work during captures.** Do not run parallel builds or test suites while an ETW capture is running.
- **Force the measurement window when the load plateau never confirms.** `run-perf-suite.ps1` declares load complete from a working-set/read-I/O plateau. The first path-explorer rebuild after loading the gargantuan fixture can keep the working set growing, so the detector waits until its timeout and the window never starts. For a deterministic window start, pass `-MinLoadSec 150 -LoadWsFloorMb 0 -WsQuietMb 1000000`; the suite then declares load at the first sample after 150 s. Use `-SettleSec 20` and `-WindowSec 60` for diagnosis, or `-WindowSec 120` for canonical captures.
- **Retry a fixture that exits during load with `-1073740791` and `sim_new_handler() OUT OF MEMORY`.** The gargantuan fixture's peak commit exceeds the free commit on a machine that is already heavily committed; the same binary can load on a later attempt. Check `\Memory\Committed Bytes` against `\Memory\Commit Limit` before treating it as a regression.
- **`etw-ctl.ps1` fixes its command/status directory to `ai\temp\perf`.** The elevation helper used by `-Trace` reads and writes `ai\temp\perf\etw-command.txt` and its status files, so a traced capture cannot use a scratch `-WorkDir`. Run traced captures in the main suite workdir and keep only the build under the scratch tree; untraced captures can use any workdir.
- **`-LineFilter` produces an empty line section when passed through `powershell -File`.** A comma-separated value such as `-LineFilter following_limit,private_car_t` currently yields an empty "Top 60 Source Lines" section; the observed symptom is consistent with the whole value arriving as one filter element, which matches no function. Until the script is changed, run `ai\tools\perf\etl-lines.exe <trace.etl> <exeBaseName> <pdbPath> <filter>...` manually on the kept trace, passing each substring as a separate argument.
- **Verify determinism and assertions after optimisation.** Rebuild a configuration with assertions enabled (`Optimised debug|x64` or `Debug|x64`) and run the network smoke test (`scripts/run-smoke-tests.ps1`), the relevant pending-runtime and reload/checklist suites, and the standard Squirrel suite (`tests/*.nut`) documented in [scripting-and-tests](../scripting-and-tests.md) before accepting any hot-path change.

## Open questions

- Does `scripts/perf/etl-lines.cs` need a flag to emit per-caller inclusive attribution for a specified target function (for example all direct callers of `grund_t::get_weg`), or is `etl-hotspots.exe` sufficient once `-KeepEtl` preserves the raw `.etl`?
- Should a built-in `PROFILE`-only `--stop-after-sync-steps N` option be added to `simmain.cc` so deterministic fixed-step wall-clock timing can be run on clean binaries without temporary `TESTING` edits?
