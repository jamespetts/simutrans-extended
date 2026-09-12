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

### Headless (COLOUR_DEPTH=0) MSVC builds crash in server-mode simulation on the gargantuan fixture — priority 1

- Loading bb-10-sep-2023.sve (the performance-suite fixture) and running it as a server
  (`-server`, loopback-only, no clients, `pause_server_no_clients = 0`) crashes with 0xC0000005,
  faulting module ntdll.dll (heap-corruption pattern), roughly 25–30 s after load completes
  ("Running world" reached; FIX_RATIO timers running) [EXECUTION-VERIFIED 2026-09-09].
- Reproduced with both the "Profile (server)|x64" build and the maintainer's
  "Debug (non-graphical server)|x64" build (rebuilt from master @ cc1c5858f) — independent of
  the new configuration [EXECUTION-VERIFIED 2026-09-09].
- NOT reproduced by the graphical "Profile|x64" build in the same server mode (survived 300 s+),
  nor by client-mode fast-forward runs (120 s windows) — headless-specific, early server sim,
  this save [EXECUTION-VERIFIED 2026-09-09].
- The suite first misclassified it as a load crash: the load completes, and the crash is
  log-invisible in Profile builds (DBG macros compiled out) [EXECUTION-VERIFIED 2026-09-09].
- Scope: the headless build works when compiled with GCC (production server and local GCC
  builds), so this is presumably an MSVC/Windows-specific defect, not a code-level deterministic
  bug on all platforms [RECOLLECTION:2026-09-09 user statement]. Candidate classes: latent
  undefined behaviour that MSVC's runtime/heap validation surfaces, or an MSVC codegen/packing
  difference. Mixed-CRT zstd linking is ruled out: the crashing "Debug (non-graphical server)"
  build is /MTd, matching the zstd lib's CRT. The `karte_t::load`/`init_threads` race family it
  was observed alongside has since been fixed; whether that family was the cause is untested —
  re-run the headless capture to find out [UNVERIFIED].
- Blocks the headless capture profile of the performance suite (→ [performance](performance.md));
  workaround: server-paced capture on the graphical Profile build (display cost included).

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

### Convoy workers read the map during the main-thread sync step (crash family) — priority 1

- Convoy route-finding workers (`convoi_t::threaded_step`, route finding only) run from the end
  of `karte_t::step` until `await_convoy_threads` part-way through the next step. In that window
  the main thread mutates the data they read: vehicle hops tear tile object lists
  (`objlist_t::add/remove/set_capacity` in `sync_step`), `karte_t::new_month` rewrites convoy
  statistics at the top of the next step (before the await), and objects are deleted while
  workers still hold pointers to them (freelist `putback_node` races) [CODE; TSan CI run on
  ae6293989, artifact of run 34630028522: ~250 of 266 warnings, dominated by
  `objlist_t::remove`/`intern_insert_at`/`objlist_t::bei`/`grund_t::set_flag`/`obj_t::set_flag`].
- Crash mechanism: torn objlist reads by route finding. Reproduced locally on the demo fixture
  (intermittent, ~1 in 6 network-harness runs, build of ee26ea8bf+df03b1b60): unhandled C++
  exception from `__RTDynamicCast` under `grund_t::get_depot` ← `rail_vehicle_t::check_next_tile`
  ← `route_t::intern_calc_route` ← `convoi_t::threaded_step` on a convoy worker; and separately
  an access violation at `grund_t::get_weg` (grund.h:656) from the same path (MSVC Debug builds
  have ASan enabled) [EXECUTION-VERIFIED:2026-09-12].
- This is forum topic 20994 (2022 headless-server objlist race) and the "convoy threads run
  across the sync step" code-stated caveat; the load-race fix did NOT cover it.
- Working hypothesis [UNVERIFIED]: the CI TSan run's post-save SIGABRT (exit 134, zero
  diagnostics, after a complete valid final.sve) is teardown corruption from this family;
  re-check after the fix.
- User decision 2026-09-12: do NOT disable MULTI_THREAD_CONVOYS (its measured share is ~1.7% of
  frame CPU — [threading](threading.md) performance attribution — but the feature is valued);
  the fix needs careful design and is deferred to a dedicated planning session. Options sketched:
  await at the top of step (kills only the `new_month` overlap); harden objlist/tile reads
  against sync_step churn (large); snapshot/quiescent-point route finding (large).

### Unbound halt handles in planquadrat haltlists crash early simulation (ex-15) — priority 1

- Loading the bb-10-sep-2023 fixture on ex-15 (pak128.Britain-Ex) crashes with 0xC0000005
  reading 0x5F4 within ~1-2 minutes of simulation start, at haltlist consumers:
  `haltestelle_t::get_destination_halts_of_ware` (simhalt.cc), `karte_t::get_nearby_halts_of_tiles`
  and `karte_t::generate_passengers_or_mail` (simworld.cc) [EXECUTION-VERIFIED:2026-09-11, via the
  crash-backtrace handler].
- Mechanism: a `halthandle_t` (quickstone) in a tile's haltlist resolves to NULL; `enables` is at
  offset 0x5F4 in haltestelle_t. No halt destruction occurs in failing runs (destructor
  instrumentation never fired) and failure is non-deterministic across identical runs —
  consistent with a load-time/threading race corrupting haltlists, most likely the
  `karte_t::load` / `init_threads` race family. That family is now fixed on master (workers are
  created only at the end of `karte_t::load`); the master→ex-15 merge carrying the fix has
  happened (ex-15 @ 9d8dde74c) — re-test whether this crash still reproduces; if it does, the
  cause is elsewhere. Whether this shares a root with the headless MSVC server crash (ntdll
  heap-corruption signature differs) is UNVERIFIED.
- A guarded ex-15 build (is_bound() skip + warning at the haltlist consumer sites) survives 8+
  minutes with zero guard hits in some runs — i.e. the corruption appears only sometimes.
  Guards were needed for the 2026-09-11 ex-15 profiling verification; the root-cause race fix
  has now landed on master (pending merge), so the guards are a purely defensive layer.
- Never reproduced on master graphical builds (windows up to 300 s+) — but the race family is
  branch-independent, so master exposure is plausible [UNVERIFIED].

### Factory intransit gate reads in-flight path-explorer state — priority 2

- `fabrik_t::calc_max_intransit_percentages` (simfab.cc:4418; from `fabrik_t::new_month` ←
  `karte_t::new_month`, top of `karte_t::step`) reads
  `path_explorer_t::get_current_compartment_category()` and then `get_paths_available(...)` —
  the path explorer's live progress marker and compartment state — while the path explorer
  thread is mid-step (`await_path_explorer` comes later in step) [CODE; TSan CI run on
  ae6293989: race at path_explorer.h:538 in `get_current_compartment_category`].
- The gate guards a saved factory parameter (`max_transit`), and its result at a month boundary
  depends on thread scheduling — a rare desync vector, not just UB.
- Deferred to the threading-choreography planning session (same conversation as the convoy
  window above). Candidate fixes: await the path explorer before `new_month`; make the gate
  timing-independent; atomic marker.

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

## P0 — critical showstoppers

None assigned. Desync/crash entries in P1 are candidates for escalation to 0 if triage
confirms they affect current builds in live games.

## P1 — high

| Forum report | Last active | Notes |
|---|---|---|
| Headless MSVC builds crash in server-mode sim on the bb-10-sep-2023.sve fixture (not a forum report: performance-suite discovery 2026-09-09) | — | detailed entry above |
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
| [Thread deadlocks](https://forum.simutrans.com/index.php/topic,23021.0.html) | 2024 | threading family; one plausible mechanism (load-time races corrupting private-car barrier accounting, incl. the unlocked suspend-flag read deciding between one and two barrier waits) was eliminated with the load-race fix — if deadlocks persist on the server, next suspect is convoy threads running across sync_step |
| [Game crashes when trying to upgrade Merchant Navy class through replace function](https://forum.simutrans.com/index.php/topic,22385.0.html) | 2024 | |
| [Listserver unavailability causes online game freezes](https://forum.simutrans.com/index.php/topic,22278.0.html) | 2023 | external-service dependency |
| [[ex-15] Crash when loading saved game saved with ex-15 branch](https://forum.simutrans.com/index.php/topic,22201.0.html) | 2023 | ex-15 branch |
| [Potential crash when editing line while line management window is open](https://forum.simutrans.com/index.php/topic,21917.0.html) | 2022 | |
| [Intermittent hangup when clicking on minimap](https://forum.simutrans.com/index.php/topic,21927.0.html) | 2022 | |
| [Crashes when deleting dead-end road](https://forum.simutrans.com/index.php/topic,22037.0.html) | 2022 | |
| ["Wrong theme loaded" crash at startup](https://forum.simutrans.com/index.php/topic,21907.0.html) | 2022 | likely same family as 24061 |
| [[ex-15] Hovering over the "move signals" button crashes the game](https://forum.simutrans.com/index.php/topic,21714.0.html) | 2022 | ex-15 branch |
| [Data race in objlist_t::remove when running headless server](https://forum.simutrans.com/index.php/topic,20994.0.html) | 2022 | convoy threads run across the sync step — detailed entry above; TSan-verified 2026-09-12; fix deferred to a planning session (user decision: keep the feature) |
| [[assert] factorylist_stats_t.cc assert(max_capacity>0)](https://forum.simutrans.com/index.php/topic,21535.0.html) | 2022 | |
| [Crashes related to road vehicle routing](https://forum.simutrans.com/index.php/topic,21491.0.html) | 2022 | |

## P2 — medium

| Forum report | Last active | Notes |
|---|---|---|
| Factory intransit gate reads in-flight path-explorer state (not a forum report: TSan CI finding 2026-09-12) | — | detailed entry above; rare desync vector; deferred to the threading-choreography planning session |
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
| Wear-based running cost never reaches the finances (not a forum report: code inspection 2026-09-07) | — | ex-15 only: `convoi_t::increment_odometer` books the descriptor-level running cost; the sigmoid wear increase (`vehicle_t::get_running_cost`) is computed and displayed but never booked, so usage-based maintenance has no financial effect. Inflation IS accounted for in this path; the defect is solely the omitted wear increase. User-confirmed bug [RECOLLECTION:2026-09-07] [CODE ex-15 @ 91d9b252e]. Detail → [ex-15 economy registry](ex-15/economy-and-vehicles.md) |
| Sigmoid argument unsigned wrap in wear/availability curves (not a forum report: code inspection 2026-09-07) | — | ex-15 only: `vehicle_t::get_running_cost`/`get_availability` compute `sigmoid(100000ll * (km_since_last_overhaul - max_distance_between_overhauls), ...)`, negative throughout the active branch (unsigned wrap); the aircraft counterparts subtract the decay-start value. Affects displayed costs AND real depot time (`maintain()` divides by availability) [CODE ex-15 @ 91d9b252e] |
| `schedule_t::copy_from` leaves stale consist orders and skips table recomputation (not a forum report: code inspection 2026-09-07) | — | ex-15 only: copies `orders` without clearing pre-existing keys and without recomputing the `parse_orders`-derived carried-category/class tables; a convoy adopting a line's schedule can hold stale orders with empty derived tables (routing may treat it as carrying nothing) [CODE ex-15 @ 91d9b252e] |
| `convoi_t::check_pending_updates` depot-entry restore passes wrong arguments (not a forum report: code inspection 2026-09-07) | — | ex-15 only: `schedule_t::insert` is called with `removed_depot_entry.target_id_uncouple` in the target_id_couple parameter position and a bool in the target_id_uncouple position; target_id_couple, target_unique_entry_uncouple and max_speed_kmh are not restored at all — silent schedule-entry data corruption when a convoy adopts line changes [CODE ex-15 @ 91d9b252e] |
| Post-shunting departure skips departure checks (not a forum report: code inspection 2026-09-07) | — | ex-15 only: a convoy completing a consist order transitions SHUNTING → ROUTING_1 → advance_schedule without passing `check_departure`, departing immediately after the shunting delay and ignoring minimum loading and spacing slots. User-confirmed defect [RECOLLECTION:2026-09-07] [CODE ex-15 @ 91d9b252e]. Context → [ex-15 schedule registry](ex-15/schedule-and-consists.md) |

## P3 — low

| Forum report | Last active | Notes |
|---|---|---|
| MSVC "single threaded" configurations compile multi-threaded code (not a forum report) | — | found by code inspection 2026-09-06 [CODE master @ 78a4bb3b9]: Simutrans-Extended.vcxproj "Release (single threaded)\|x64" defines `MULTI_THREAD=0`, "Debug (single threaded new)\|x64" defines plain `MULTI_THREAD`; all guards are `#ifdef`, so both build MT code — misleads debugging/bisection. Details → [threading](threading.md) |
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
| Schedule trigger flag polarity mismatch (not a forum report: code inspection 2026-09-07) | — | ex-15 only: `convoi_t::ziel_erreicht` treats cond_trigger_is_line_or_cnv set=line/unset=convoy, the reverse of the couple/uncouple equivalents (set=convoy); the GUI stores a line ID without setting the flag. Latent until the trigger GUI is wired [CODE ex-15 @ 91d9b252e]. Context → [ex-15 schedule registry](ex-15/schedule-and-consists.md) |
| Urgent maintenance does not force a depot visit (not a forum report: code inspection 2026-09-07) | — | ex-15 only: exceeding 1.5× maintenance_interval_km sets only no_load; the documented intent (vehicle_desc.h comment) is an emergency depot visit wherever the convoy is; user confirms the comment reflects intent [RECOLLECTION:2026-09-07] [CODE ex-15 @ 91d9b252e] |

## P4 — very low / backlog

| Forum report | Last active | Notes |
|---|---|---|
| [Heavy steel elevated support icon flickers on zooming](https://forum.simutrans.com/index.php/topic,22186.0.html) | 2023 | |
| [Duplicate drawing in the depot dialog](https://forum.simutrans.com/index.php/topic,21214.0.html) | 2022 | |
| [Date format setting is not reflected in some dialogs](https://forum.simutrans.com/index.php/topic,21502.0.html) | 2022 | |
| [Can break the slope (shore) texture](https://forum.simutrans.com/index.php/topic,21425.0.html) | 2022 | |
| [Erroneous commit tag in game window](https://forum.simutrans.com/index.php/topic,21100.0.html) | 2022 | |
| [Padded city chart numbers (jobs/visitor demand)](https://forum.simutrans.com/index.php/topic,21236.0.html) | 2022 | |
| `state_names[]` debug array in simconvoi.cc stale (not a forum report: code inspection 2026-09-07) | — | ex-15 only: 25 names for a 30-value states enum; the 15.x convoy states (REPLENISHING/MAINTENANCE/OVERHAUL/AWAITING_TRIGGER/SHUNTING) log wrong names — misleads debugging/logging [CODE ex-15 @ 91d9b252e] |
| Dead/broken container utilities in tpl/ (not a forum report: code inspection 2026-09-07) | — | koord_pair_hashtable_tpl: `comp()` returns bool (violates the hashtable diff contract → wrong "absent" results) and its companion iterator class cannot compile; quickstone_tpl `(T*,bool)` ctor scan loop increments instead of decrements (both unused); freelist_tpl/freelist_iter_tpl unused, iter variant uncompilable. Fix-or-delete undecided (→ [utilities](utilities.md)) [CODE master @ d91fc8fce] |
| Dead utils/ files (not a forum report: code inspection 2026-09-07) | — | notification.h (zero includes), snprintf.h (uncompilable PHP-derived stub), dbg_weightmap.* (never-defined DEBUG_WEIGHTMAPS gate, .cc unbuilt), dumb-log.cc (test-only log_t impl), omzet2.c (legacy font converter, unbuilt). User decision 2026-09-07: delete when convenient [RECOLLECTION:2026-09-07]. Note: dumb-log.cc is pulled in by tpl/test_piecewise_linear_tpl.cc (also unbuilt) — deleting it means deleting or rewiring that test (→ [utilities](utilities.md)) [CODE master @ d91fc8fce] |

## Open questions

- Should fixed-bug history be recorded anywhere beyond git `FIX:` commit messages?
  (Current position: nowhere.)
- Which catalogue entries are actually already fixed but unmoved from the board?
  Verify per entry on triage; delete from the registry when confirmed fixed.
- Which P1 desync/crash entries still reproduce on current master/ex-15 builds?
