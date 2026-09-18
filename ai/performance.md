---
status: draft
verified: mapgen-perf-fixes @ 652568623
---
# Performance & profiling

**Covers:** scripts/run-perf-suite.ps1, the automated ETW profiling pipeline in scripts/perf/
(cpu-sample.wprp, etl-hotspots.cs, etw-ctl.ps1, install-perf-task.ps1; analyzer deployed under
ai/tools/perf/, symbol cache ai/tools/symbols/, both gitignored), the profiling fixture
(bb-10-sep-2023.sve + branch pakset binaries in simutrans/), the MSVC "Profile" configuration,
the DEBUG/PROFILE-only benchmark command-line options (`-until`, `-times`, `-fast-network-sync`
in simmain.cc), and the performance hotspot inventory (play fixture and `-generate_map` worldgen).

## Why this doc

Almost all live simulation code runs under `karte_t::step()` or `karte_t::sync_step()` and is
performance critical — full rules in [project-notes](project-notes.md) (memory bandwidth is the
biggest constraint on the huge maps now commonly played). Any change touching those paths needs a
*measured* performance check, not just reasoning. This doc defines the canonical way to measure and
lists the known hotspots so agents know where care is needed and where to look when profiling.

## The canonical profiling suite

### Fixture

- **Savegame:** `bb-10-sep-2023.sve`, the gargantuan Bridgewater-Brunel server save, in the
  maintainer's `<user>\Documents\Simutrans\save\` directory [RECOLLECTION:2026-09-09]. A
  Bridgewater-Brunel-era map: route-search coordinates span at least ~7700 × ~2310 tiles
  [execution-verified 2026-09-09]; in-game date November 1993 [RECOLLECTION:2026-09-09]. The file
  is ~400 MB, zstd-compressed ("ZD" magic; loading requires a USE_ZSTD build). Not tracked in git;
  the suite hardlinks it into its sandbox (same volume), so there is exactly one copy on disk.
- **Pakset:** the built pakset binaries in the repo working directory `simutrans/` — use the right
  one for the branch: **master → `simutrans\pak128.Britain-Ex-0.9.4`; ex-15 →
  `simutrans\pak128.Britain-Ex`** [RECOLLECTION:2026-09-09]. An ex-15 pakset fatals in the object
  readers on master and vice versa (scripts/run-smoke-tests.ps1 header).
- Loading the fixture with the master pakset produces no missing-object warnings (the pakset fully
  covers the 2023 save); ~30 one-time startup menu errors (tunnel-builder lookups) are expected
  noise, not yet investigated [RECOLLECTION:2026-09-09 user: do not investigate now].

### Build configuration

- **`"Profile|x64"`** (graphical) and **`"Profile (server)|x64"`** (headless, `COLOUR_DEPTH=0`) in
  Simutrans-Extended.vcxproj are the profiling builds, release-like in every respect: `MaxSpeed`,
  `NDEBUG`, `MultiThreadedDLL`, no `DEBUG` and no `MSG_LEVEL` — so all `DBG_*` logging macros
  compile to nothing (zero logging overhead in hot paths), asserts are off, no `DEBUG_FREELIST`
  allocator instrumentation — while **`PROFILE`** is defined so `-until`/`-times` exist, and link
  PDBs are generated for profiler symbol resolution. Outputs:
  `simutrans\Simutrans-Extended-Profile.exe` / `Simutrans-Extended-Profile-server.exe` (+ .pdb).
  Both wired into the .sln.
- Profiling must run against release-like builds: debug-defined builds bias results
  [RECOLLECTION:2026-09-09]. **"Optimised debug|x64" is for optimised *debugging*, not
  profiling**: it is optimised code, but `DEBUG=3` compiles `DBG_*` macro calls into hot paths
  (runtime level-gated, still called), keeps asserts active and `DEBUG_FREELIST` on.
- `-until` and `-times` are compiled **only in DEBUG/PROFILE builds** (simmain.cc,
  `#if defined DEBUG || defined PROFILE` blocks); the suite script pre-flights the exe's `-h`
  output for this.
- Command line build (project-level; no solution needed):
  `MSBuild.exe Simutrans-Extended.vcxproj "/p:Configuration=Profile" "/p:Platform=x64" /m`
- **zstd caveat:** every MSVC configuration links the old debug-built zstd static lib
  (`..\zstd-1.4.4\build\VS2010\bin\x64_Debug`). A freshly built release zstd lib cannot be linked
  into v143 game builds: its objects force link-time code generation, which then fails against the
  ancient bytecode in `libbz2.lib` (C1047/LNK1257) [execution-verified 2026-09-09]. Consequence:
  **load-phase timings include debug-build (unoptimised) zstd decompression** — treat absolute
  load numbers with caution; simulation capture is unaffected.
- **Verify `revision.h` after MSVC builds**: the pre-build `revision.jse` historically wrote a
  stale revision (its git-output length check rejected modern `--short=7` output; fixed 2026-09-09
  to accept 7–12 trimmed hex digits). The embedded revision identifies the profiled build in every
  run's log header — check it matches `git rev-parse --short HEAD` before trusting a profiling
  session. [execution-verified 2026-09-09: stale 2020 revision reproduced; fix verified]

### Running the suite

`scripts/run-perf-suite.ps1` (Windows; mirrors the run-smoke-tests.ps1 sandbox pattern — junctioned
pakset/font/text/themes, suite simuconf, hardlinked fixture, `-set_workdir … -singleuser`; it never
touches the user's Simutrans profile). **Suite state is persistent under `ai\temp\perf\`** (sandbox
workdir + `results\<stamp>-<mode>\` with summary.txt + simu.log [+ trace.etl]); it is exempt from
the AGENTS.md rule-8 session-end clearing of `ai\temp/` [RECOLLECTION:2026-09-09 user decision].
The pakset fixture is the live per-branch pakset directory in `simutrans/` — rebuilds are accepted
as part of the fixture's evolution [RECOLLECTION:2026-09-09 user decision]. The same save fixture
is intended for ex-15 (loaded through the 14.x savegame upgrade path, with the ex-15 pakset);
verified there 2026-09-11 (ex-15 @ 60228c088 + uncommitted haltlist guards, 120 s server-paced
window): the fixture loads through the upgrade path and a full traced Capture passes. Note the
ex-15 pakset lacks the `TimingPoint` building: 437 buildings are removed at load via the
missing-desc path, so the ex-15 world state differs slightly from master's
[EXECUTION-VERIFIED:2026-09-11].

| Mode | Command line core | Measures |
|---|---|---|
| `Load` | `-until 0 -debug 3` | Load cost: pakset + savegame load, at most one sim step, clean exit. Wall time of the whole run (zstd caveat above). (graphical build) |
| `Capture` | `-server <port> -debug 1` | Server workload: load, then run as a server at its normal pace (FIX_RATIO; the savegame's own settings drive frame/step pacing; loopback-only, no clients) for `-WindowSec`, then kill. Default exe: headless Profile (server) build (no display cost); `-Graphical` selects the graphical Profile build. |
| `CaptureGui` | `-debug 1` | Client workload: load, then run as an offline client at normal single-player pace (display + simulation — the way players actually run) for `-WindowSec`, then kill. Use for graphics-code hotspots. (graphical build) |
| `Times` | `-times -until 0 -debug 3` | Built-in drawing micro-benchmarks (show_times in simmain.cc) on the loaded world; results parsed into the summary. (graphical build) |

- `-until YEAR.MONTH` quits at a month boundary and switches the game to fast-forward
  (`-until 0` = load-then-exit). DEBUG/PROFILE builds only. **Fast-forward is never used for
  profiling** — it distorts realistic pacing (and never applies to network mode); it is only a
  tool for reaching a point in game time quickly [RECOLLECTION:2026-09-09].
- **Load completion is detected from a working-set/read-I/O plateau** (quiet growth above floors,
  three consecutive samples, and a minimum load time — mid-load allocation lulls must not trigger
  it; page-fault image reads trickle on after load, so the working-set signal is the reliable one).
  The run stays at `-debug 1` throughout so logging never perturbs the profile (see the logging
  caveat). The window starts after the plateau + `-SettleSec` (default 20 s) margin. Load takes on
  the order of 1.5 minutes on the maintainer's machine [EXECUTION-VERIFIED 2026-09-09].
- `-threads` is pinned explicitly (default 4, the env default) for run-to-run comparability.

### Logging caveat

The game log flushes every line (`init_logging(…, force_flush=true)`). While the sim runs, direct
`dbg->warning` route-search diagnostics (`route_t::intern_calc_route` "Problem with heuristic",
dataobj/route.cc) stream continuously at `-debug ≥ 2` [execution-verified 2026-09-09]. Hence
**capture runs use `-debug 1`** (near-silent); `-debug 3` only for Load/Times runs where the sim is
not (meaningfully) running. In DEBUG-defined builds the problem is far worse: `DBG_*` macro calls
are compiled in and flood the log (a single post-load step produced ~155k log lines in the
"Optimised debug" build vs ~2.5k in the Profile build) [execution-verified 2026-09-09] — another
reason profiling uses the Profile configuration.

### Automated profiling pipeline (-Trace)

`-Mode Capture|CaptureGui -Trace [-TracePhase Window|Load]` runs a complete profile
non-interactively [EXECUTION-VERIFIED:2026-09-10]:

1. The suite starts a sampled-CPU ETW capture (WPT `wpr.exe` + the custom sampled-only profile
   `scripts/perf/cpu-sample.wprp` — no context-switch events, ~10× smaller traces than the
   builtin "CPU" profile). `-TracePhase Window` (default) traces the steady-state window after
   the load plateau + settle; `-TracePhase Load` traces from process start to the plateau, for
   load-cost work.
2. Elevation: ETW kernel sessions need admin and the suite shell is non-elevated, so wpr
   start/stop go through the **SimPerfEtw scheduled task** (one-time elevated install:
   `scripts/perf/install-perf-task.ps1`; command/status-file protocol in ai/temp/perf/). Use the
   WPT wpr.exe — the inbox wpr (System32) has a broken `-stop` (RPC_E_CHANGED_MODE) on this
   machine [EXECUTION-VERIFIED:2026-09-10].
3. At window end the TraceEvent analyzer (source `scripts/perf/etl-hotspots.cs`; deployed with
   its managed + native DLLs in `ai/tools/perf/`) writes `hotspots-self.csv` /
   `hotspots-incl.csv` (function, samples, %) into the results dir and the top-10 self list into
   summary.txt. The raw ETL is deleted on success, kept on failure. Analyzer caveats, both
   handled in the checked-in source: DIA-based PDB parsing needs TraceEvent's native DLLs
   (amd64\msdia140 etc.) beside the exe, and the MS symbol servers reject .NET 4.0's default
   TLS 1.0 (TLS 1.2 is forced).
4. Comparing runs: diff the pct columns of two hotspots CSVs (same machine, mode, window,
   fixture, threads). Compare function CPU share, not absolute wall time; wall-time comparisons
   only between clean-exit modes (Load/Times).
- gprof is unusable on this toolchain: mingw64 gcc 16.2 `-pg` emits zero instrumentation
  (no mcount/fentry calls) [EXECUTION-VERIFIED:2026-09-10].
- **Visual Studio Performance Profiler** remains the manual fallback for interactive drill-down
  (launch/attach against the Profile builds; PDBs next to the exe). PerfView CLI has no analysis
  commands (GUI-only) [EXECUTION-VERIFIED:2026-09-10].
- **Window length:** one to two minutes of paced running on this fixture is enough to identify
  hotspots for most purposes; much longer runs are only needed to observe long-cycle behaviour
  (e.g. a complete path-explorer run), which is rare and better studied on a small map (the CI
  demo fixture) [RECOLLECTION:2026-09-09].

## Mapgen profiling (`-generate_map`)

Alternative profiling target to the play fixture: headless world generation, fully deterministic
from `-map_seed` (all RNG streams seeded, simmain.cc) — an identical seed yields an identical
`MAP-GEN:` result line across server/GUI and release/optimised-debug builds. That line, plus the
instrumentation counters below, is the oracle for proving a change output-invariant
[EXECUTION-VERIFIED:2026-09-18].

Canonical profiling case: `-generate_map -map_size 1024,1024 -map_seed 42 -map_towns 50
-map_factories 8 -map_attractions 4 -map_water_level -2`, pak128.Britain-Ex-0.9.4,
`Profile (server)|x64`. Invariant line at mapgen-perf-fixes @ 652568623:
`MAP-GEN: PASS size=1024x1024 seed=42 towns=50 (cities=42 factories=13)`.

Runner hygiene [EXECUTION-VERIFIED:2026-09-18]:
- Delete `settings-extended.xml` from the working directory before every run (map settings
  persist between runs); DEBUG-config builds use `settings-extended-debug.xml` instead (simmain.cc).
- MSBuild silently skips a source file whose timestamp is older than its `.obj` — PowerShell
  `Copy-Item` preserves timestamps, so fix them after restoring A/B sources from a copy.
- Profile the `Profile` configuration: at `-debug ≥ 3` worldgen floods `way_builder_t::init_builder`
  log lines (~19k on the 42-city map; simcity.cc constructs a `way_builder_t` per direction only
  to call `check_slope`), and per-line log flushing dominates unoptimised Debug builds.

Instrumentation: `MAP-GEN-T` phase timings and `MAPGEN-D` per-city growth counters exist as
LOCAL-ONLY commits on `mapgen-perf-fixes` (2a99f36c5/a3e08150b, 479523d90 — strip before PR;
counter fields documented at `stadt_t::growth_diag_t`). DEBUG||PROFILE-gated, no RNG consumed,
counter-identical across repeat runs. For ETW, trace the whole generation via the pipeline above
and analyse while the PDB still matches the binary being profiled.

Current cost structure (canonical case; init ≈ 69 s) [EXECUTION-VERIFIED:2026-09-18]:
- City growth ≈ 60 s (~87%), of which the largest city (16.5k buildings) ≈ 46–48 s;
  `create_rivers` ≈ 7 s; every other phase ≲ 1%.
- Growth hotspots (ETW, self % of in-process samples): `stadt_t::bewerte_loc` 28.9 (rule-entry
  loop over candidates × rules × rotations), `stadt_t::build` 10.2 (candidate-collection sweep:
  every `build()` call re-collects all natur tiles in bounds), `simrand` 10.0 (rule gate draws —
  these define the output and cannot be removed without changing it), `stadt_t::reset_city_borders`
  9.0 (runs on every building success), `stadt_t::compute_loc_flags` 6.1, `grund_t::get_weg` 5.5.
- Next targets in that order: the candidate-collection sweep, then `reset_city_borders` batching.

City-growth structure needed to work on it safely [CODE mapgen-perf-fixes @ 652568623]:
- Growth uses an exhaustive candidate sweep with enlarge-bounds-on-failure — the deliberate
  Extended divergence (7bd1947ea). Standard's `build()` (checked at its current master, 2026-09)
  is the single-random-tile algorithm, which survives in Extended only as the
  `quick_city_growth=1` path; no upstream port of the sweep exists.
- The `bewerte_loc` tile predicates (public road, fundament/house, natur, slope, stop) are
  memoised per sweep (`stadt_t::loc_cache_*`, rect = bounds+3, sweeps with ≥ 64 candidates).
  INVARIANT for anyone touching `build()`/`build_road`: the cache is valid only while no tile
  mutates — any tile mutation during a sweep (terraformation, excess-road removal, road/bridge
  construction) must deactivate it for the rest of the call; the current deactivation points are
  the flatten/water-remediation block and the head of the `connection_roads` mutation cluster in
  `build_road`.
- Failed sweeps enlarge the bounds (up to 4× per `build()`); enlarged rows are marked
  `set_city(this)` and only unmarked on building *success* (`reset_city_borders`) — the resulting
  bounds leak is open (→ [known-bugs](known-bugs.md)). Mild stalls at hilly sites (≈1–2 s per
  affected city at 1024²) are accepted behaviour; a stall cap was rejected as contrary to growth
  design intent.
- Rule gating: a rule is evaluated when `simrand(8 + distribution_weight) == 0`; weight −8 =
  guaranteed and consumes no RNG (`simrand(0)` draws nothing). Pakset `.chance` and
  `.distribution_weight` keys are both read (→ [economy-and-passengers](economy-and-passengers.md)).
- Output caveats: generated town/factory counts differ from the requested numbers
  (→ [known-bugs](known-bugs.md)); two `bewerte_loc` legend-vs-code mismatches are cosmetic and
  fixing them would change output: 'S' passes private-road tiles, and 'U'/'u' are inverted
  relative to the cityrules.tab legend.

## Hotspots

Measured 2026-09-10 on the canonical fixture via the -Trace pipeline (server-paced Capture,
120 s window, threads=4, master @ d40847e90; 262,784 CPU samples, 57% in the game module)
[EXECUTION-VERIFIED:2026-09-10]. Thread attribution (`hotspots-threads.csv`, root-most game
frame per stack; second capture, same parameters, master @ 4b2b927a7): main thread 73.9%;
workers: `unreserve_route_threaded` 11.8%, `step_passengers_and_mail_threaded` 4.6%,
`display_region_thread` 3.4%, `dr_flush_screen` 2.6%, `step_individual_convoy_threaded` 1.7%,
`check_road_connexions_threaded` 1.5%; the `<no resolved game frame>` bucket (stack-truncated,
unattributable time) is 0.001%. `pthreadvc2!?`/`msvcr100!?` frames are the nameless thread-start
roots of pthread-win32 worker threads (no PDBs): ~24% incl but 0.13% self — inert, not lost time
[EXECUTION-VERIFIED:2026-09-10]. Percentages below are share of matched in-game samples; "incl"
counts a function for every stack it appears in, "self" counts only leaf frames. Treat
everything under `step()`/`sync_step()` as hot (project-notes). Domain mechanics:
[simulation-core](simulation-core.md), [routing-and-scheduling](routing-and-scheduling.md),
[vehicles-and-convoys](vehicles-and-convoys.md), [threading](threading.md),
[rendering](rendering.md). The structural/design-style causes of these costs and the feasible
data-oriented alternatives: [data-layout-and-design-style](data-layout-and-design-style.md).

**ex-15 comparison** (first ex-15 capture, same fixture/window/threads, ex-15 @ 60228c088 with
uncommitted haltlist guards; 358,011 samples, 47% in game module) [EXECUTION-VERIFIED:2026-09-11]:
route-reservation clearing's share roughly DOUBLES versus master — `unreserve_route_range` 26.0%
self (master ~12%), `unreserve_route_threaded` worker 26.8% (11.8%) — while
`sync_list_t::sync_step` drops to 12.8% self (23.7%) and `private_car_t::sync_step` to 4.2%
(9.0%). Single run each and the ex-15 world state differed (437 missing TimingPoint buildings
removed at load) — repeat before acting on the deltas.

1. **Synced-object stepping — `karte_t::sync_list_t::sync_step` (simworld.cc).** 23.7% self /
   67.3% incl (`karte_t::sync_step` overall 69.5% incl). The dominant single leaf cost: the
   per-step walk over the synced moving-object lists itself, before the objects' own sync_step
   bodies — a raw iteration/memory-bandwidth cost on a map with this many moving objects.
2. **Convoy physics & stepping — `convoi_t` (simconvoi.cc), `vehicle_base_t`/`convoy_t`.**
   `convoi_t::sync_step` 22.4% incl; `calc_acceleration` 18.9% incl / 8.3% self;
   `vehicle_base_t::do_drive` 10.0% incl; `convoy_t::calc_move` 6.1% incl;
   `convoy_t::calc_min_braking_distance` 3.0% incl; the sync-safe fixed-point `float32e8_t`
   operators ~7% self combined (`operator*` 4.9%, `operator+` 1.2%, `operator/` 0.6%).
   Immediately after loading the fixture there is a mass reroute wave (the first step emits
   ~10⁵ route-search log lines at -debug ≥ 2 in DEBUG builds) [EXECUTION-VERIFIED:2026-09-09].
3. **Route reservation clearing — `convoi_t::unreserve_route_range` +
   `unreserve_route_threaded`.** 12.8% incl / 12.4% self — a first-class hotspot with thousands
   of convoys running (→ [signals-and-blocks](signals-and-blocks.md)).
4. **City traffic — `private_car_t::sync_step` 18.6% incl / 9.0% self (`hop_check` 7.4% incl);
   `pedestrian_t::sync_step` 1.2% self.** Not previously suspected at this rank.
5. **Passenger generation — `stadt_t` (simcity.cc) / `karte_t`.**
   `karte_t::generate_passengers_or_mail` 4.8% incl (`step_passengers_and_mail_threaded` 4.9%
   incl), `karte_t::find_destination` 3.0% incl. Monthly cadence; city *growth* is not hot
   [RECOLLECTION:2026-09-09, consistent with the measured absence of growth functions].
6. **Map/ground access — `grund_t::get_weg` 3.4% self, `karte_t::lookup` 2.4% self,
   `grund_t::get_neighbour` 2.6% incl, `planquadrat_t::get_boden_in_hoehe` 1.0% self.**
   Raw lookup cost over huge maps; parts of the tile walk are multi-threaded — machinery and its
   fragility: [threading](threading.md).
7. **Halt cargo handling — `karte_t::check_transferring_cargoes` 2.6% incl / 2.6% self
   (`haltestelle_t`, simhalt.cc).** Moderate, not dominant.
8. **Route search (A\*) — `route_t::intern_calc_route` (dataobj/route.cc).** Minor in *steady
   state* on this fixture: `route_t::find_route` ~1.2% self in the measured window — the
   continuous heuristic-failure diagnostics (`heur` ~10× `cost`
   [EXECUTION-VERIFIED:2026-09-09]) do not translate into a top steady-state cost. Concentrated
   in the post-load reroute wave instead. `max_route_steps` (simuconf, default 1.5M) bounds
   search memory.
9. **Path explorer — `path_explorer_t` (path_explorer.{h,cc}).** Centralised, *steppable*
   Floyd-Warshall connection search, budgeted per step via `limit_set_t`; runs concurrently and
   governs how quickly in-game routes update, not framerate [RECOLLECTION:2026-09-09]. It only
   runs when something has changed since its last completed pass; on a busy server game changes
   outpace it so it is effectively always running, but it can be dormant (e.g. the small demo
   fixture) [RECOLLECTION:2026-09-10]. It was near-dormant in the measured fixture window
   (`get_path_between` 0.5% incl, `path_explorer_threaded` 0.007%)
   [EXECUTION-VERIFIED:2026-09-10].
10. **Display — simview/simgraph pipeline.** Secondary in server-paced running on this fixture
    (`main_view_t::display_region` 4.9% incl), but dominated a CaptureGui window on the small
    demo map (73% incl) — display share is workload-dependent; use CaptureGui + -Trace for
    graphics-code hotspots, `Times` mode for micro-benchmarks. Details: [rendering](rendering.md).
11. **Savegame load — `karte_t::load` (simworld.cc) + loadsave/io layers.** ~78 s to the load
    plateau for this fixture [EXECUTION-VERIFIED:2026-09-10]; dominates server rotations and
    client joins. Profile with `-TracePhase Load` (zstd caveat above).

Cross-cutting rules that protect these paths: Simutrans `tpl/`/`utils/` containers instead of std
(profiled faster for these workloads) [project-notes](project-notes.md); plain integers over floats
in sync-critical code (also faster) [project-notes](project-notes.md); multi-threaded simulation
must not be perturbed without reading [threading](threading.md) and
[sync-and-determinism](sync-and-determinism.md).

## Open questions

- Exact fixture map dimensions and object counts (convoys, halts, cities, ways) — worth recording
  once extracted for hotspot reasoning; not yet measured.
- Why is `sync_list_t::sync_step` itself (not the objects it steps) 23.7% *self*? Candidates:
  list iteration cost at this object count, cache misses on the node walk, or inlining
  attribution artefacts. Investigate before attempting optimisation.
- The A\* heuristic failures on this map (heur ~10× cost, diagnostics fire continuously at
  -debug ≥ 2): artefact or improvable? Steady-state route search is only ~1% self, so this is a
  post-load-wave problem, not a steady-state one; investigate with a Load-phase or
  immediate-post-load trace before touching the heuristic.
- The ~30 startup tunnel-builder menu errors with pak128.Britain-Ex-0.9.4 on current master:
  possibly pakset/menuconf mismatch; worth investigating later
  [RECOLLECTION:2026-09-09 user: not now].
- A linkable release-build zstd static lib (same toolset as the game, or zstd sources compiled into
  the project) — would remove the debug-zstd decompression bias from load-phase measurements.
- Mapgen: source of the Optimised-debug factory-placement config-sensitivity, and why town
  placement stops at 42/50 on a sparse-enough 1024² map (spacing vs terrain rejection) —
  resolve before adding placement-based test asserts.
