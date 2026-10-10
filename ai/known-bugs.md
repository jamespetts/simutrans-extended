---
status: draft
verified: none
---
# Known bugs (open only)

Registry of OPEN bugs. Hard rule: only open bugs are listed — when a bug is fixed, its
entry (and any dedicated bug doc) is DELETED; fixed bugs are never kept or moved within
this doc family (rule in [conventions](conventions.md)). Fix history lives in git (`FIX:` commits).

**Trust level (user decision, 2026-09-06):** this doc is NOT user-reviewed and will not be
interactively reviewed: the entries derive from third-party forum reports rather than the
user's own knowledge, and are too numerous to walk through. Consequences:

- Treat every entry as an unverified lead ([FORUM] provenance per the scan note below),
  never as established fact. The implicit `[CODE]` default for untagged prose in a `draft`
  doc does NOT apply to this doc's entries.
- Do not set `status: reviewed` on this doc. Entries gain confidence individually through
  triage against the code (then re-rank, expand, or delete them).

Structure: simple bugs are one table row in the priority sections below; complex bugs get a
detailed entry (or a dedicated doc, `bug-<slug>.md` in `ai/`) plus a row pointing to it.

## Priority scale

- **0 — critical showstopper (reserved):** e.g. startup crash, savegame corruption, network
  desync in live games, data loss.
- **1 — high:** crash, hang/freeze, desync, savegame load failure, live-server problem,
  money duplication/exploit; or a strong candidate to escalate to 0.
- **2 — medium:** real functional defect with gameplay impact, no crash/data loss.
- **3 — low:** minor or UI/diagnostic issue, easy workaround.
- **4 — very low / backlog:** cosmetic, packaging, likely-stale, or in a deprioritised area.

Rank on discovery; re-rank on triage.

## Forum-scan provenance & caveats

- The catalogue was compiled on 2026-09-06 from a full scan of the forum bug board
  [FORUM:https://forum.simutrans.com/index.php/board,152.0.html] (all 7 listing pages),
  filtered to topics whose most recent post is on or after 1 January 2022. Older open
  reports remain on the board but are excluded here to keep the registry small; add them
  individually if triage makes one relevant.
- Open status = board placement: solved reports are moved to the closed board (73), so
  remaining topics are presumed open. NOT individually verified; verify against code when
  triaging.
- Ranks are provisional: assigned from topic titles and metadata only; thread bodies were
  not read. Descriptions are the topic titles. Re-rank and expand on triage.
- Excluded: guidance/meta stickies, moved-topic redirects, junk posts, OTRP-fork-specific
  reports, patch/pull-request threads, pure question threads.
- Scripting-specific bugs are not tracked here at all: the scripting system is non-working
  in Extended and low priority (→ [scripting-and-tests](scripting-and-tests.md)).

## Detailed entries

### Await gaps around map operations — priority 2

- `karte_t::enlarge_map`'s live path (gui/enlarge_map_frame_t.cc) disables interrupts and awaits
  the path explorer, but does NOT await the convoy or passenger/mail threads before reallocating
  and swapping `plan`/`grid_hgts`/`water_hgts`; convoy workers started at the end of the previous
  step are awaited only at the start of the next step — overlap is not excluded by the code
  [CODE master @ 78a4bb3b9; found in the 2026-09-07 threading mutation inventory].
- `karte_t::update_map` performs no awaits at all (live caller: a tool in simtool.cc); it only
  recalculates images and no concurrent writer of the same image fields was found, but this was
  not exhaustively proven [CODE master @ 78a4bb3b9].
- Unconfirmed whether either gap has ever bitten. Enlarge-map is UI-disabled in network mode, so
  network exposure would need a future non-UI code path
  (→ [sync-and-determinism](sync-and-determinism.md)); single-player exposure is a
  crash/corruption risk. Re-rank 1 if triage shows a sync-critical or live crash path.

### `recheck_road_connexions` flag is written but never read — priority 3

- Every road-building/removal tool path (`wegbauer.cc`, `tunnelbauer.cc`, `brueckenbauer.cc`,
  several sites in `simtool.cc`, one in `simcity.cc`) calls
  `karte_t::set_recheck_road_connexions()`, but nothing ever reads the flag: its only other
  occurrences are the clear in `new_month()` and its (unconditional) rdwr in simworld.cc.
  The intended "re-check private car routes
  promptly after the road network changes" trigger therefore never fires; recorded private
  car route data instead persists until the natural refresh cycle completes
  [CODE private-car-mt-network working tree @ 7d459990a+, found 2026-09-15 during the
  private-car route-recording redesign review; the flag sites are identical on master].
- Consequence: after road construction or removal, stale private-car route entries persist
  until the current refresh cycle finishes (up to a full cycle, or two for complete purging).
  Moving cars detect deleted ways at drive time and fall back to wandering, so the visible
  impact is limited to delayed route availability — hence priority 3.
- To investigate: whether the flag was meant to force `refresh_private_car_routes()` early
  (and whether reviving that is desirable now that refresh cycles run continuously), or
  whether the flag and its rdwr should be removed as dead code. Any rdwr removal is
  version-conditional — see [savegame-versioning](savegame-versioning.md).

### threads = 1 crashes multi-threaded builds (divide by zero) — priority 2

- With simuconf `threads = 1` on a MULTI_THREAD build, `karte_t::get_parallel_operations()`
  (simworld.cc) returns `env_t::num_threads - 1` = 0 (on a network server, load forces
  `parallel_operations` to 0, so the num_threads-derived value is used), but `init_threads()`
  still creates `po + 1` = 1 worker per subsystem. Workers then divide by zero:
  `step_passengers_and_mail_threaded` (`next_step_passenger / get_parallel_operations()`) and
  `unreserve_route_threaded` (`max_count / get_parallel_operations()`) [CODE ex-15 @ 6a22b7388;
  same sites present on master @ 87948985b].
- Reproduced on ex-15 Profile|x64: process crash 0xC0000094 (integer divide by zero) in
  `unreserve_route_threaded` during early simulation of the demo fixture as a loopback server
  [EXECUTION-VERIFIED:2026-09-13 via crash-backtrace.log].
- The identical division sites exist on master (simworld.cc) — presumably affected there too
  [UNVERIFIED by execution on master].
- Ranked 2 rather than the scale default of 1 for crashes: the trigger requires the non-default
  `threads = 1` setting on a multi-threaded build (default 4; non-MT builds force 1 but take the
  `#ifndef MULTI_THREAD` path and are unaffected). Re-rank to 1 if threads=1 on MT builds is
  considered a supported configuration.

### Halt connexions skipped on load when goods categories change — priority 2

- `haltestelle_t::rdwr` (simhalt.cc) sets `path_explorer_t::set_must_refresh_on_loading()`
  when the saved `iteration_limit` differs from the current pakset (simhalt.cc:4689-4694).
  The comment at `:4691-4692` states the data must still be read to advance the file
  position, but the loading branch at `:4757-4788` skips `rdwr_long(connexions_map_count)`
  and all entry reads when the flag is set [CODE master @ 0e5d0be48].
- Consequence: file position desynchronises for all data after halts (convoys, players,
  finance history, path explorer, private-car queue) whenever the pakset's goods
  categories or classes changed since saving. Symmetric across peers with identical
  file and pakset, so not a network join vector; single-player load corruption.
  Recorded per user instruction 2026-09-30 for a separate fix session.

### Server ignores nettool shutdown for 30+ minutes on the gargantuan fixture — priority 2

- Loading bb-10-sep-2023.sve (the performance-suite fixture) as a loopback server and issuing an
  authenticated nettool shutdown: the shutdown is *accepted* (nettool exits 0) but the game kept
  simulating for 34+ minutes afterwards (window live, one core pinned) until killed
  [EXECUTION-VERIFIED:2026-09-10]. demo.sve exits cleanly in 15 s under the same procedure.
- Hypothesis: a very long post-load server step (the mass reroute wave after loading this save —
  see [performance](performance.md)) delays the quit check — UNVERIFIED.
- Production relevance: shutdown/save latency on rotation of very large server saves. Also blocks
  any clean-exit automation against this fixture; kill-based capture is unaffected.
- nettool itself works (mingw build, auth + shutdown verified on demo.sve)
  [EXECUTION-VERIFIED:2026-09-10].

### MSVC "single threaded" configurations silently compile multi-threaded — priority 4

- "Release (single threaded)|x64" defines `MULTI_THREAD=0` and "Debug (single threaded new)|x64"
  defines plain `MULTI_THREAD`; all code guards are `#ifdef MULTI_THREAD`, so both silently
  compile the multi-threaded code (Simutrans-Extended.vcxproj) [CODE master @ 78a4bb3b9].
- User decision 2026-09-07: leave unfixed for now — very low priority; fix-or-delete undecided
  [RECOLLECTION:2026-09-07] (→ [threading](threading.md)).

### recalc_transitions climate-byte data race (TSan mapgen) — priority 3

- `karte_t::recalc_transitions_loop` run under `world_xy_loop` reads neighbouring tiles'
  `planquadrat_t::climate_data` byte via `get_climate()` (simplan.h:129, neighbour read at
  simworld.cc:10764) while another slice's worker read-modify-writes the same byte via
  `set_climate_transition_flag()` (simplan.h:147, own-tile write at simworld.cc:10794);
  `grund_t::calc_image()`'s neighbour-climate reads (boden/grund.cc:1217) share the exposure.
  Root cause: climate (bits 0–2), transition flag (bit 3) and corners (bits 4–7) share one
  `uint8` (simplan.h:49), so the row-slice callback writes state other slices read —
  violating the map-loop invariant (→ [threading](threading.md)). Same code on both branches
  [CODE ex-15 @ cafd18e1e; CODE master @ 0e5d0be48].
- Observed as an intermittent TSan-smoke failure (mapgen `small-s5` abort after 3 race
  reports; neighbouring runs with identical code go green — timing-dependent row-boundary
  overlap). Value-benign in practice: the writer preserves the climate bits, per-tile results
  are deterministic, and the race cannot deadlock (no waits, locks, or control-flow dependence
  in the callback); non-sanitizer builds are unaffected. Only the non-blocking TSan job fails.
- Open questions: (1) whether to fix at all — removes UB and CI noise, but the defect is
  benign and the failing gate is non-blocking; (2) if so, how — Option A: serialize the two
  `world_xy_loop(&karte_t::recalc_transitions_loop, 0)` calls (simworld.cc:3139 new-world
  path, simworld.cc:9915 old-save load path; ~5 lines, strictly fewer threaded moving parts)
  vs Option B: two-phase parallel compute/apply (keeps parallelism but is complicated by
  `calc_image()`'s own neighbour reads, which would need snapshot plumbing or a serial apply).

### Way maintenance accounting defects (GitHub issue 689 triage) — priority 1

- Symptom: [GitHub issue 689](https://github.com/jamespetts/simutrans-extended/issues/689) reports billion-scale "Infrastructure maintenance" bills (e.g. $6.2bn in year 2000) after using intercity roads, bankrupting companies. Intercity roads become player-owned when upgraded [CODE ex-15 @ 0f0ba6efa: bauer/wegbauer.cc:2757-2759].
- [EXECUTION-VERIFIED:2026-10-03] on ex-15 @ 0f0ba6efa with TEST-only logging (reverted afterwards): loading bb6-apr-2010.sve and simulating to the next month boundary shows monthly infrastructure deductions of $45M–$4.2B across established players, drawn from `maintenance[]` accumulators of 23M–2.1B base units (e.g. player 7: 2149938790 base → 419324061601 cents deducted; public player: 623612721 → 121629425103), with balances down to -932283696294716317 cents. Renewal events in the same run bill $9k–$48k per tile (e.g. `city_road→city_road-sma price=910312`, `TramTrack-92lb→TramTrack-98lb price=1456500`): the renewal PRICE formula is not the billion-scale source. Load-time `finish_rd` multiplicity measured 4598 calls over 4599 ways on demo.sve (1.0x): no load multiplication. Deduction arithmetic is single-inflation (deduction/base ≈ 195 = month-scale × general index).
- Vehicle fixed-cost sign wrap (`-get_fixed_cost(welt)` on `uint32`, vehicle/vehicle.cc) was fixed separately; it inflated the monthly maintenance accumulator by ~2^32 base units per vehicle removal/mothball in a running game (cleared by save/load) [EXECUTION-VERIFIED:2026-10-10 user-confirmed fix]. Whether it also explains issue 689 is OPEN QUESTION: the 2026-10-03 measurement above was taken straight after a savegame load, where that wrap cannot have acted. The remaining way-accounting defects below are each bounded and small, so the cause of load-time 2.1e9-scale accumulators is still unidentified.
- Contributing small leaks, code defects [CODE ex-15 @ 0f0ba6efa]: same-owner road upgrade double-counts the new maintenance (`set_desc` at boden/wege/weg.cc:170-182, then `finish_rd` at bauer/wegbauer.cc:2770); city adoption drops ownership without removing the builder's maintenance (bauer/wegbauer.cc:2751-2754); track upgrades change ownership with no maintenance transfer at all (bauer/wegbauer.cc:2918-2934, no `finish_rd`); renewal bills a price computed before `replacement_way` is finalised (boden/wege/weg.cc:1447 vs 1458/1465/1471, billed :1478).
- Ruled out: renewal price formula, `finish_rd` load multiplicity, tolls (separate `ATV_TOLL_*` category, simconvoi.cc:852-877).
- To close this entry, the reporter's savegame and binary version are needed. Related evidence: demo.sve's public balance (-$7.8B) is ~1000x inconsistent with its recent interest/history flows, pointing to an uncategorised historical lump or a historical-build artefact.

## P0 — critical showstoppers

None assigned. Desync/crash entries in P1 are candidates for escalation to 0 if triage
confirms they affect current builds in live games.

## P1 — high

| Forum report | Last active | Notes |
|---|---|---|
| First-join desync then clean rejoin (not a forum report: investigation 2026-09-30; see also 22203 below) | — | detailed investigation → [bug-first-join-desync](bug-first-join-desync.md); isolated to single-convoy speed divergence 3 frames after unpause; cause not yet isolated |
| ["Lost synchronisation with server" report thread](https://forum.simutrans.com/index.php/topic,20355.0.html) | 2024 | sticky umbrella thread for desync reports; triage individual cases |
| ["Wrong theme loaded" crash on start](https://forum.simutrans.com/index.php/topic,24061.0.html) | 2026 | startup crash; candidate 0 if reproducible on current builds; see also 21907 |
| [Reproducible crash when deleting road stop](https://forum.simutrans.com/index.php/topic,23834.0.html) | 2026 | reported reproducible |
| [Crash while modifying line](https://forum.simutrans.com/index.php/topic,23706.0.html) | 2026 | |
| [Rotating the map deletes every industry connection](https://forum.simutrans.com/index.php/topic,23774.0.html) | 2026 | data loss; map-rotation family (see also 20569, 20478 — pre-2022, on board) |
| [Corrupted save game](https://forum.simutrans.com/index.php/topic,23613.0.html) | 2025 | candidate 0 if reproducible |
| [B-B crashes when two people are chatting simultaneously](https://forum.simutrans.com/index.php/topic,23625.0.html) | 2025 | live Bridgewater-Brunel server crash; candidate 0 if still occurring |
| [Fails to run with 'No fonts found!' error](https://forum.simutrans.com/index.php/topic,23403.0.html) | 2025 | startup failure; platform scope unverified |
| [All industries lose connections without warning, usually a crash after](https://forum.simutrans.com/index.php/topic,22871.0.html) | 2024 | data loss + crash |
| [Strange behaviour possibly causing desync](https://forum.simutrans.com/index.php/topic,22405.0.html) | 2024 | desync |
| [[734f8e3] Desync immediately first time try to join the server](https://forum.simutrans.com/index.php/topic,22203.0.html) | 2024 | desync |
| [Thread deadlocks](https://forum.simutrans.com/index.php/topic,23021.0.html) | 2024 | threading family; three mechanisms eliminated since: load-time races corrupting private-car barrier accounting (load-race fix), convoy workers racing the sync step (map-reader hardening 2026-09-13), and unrestorable private-car counters from a corrupt save deadlocking the first step's await (load-time validation [CODE master @ 1b0aa7159]) — if deadlocks persist on the server, the barrier-accounting fragility (→ [threading](threading.md) known problems) is the remaining documented suspect; the corrupt-counter writer fault (wild write vs external damage) is deferred to ASan |
| [Game crashes when trying to upgrade Merchant Navy class through replace function](https://forum.simutrans.com/index.php/topic,22385.0.html) | 2024 | |
| [Listserver unavailability causes online game freezes](https://forum.simutrans.com/index.php/topic,22278.0.html) | 2023 | external-service dependency |
| [[ex-15] Crash when loading saved game saved with ex-15 branch](https://forum.simutrans.com/index.php/topic,22201.0.html) | 2023 | ex-15 branch |
| [Potential crash when editing line while line management window is open](https://forum.simutrans.com/index.php/topic,21917.0.html) | 2022 | |
| [Intermittent hangup when clicking on minimap](https://forum.simutrans.com/index.php/topic,21927.0.html) | 2022 | |
| [Crashes when deleting dead-end road](https://forum.simutrans.com/index.php/topic,22037.0.html) | 2022 | |
| ["Wrong theme loaded" crash at startup](https://forum.simutrans.com/index.php/topic,21907.0.html) | 2022 | likely same family as 24061 |
| [[ex-15] Hovering over the "move signals" button crashes the game](https://forum.simutrans.com/index.php/topic,21714.0.html) | 2022 | ex-15 branch |

| [[assert] factorylist_stats_t.cc assert(max_capacity>0)](https://forum.simutrans.com/index.php/topic,21535.0.html) | 2022 | |
| [Crashes related to road vehicle routing](https://forum.simutrans.com/index.php/topic,21491.0.html) | 2022 | |
| Industry-generation rework: non-terminating infill sweeps (not a forum report: code inspection) | — | rework branch only; `while (fails < 3)` with no reset on success in `karte_t::new_month`, `stadt_t::check_bau_factory` and `karte_t::init` — hang/freeze inside the synced step, uncapped by density; [CODE] → [bug-industry-generation](bug-industry-generation.md) |
| Industry-generation rework: unguarded divisions (not a forum report: code inspection) | — | rework branch only; crash. Two families: the apportionment helpers (`adjust_input_consumption`, `adjust_output_production`, `increase_industry_density`) and `karte_t::recalc_idp` at save load — the latter's zero-consumption case is made more likely by the rework's own switch to bottleneck-adjusted global production; [CODE] → [bug-industry-generation](bug-industry-generation.md) |
| Industry-generation rework: unmemoised mutual recursion, no cycle guard (not a forum report: code inspection) | — | rework branch only; `get_global_consumption` / `get_global_production` / `adjust_output_production` / `adjust_input_consumption` recurse with no visited set or depth cap — stack overflow if a goods cycle exists, else combinatorial cost inside the synced step; [CODE] → [bug-industry-generation](bug-industry-generation.md) |
| Industry-generation rework: save-load density basis wrong for its own version series (not a forum report: code inspection) | — | rework branch only; the legacy Extended-series IDP conversion branch is unreachable, and with no `EX_SAVE_MINOR` bump base-branch saves are indistinguishable from rework saves so they skip conversion entirely — wrong density state, sustained runaway infill. AGENTS.md rule 5 applies; [CODE] → [bug-industry-generation](bug-industry-generation.md) |

| [ex-15] billion-scale infrastructure maintenance billing (not a forum report: GitHub issue 689 triage, code + execution 2026-10-03) | — | detailed entry above; runtime vehicle-removal wrap fixed; load-time 2.1e9-scale accumulators and small way leaks remain open |

## P2 — medium

| Forum report | Last active | Notes |
|---|---|---|
| Halt connexions skipped on load when goods categories change (not a forum report: code inspection 2026-09-30) | — | detailed entry above; load corruption when pakset categories change; symmetric, not a join vector |
| threads = 1 crashes multi-threaded builds (divide by zero) (not a forum report: found during determinism triage 2026-09-13) | — | detailed entry above; both branches; non-default config only |
| Industry-generation rework: four functional/numeric defects (not a forum report: code inspection) | — | rework branch only; [CODE] → [bug-industry-generation](bug-industry-generation.md). Unsigned wrap in `karte_t::recalc_idp` target density → runaway growth; `adjust_input_consumption` returns 0 for a consumer-less manufacturer so partially-supplied stranded manufacturers are invisible to the infill (defeats the rework's own purpose); 32-bit overflow in the oversupplied-goods weight; `find_valid_factory_pos` early return leaves `rotation` indeterminate |
| Industry density cap fails open when actual exceeds target (not a forum report: code inspection) | — | BOTH branches, pre-existing; unsigned subtraction `get_target_industry_density() - get_actual_industry_density()` wraps in `factory_builder_t::increase_industry_density`, so `do_not_add_beyond_target_density` stops bounding; [CODE] → [bug-industry-generation](bug-industry-generation.md) |
| [Bug with replacing signals](https://forum.simutrans.com/index.php/topic,23958.0.html) | 2026 | |
| [48,000 jobs and no production](https://forum.simutrans.com/index.php/topic,23771.0.html) | 2026 | industry simulation |
| ["Passengers intended for a building that has been deleted" warning](https://forum.simutrans.com/index.php/topic,23862.0.html) | 2026 | |
| [No signal at station leads to 1km/h trains](https://forum.simutrans.com/index.php/topic,23852.0.html) | 2026 | |
| [Pax stay on train](https://forum.simutrans.com/index.php/topic,23727.0.html) | 2025 | |
| [Industries don't consume electricity](https://forum.simutrans.com/index.php/topic,23649.0.html) | 2025 | |
| [Island town spreading to far away shore](https://forum.simutrans.com/index.php/topic,23614.0.html) | 2025 | town growth |
| [Underground waterway tunnels](https://forum.simutrans.com/index.php/topic,23481.0.html) | 2025 | |
| [City growth behaving erratically with altered settings](https://forum.simutrans.com/index.php/topic,23419.0.html) | 2025 | |
| [After demolishing underground signal cabins, you can't restore the land](https://forum.simutrans.com/index.php/topic,23199.0.html) | 2024 | |
| [Zero goods in Stops list](https://forum.simutrans.com/index.php/topic,23082.0.html) | 2024 | |
| [2 speed limit bugs: mothballed roads or railways; and fords](https://forum.simutrans.com/index.php/topic,22440.0.html) | 2024 | |
| [Feedback on "Upgrade" fix](https://forum.simutrans.com/index.php/topic,22841.0.html) | 2024 | verify what residual defect remains |
| [[pak192.comic.ext] Trains running at 1 km/h even in track circuit block mode](https://forum.simutrans.com/index.php/topic,22155.0.html) | 2024 | pakset context; may be code-level |
| [Unexpected/incorrect wait times at airports](https://forum.simutrans.com/index.php/topic,22448.0.html) | 2023 | |
| [Pricing is confused](https://forum.simutrans.com/index.php/topic,22442.0.html) | 2023 | |
| [Erratic behavior with "cannot delete public way without diversionary route"](https://forum.simutrans.com/index.php/topic,22441.0.html) | 2023 | |
| [Rail vehicles in drive-by-sight reserve junctions unnecessarily far in advance](https://forum.simutrans.com/index.php/topic,22312.0.html) | 2023 | |
| [Roads cannot be autobuilt up elevated slopes](https://forum.simutrans.com/index.php/topic,22417.0.html) | 2023 | |
| [Coupling constraint issues with locomotives](https://forum.simutrans.com/index.php/topic,22334.0.html) | 2023 | |
| [Rivers with public right of way cannot be upgraded to canals](https://forum.simutrans.com/index.php/topic,21005.0.html) | 2023 | |
| [Can't build underground signalboxes](https://forum.simutrans.com/index.php/topic,19461.0.html) | 2023 | |
| [Log reports division by zero errors](https://forum.simutrans.com/index.php/topic,22321.0.html) | 2023 | |
| [Reloading changes convoy reversing/shunting behaviour](https://forum.simutrans.com/index.php/topic,22310.0.html) | 2023 | save/load consistency |
| [Maximum in transit can be zero](https://forum.simutrans.com/index.php/topic,19947.0.html) | 2023 | |
| [Clicking "replace" in the vehicle window immediately deducts cost of new vehicle](https://forum.simutrans.com/index.php/topic,22206.0.html) | 2023 | |
| [Building and removing crossing with tram track results in different maintenance](https://forum.simutrans.com/index.php/topic,22073.0.html) | 2023 | |
| [Vehicles not following waypoints in order](https://forum.simutrans.com/index.php/topic,22126.0.html) | 2022 | |
| [Can't delete stop if foreign tram tracks present](https://forum.simutrans.com/index.php/topic,21711.0.html) | 2022 | |
| [Electricity networks are buggy](https://forum.simutrans.com/index.php/topic,21713.0.html) | 2022 | |
| [Building convoys in depot shows/hides vehicles randomly](https://forum.simutrans.com/index.php/topic,21637.0.html) | 2022 | |
| ["End of signalling" signs cause 1km/h and bugged drive-by-sight mechanics](https://forum.simutrans.com/index.php/topic,21693.0.html) | 2022 | |
| [Signal reservations not clearing](https://forum.simutrans.com/index.php/topic,21797.0.html) | 2022 | |
| [Fruits refuse initially to fully load on convoy from orchard](https://forum.simutrans.com/index.php/topic,21862.0.html) | 2022 | |
| [Max Intransit off by consumption factor](https://forum.simutrans.com/index.php/topic,21829.0.html) | 2022 | |
| [Some small producers fail to deliver goods to the stations](https://forum.simutrans.com/index.php/topic,21671.0.html) | 2022 | |
| [New market without any suppliers](https://forum.simutrans.com/index.php/topic,21815.0.html) | 2022 | |
| [Fishing port and docks do not interact as expected](https://forum.simutrans.com/index.php/topic,21796.0.html) | 2022 | |
| [Can make signal box partly overlap rails](https://forum.simutrans.com/index.php/topic,19027.0.html) | 2022 | |
| [Docking at wrong port](https://forum.simutrans.com/index.php/topic,21724.0.html) | 2022 | |
| [Trains ignoring or forgetting about token block/single staff signalling](https://forum.simutrans.com/index.php/topic,21689.0.html) | 2022 | |
| [The market has too much power](https://forum.simutrans.com/index.php/topic,21692.0.html) | 2022 | economy simulation |
| [Calculation of "max. comfortable journey time" when comfort is 240 or more](https://forum.simutrans.com/index.php/topic,21697.0.html) | 2022 | |
| [Bridges and roads autobuild inappropriately](https://forum.simutrans.com/index.php/topic,21675.0.html) | 2022 | |
| [Builder's Yards and Quarries](https://forum.simutrans.com/index.php/topic,21666.0.html) | 2022 | title vague; verify content on triage |
| [Makeobj Extended doesn't run](https://forum.simutrans.com/index.php/topic,21635.0.html) | 2022 | makeobj tool |
| [Line colours depend on SDL2 (was: command line server build failed)](https://forum.simutrans.com/index.php/topic,21452.0.html) | 2022 | two issues in one thread |
| [Discrepancies in calc_adjusted_monthly_figure](https://forum.simutrans.com/index.php/topic,21443.0.html) | 2022 | statistics |
| [Multitile signalbox refuses to be built on artificial flat slopes](https://forum.simutrans.com/index.php/topic,21429.0.html) | 2022 | |
| [Overtaking algorithm is broken at a railroad crossing](https://forum.simutrans.com/index.php/topic,21030.0.html) | 2022 | |
| [Choose Sign sends train to occupied platform (reproducible)](https://forum.simutrans.com/index.php/topic,21151.0.html) | 2022 | reported reproducible |

## P3 — low

| Forum report | Last active | Notes |
|---|---|---|
| recalc_transitions climate-byte data race (not a forum report: TSan CI triage) | — | detailed entry above; both branches; intermittent non-blocking-TSan failure only, value-benign, cannot deadlock; whether/how to fix open |
| MSVC "single threaded" configurations compile multi-threaded code (not a forum report) | — | found by code inspection 2026-09-06 [CODE master @ 78a4bb3b9]: Simutrans-Extended.vcxproj "Release (single threaded)\|x64" defines `MULTI_THREAD=0`, "Debug (single threaded new)\|x64" defines plain `MULTI_THREAD`; all guards are `#ifdef`, so both build MT code — misleads debugging/bisection. Details → [threading](threading.md) |
| Industry-generation rework: persisted density discarded + contradictory overload docs (not a forum report: code inspection) | — | rework branch only; `karte_t::load` reads `actual_industry_density` then recomputes it unconditionally (and duplicates the call under a condition that can never add anything), so the persisted datum is never honoured; `factory_builder_t::adjust_input_consumption(fab, good)` returns the amount NOT used while its declaration documents the amount used; [CODE] → [bug-industry-generation](bug-industry-generation.md) |
| [UI: can't jump to stop from Stops list](https://forum.simutrans.com/index.php/topic,23391.0.html) | 2025 | |
| [Minimum loading percentage display in schedule UI](https://forum.simutrans.com/index.php/topic,22781.0.html) | 2024 | |
| [Bug in Listbox when changing schedules](https://forum.simutrans.com/index.php/topic,22202.0.html) | 2023 | |
| [Minimap panning movement is weird](https://forum.simutrans.com/index.php/topic,21688.0.html) | 2022 | |
| [Icons in convoy list are gone](https://forum.simutrans.com/index.php/topic,21690.0.html) | 2022 | |
| [No "No Route" warnings if problem with low bridges](https://forum.simutrans.com/index.php/topic,21725.0.html) | 2022 | missing diagnostic |
| [Illegal teleport pedestrian](https://forum.simutrans.com/index.php/topic,19106.0.html) | 2022 | |
| [Available Vehicles List does not correctly filter vehicles by traction type](https://forum.simutrans.com/index.php/topic,21559.0.html) | 2022 | |
| [Unable to compile Extended in Arch's MinGW cross-compilation toolchain](https://forum.simutrans.com/index.php/topic,21401.0.html) | 2022 | likely stale — CI MinGW builds pass; verify |
| [The lines of the combobox collapse and overlap in one line](https://forum.simutrans.com/index.php/topic,21776.0.html) | 2022 | |
| [Line Management Charts — wrong maximum numbers](https://forum.simutrans.com/index.php/topic,21691.0.html) | 2022 | |

| Dead `current_way_better_cost` + inverted condition in `weg_t::renew` public-road fallback (not a forum report: code inspection 2026-10-03) | — | boden/wege/weg.cc:1492-1503: `current_way_better_cost` is computed but never used, and `default_way_is_better_than_current_way &= !no_worse_stats` clears the flag exactly when the default road is genuinely better, so owned public-right-of-way tiles never upgrade to the default road on that path [CODE ex-15 @ 0f0ba6efa] |
| Upgrade-cost display monthly-scales one-off costs (not a forum report: code inspection 2026-10-03) | — | gui/way_info.cc:579,585 applies `calc_adjusted_monthly_figure` to one-off construction costs while the booking paths do not monthly-scale them, so displayed upgrade costs mismatch actual charges [CODE ex-15 @ 0f0ba6efa] |

## P4 — very low / backlog

| Forum report | Last active | Notes |
|---|---|---|
| [Heavy steel elevated support icon flickers on zooming](https://forum.simutrans.com/index.php/topic,22186.0.html) | 2023 | |
| [Duplicate drawing in the depot dialog](https://forum.simutrans.com/index.php/topic,21214.0.html) | 2022 | |
| [Date format setting is not reflected in some dialogs](https://forum.simutrans.com/index.php/topic,21502.0.html) | 2022 | |
| [Can break the slope (shore) texture](https://forum.simutrans.com/index.php/topic,21425.0.html) | 2022 | |
| [Erroneous commit tag in game window](https://forum.simutrans.com/index.php/topic,21100.0.html) | 2022 | |
| [Padded city chart numbers (jobs/visitor demand)](https://forum.simutrans.com/index.php/topic,21236.0.html) | 2022 | |
| Dead/broken container utilities in tpl/ (not a forum report: code inspection 2026-09-07) | — | koord_pair_hashtable_tpl: `comp()` returns bool (violates the hashtable diff contract → wrong "absent" results) and its companion iterator class cannot compile; quickstone_tpl `(T*,bool)` ctor scan loop increments instead of decrements (both unused); freelist_tpl/freelist_iter_tpl unused, iter variant uncompilable. Fix-or-delete undecided (→ [utilities](utilities.md)) [CODE master @ d91fc8fce] |
| Dead utils/ files (not a forum report: code inspection 2026-09-07) | — | notification.h (zero includes), snprintf.h (uncompilable PHP-derived stub), dbg_weightmap.* (never-defined DEBUG_WEIGHTMAPS gate, .cc unbuilt), dumb-log.cc (test-only log_t impl), omzet2.c (legacy font converter, unbuilt). User decision 2026-09-07: delete when convenient [RECOLLECTION:2026-09-07]. Note: dumb-log.cc is pulled in by tpl/test_piecewise_linear_tpl.cc (also unbuilt) — deleting it means deleting or rewiring that test (→ [utilities](utilities.md)) [CODE master @ d91fc8fce] |
| Industry-generation rework: dead code, retained superseded code and diagnostic noise (not a forum report: code inspection) | — | rework branch only; `factory_builder_t::is_final_good` has no callers on either branch, `karte_t::init` declares an unused failure counter, superseded blocks are commented out rather than deleted (incl. four density-accounting sites in simfab.cc and the old mapgen label in gui/welt.cc), per-load `DBG_MESSAGE` noise incl. one message reporting an unconditional action as conditional, and only en.tab/fr.tab carry the renamed mapgen label key so other languages fall back to English (→ [translations](translations.md)); [CODE] → [bug-industry-generation](bug-industry-generation.md) |

## Open questions

- Should fixed-bug history be recorded anywhere beyond git `FIX:` commit messages?
  (Current position: nowhere.)
- Which catalogue entries are actually already fixed but unmoved from the board?
  Verify per entry on triage; delete from the registry when confirmed fixed.
- Which P1 desync/crash entries still reproduce on current master/ex-15 builds?
