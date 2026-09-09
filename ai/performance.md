---
status: draft
verified: master @ cc1c5858f
---
# Performance & profiling

**Covers:** scripts/run-perf-suite.ps1, the profiling fixture (bb-10-sep-2023.sve + branch pakset
binaries in simutrans/), the MSVC "Profile" configuration, the DEBUG/PROFILE-only benchmark
command-line options (`-until`, `-times`, `-fast-network-sync` in simmain.cc), and the performance
hotspot inventory.

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
  Both wired into the .sln. **The headless build currently crashes on this fixture in server-mode
  simulation — [known-bugs](known-bugs.md) — so server-paced capture runs on the graphical build
  until that is fixed.**
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
verify a full Capture run there after merging [RECOLLECTION:2026-09-09 user decision].

| Mode | Command line core | Measures |
|---|---|---|
| `Load` | `-until 0 -debug 3` | Load cost: pakset + savegame load, at most one sim step, clean exit. Wall time of the whole run (zstd caveat above). (graphical build) |
| `Capture` | `-server <port> -debug 1` | Server workload: load, then run as a server at its normal pace (FIX_RATIO; the savegame's own settings drive frame/step pacing; loopback-only, no clients) for `-WindowSec`, then kill. Default exe: graphical Profile build (headless blocked by the known crash above; `-Headless` selects the headless build). |
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

### Profiler workflows

- **Visual Studio Performance Profiler** (primary): launch `Simutrans-Extended-Profile.exe` (or
  `-Profile-server.exe` once the headless crash is fixed) with the suite's argument line (copy it
  from the script's `$argLine` or a previous summary; the sandbox must exist — run any suite mode
  once to create it), CPU Usage tool; PDBs sit next to the exe. Attach-based sampling also works
  against a Capture/CaptureGui run. VS manages its own ETW session, avoiding the wpr quirks below.
- **ETW / WPR** (scripted): `-Etw` on the script wraps the capture run in `wpr -start CPU
  -filemode` / `wpr -stop <results>\trace.etl`. **Requires the whole script to be run from an
  elevated console** (non-elevated `wpr` cannot enable the policy). Caveat: orchestrated
  `wpr -stop` from nested/elevated child processes proved unreliable on this machine (profile
  stopped, file not written — quoting/COM-thread-mode failures) [EXECUTION-VERIFIED 2026-09-09];
  if `-Etw` reports no trace, stop it by hand from the elevated console or prefer the VS Profiler.
- **Window length:** one to two minutes of paced running on this fixture is enough to identify
  hotspots for most purposes; much longer runs are only needed to observe long-cycle behaviour
  (e.g. a complete path-explorer run), which is rare and better studied on a small map (the CI
  demo fixture) [RECOLLECTION:2026-09-09].
- Comparing runs: same machine, same mode/parameters, same fixture and pakset; compare profiler
  function/module CPU share rather than absolute wall time; wall-time comparisons only between
  clean-exit modes (Load/Times).

## Hotspots

Treat everything under `step()`/`sync_step()` as hot (project-notes); the entries below are the
areas where cost concentrates and evidence exists. **These categories are coarse** — there are
very uneven major hotspots *within* them (especially within the vehicle and city categories); the
fine-grained picture must come from profiler data [RECOLLECTION:2026-09-09]. Magnitudes are
machine- and game-state-specific: profile, do not assume. Domain mechanics:
[simulation-core](simulation-core.md), [routing-and-scheduling](routing-and-scheduling.md),
[vehicles-and-convoys](vehicles-and-convoys.md), [threading](threading.md),
[rendering](rendering.md).

1. **Route search (A\*) — `route_t::intern_calc_route` (dataobj/route.cc).** Convoy pathfinding.
   On the fixture, heuristic-failure diagnostics fire continuously: the printed `heur` values run
   ~10× the achieved `cost`, i.e. searches routinely expand far more nodes than the heuristic
   predicts [execution-verified 2026-09-09]. Probably an artefact of the heuristic interacting
   with this map's design rather than a bug — not established either way
   [RECOLLECTION:2026-09-09]. `max_route_steps` (simuconf, default 1.5M) bounds search memory.
2. **Path explorer — `path_explorer_t` (path_explorer.{h,cc}).** Centralised, *steppable*
   Floyd-Warshall connection search between halts for goods/passengers, budgeted per step via
   `limit_set_t` (rebuild_connexions → filter_eligible → fill_matrix → explore_paths →
   reroute_goods). Definitely a hotspot, but it runs **concurrently** with the rest of the
   simulation and a complete run takes a *long* time: it governs how quickly in-game routes
   update, not framerate or UI responsiveness [RECOLLECTION:2026-09-09].
3. **Route reservation clearing** (signal reservations; → [signals-and-blocks](signals-and-blocks.md)).
   A distinct, significant per-step cost when many convoys run [RECOLLECTION:2026-09-09].
4. **Vehicle physics.** Significant per-step cost: movement physics (fixed-point arithmetic;
   `float32e8_t` for sync-safe decimals) across all vehicles [RECOLLECTION:2026-09-09].
5. **Convoy stepping — `convoi_t` (simconvoi.cc).** Per-convoy state machines (movement, loading,
   readiness, schedule adherence); with thousands of convoys this is broad per-step cost.
   Immediately after loading the fixture there is a mass reroute wave: the first step emits
   ~10⁵ route-search log lines at `-debug ≥ 2` in DEBUG builds [execution-verified 2026-09-09].
6. **Passenger generation — `stadt_t` (simcity.cc).** Very much a hotspot (monthly cadence);
   **city *growth* is not** [RECOLLECTION:2026-09-09]. Building rules and electricity consumption
   tables are per-city configuration, not per-step costs.
7. **Map/ground iteration — `karte_t::sync_step` tile slices, plan/planquadrat_t, grund_t/obj
   lists.** The per-frame walk over ground tiles and their object lists is the raw bandwidth cost
   of huge maps; parts are multi-threaded — the machinery and its fragility: [threading](threading.md).
8. **Halt (stop) processing — `haltestelle_t` (simhalt.cc), `ware_t` (simware.h).** Goods
   boarding/transfer/rerouting at stops; `reroute_goods` is also a path-explorer phase. Not
   recalled as a major hotspot — possibly somewhat hot; unconfirmed
   [RECOLLECTION:2026-09-09 user uncertain]. Verify with profiler data before acting on it.
9. **Display — simview/simgraph pipeline.** The **simulation dominates** on this fixture; display
   is secondary at typical window sizes [RECOLLECTION:2026-09-09]. Measurable via the suite's
   `Times` mode (display_img/view->display/fillbox/text micro-benchmarks). Details:
   [rendering](rendering.md).
10. **Savegame load — `karte_t::load` (simworld.cc) + loadsave/io layers.** On the order of
    1.5 minutes for this fixture on the maintainer's machine [execution-verified 2026-09-09];
    dominates server rotations and client joins. Profile with a `Load`-mode run (zstd caveat
    above).

Cross-cutting rules that protect these paths: Simutrans `tpl/`/`utils/` containers instead of std
(profiled faster for these workloads) [project-notes](project-notes.md); plain integers over floats
in sync-critical code (also faster) [project-notes](project-notes.md); multi-threaded simulation
must not be perturbed without reading [threading](threading.md) and
[sync-and-determinism](sync-and-determinism.md).

## Open questions

- Exact fixture map dimensions and object counts (convoys, halts, cities, ways) — worth recording
  once extracted for hotspot reasoning; not yet measured.
- Relative CPU share of the hotspot areas (and the fine-grained breakdown within the coarse
  categories) — to be filled from the first profiler sessions rather than guessed.
- The A\* heuristic failures on this map: artefact or improvable? Perf-relevant either way
  (searches expand ~10× the predicted nodes); investigate with profiler data before touching the
  heuristic.
- The ~30 startup tunnel-builder menu errors with pak128.Britain-Ex-0.9.4 on current master:
  possibly pakset/menuconf mismatch; worth investigating later
  [RECOLLECTION:2026-09-09 user: not now].
- A linkable release-build zstd static lib (same toolset as the game, or zstd sources compiled into
  the project) — would remove the debug-zstd decompression bias from load-phase measurements.
- Fix the headless server-mode crash ([known-bugs](known-bugs.md)) so the server-paced capture can
  run on the true headless build without display cost.
