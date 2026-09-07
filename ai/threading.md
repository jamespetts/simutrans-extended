---
status: reviewed
verified: master @ 78a4bb3b9
---
# Threading

**Covers:** utils/simthread.*; every thread in the game binary: simulation workers (the
threading regions of simworld.cc/h, `convoi_t::threaded_step` in simconvoi.cc, path_explorer.*,
private-car route searching in dataobj/route.* and route data in boden/wege/weg.*), the map-loop
workers (`karte_t::world_xy_loop`), display helpers (display/simview.cc, per-thread clipping in
simgraph16.cc), save/load I/O (dataobj/loadsave.cc); thread_local state; the lock/barrier
inventory. Determinism rules for threaded simulation work (work-split identity, seeding,
fixed-order merging, checklist coupling) → [sync-and-determinism](sync-and-determinism.md);
never duplicated here. Platform/build topics → [build-and-toolchain](build-and-toolchain.md).

## Rules (non-sync; read first)

1. Non-main threads mutate shared game state only inside explicitly bounded start→await (or
   lifecycle) windows, and every such write is protected either by a named mutex or by barrier
   discipline guaranteeing no main-thread consumer runs concurrently. Track, per thread class,
   WHAT shared state it writes and WHEN it may run (its window boundaries) — the inventory is
   below. Workers additionally compute into per-thread buffers or thread-local structures that
   the main thread merges in fixed order at await points; the network-safety rules for that
   split/merge/seed pattern → [sync-and-determinism](sync-and-determinism.md)
   [CODE master @ 78a4bb3b9; user correction RECOLLECTION:2026-09-07].
2. Non-simulation threads mutate shared state only inside lifecycle windows where the main
   thread is blocked or the simulation is not stepping: `world_xy_loop` callbacks write
   plan/ground/object state during load, rotation and map updates (the main thread participates
   in the loop and blocks); they run under `INTERACTIVE_RANDOM` — `simrand()` is forbidden in
   anything they call. Display helpers write the shared image cache and `grund_t` dirty flags
   inside the display barriers; save/load I/O moves bytes only [CODE master @ 78a4bb3b9].
3. Main-thread code that modifies data a running worker reads must await that worker first
   (`await_convoy_threads` / `await_private_car_threads` / `await_path_explorer` /
   `await_passengers_and_mail_threads`). `karte_t::await_all_threads()` is the full stop used
   before saving (`karte_t::save`) and map rotation (`karte_t::rotate90`); world teardown goes
   through `destroy_threads()` directly. Existing precedents: way/signal modification (weg.cc, roadsign.cc,
   `grund_t::weg_entfernen`), halt/depot/tool changes visible to the path explorer (simhalt.cc,
   simdepot.cc, simtool.cc), GUI reads of route data (gui/settings_stats.cc).
4. Do not invent new thread lifecycles. Simulation workers are created once by
   `karte_t::init_threads()`, park on barriers, are released/awaited by matched barrier waits,
   and are only ever torn down by `karte_t::destroy_threads()`. New threaded simulation work
   must follow the same pattern plus the sync-doc rules.
5. Barriers (`simthread_barrier_*`) synchronise phases; mutexes protect shared aggregates.
   Every participant of a barrier must execute the same number of waits per cycle; the wait
   counts are hand-balanced ("having N of these is intentional" comments) and fragile — see
   Known problems.

## Build configuration

- `MULTI_THREAD`: make builds via config.template (`MULTI_THREAD = 1`); CMake via
  `SIMUTRANS_MULTI_THREAD` (default ON where pthreads is found) defining `MULTI_THREAD=1`; MSVC
  via per-configuration preprocessor definitions in Simutrans-Extended.vcxproj. All code guards
  are `#ifdef MULTI_THREAD` — no value-based `#if` anywhere.
- Feature guards (simworld.h, active only under MULTI_THREAD): `MULTI_THREAD_PASSENGER_GENERATION`,
  `MULTI_THREAD_CONVOYS`, `MULTI_THREAD_PATH_EXPLORER`, `MULTI_THREAD_ROUTE_UNRESERVER`; each can
  be disabled by uncommenting its `FORBID_*` companion at the same place (several further
  commented-out FORBID_* desync-testing switches live there).
- `env_t::num_threads`: simuconf.tab `threads` / command-line `-threads`, clamped 1..`MAX_THREADS`
  (12, simconst.h); default 4 in MT builds; forced 1 in non-MT builds. All peers must run the same
  `threads` value (checklist) → [sync-and-determinism](sync-and-determinism.md).
- Broken MSVC "single threaded" configurations: "Release (single threaded)|x64" defines
  `MULTI_THREAD=0` and "Debug (single threaded new)|x64" defines plain `MULTI_THREAD`; since all
  guards are `#ifdef`, both silently compile the multi-threaded code
  (→ [known-bugs](known-bugs.md)).
- Non-MT MSVC builds: simtypes.h defines `thread_local` away (empty); pre-VS2015 MT builds map it
  to `__declspec(thread)` (utils/simthread.h). makeobj/nettool are single-threaded.

## Primitives (utils/simthread.*)

- Barriers only: `simthread_barrier_t` = native pthread barrier where POSIX provides it
  (`_USE_POSIX_BARRIERS`), else a mutex+condvar fallback (simthread.cc; needed on macOS — the
  header force-defines `_XOPEN_SOURCE 600` on non-Apple POSIX for this). There is no
  thread-spawn helper: every spawn site is a raw `pthread_create`.
- Semaphores (`sem_t`) are used once: x-direction pacing inside `world_xy_loop` (`SYNCX_FLAG`).

## Worker inventory

po = `karte_t::get_parallel_operations()` (own `parallel_operations` when threads are up, or in
network clients which adopt the server's saved value; else `env_t::num_threads - 1`).

### Simulation workers (simworld.cc, created by `karte_t::init_threads`)

Joinable; each loops {barrier wait → work → barrier wait(s)} until `terminating_threads` (set by
`destroy_threads()`, which then trips every barrier once and joins via `clean_threads`). Barrier
participant counts are set in `init_threads` (comments explain the +1/+2 variants). Main-thread
`start_*` helpers release a subsystem with one barrier wait and set its `*_threads_working` flag;
`await_*` helpers match the workers' waits and clear it.

| Worker fn | × | Work | Synchronisation |
|---|---|---|---|
| `check_road_connexions_threaded` | po | city private-car route checks (`stadt_t::check_all_private_car_routes`), dequeued from `cities_awaiting_private_car_route_check` | `private_car_barrier` (po+1); `private_car_route_mutex` (queue, city road connexions, suspend flag) |
| `unreserve_route_threaded` | po+1 | `convoi_t::unreserve_route_range` over slices of the global way list, on demand | `unreserve_route_barrier` (po+2) |
| `step_passengers_and_mail_threaded` | po+1 | passenger/mail generation, quota-split; per-thread seed `setsimrand(325651 + random_counter·thread_number)`; thread numbers 1..po+1 (0 = main thread, which does not generate) | `step_passengers_and_mail_barrier` (po+2); `step_passengers_and_mail_mutex` |
| `step_individual_convoy_threaded` | po | `convoi_t::threaded_step()` — route finding only, only in state `ROUTING_2` (set only by the single-threaded `convoi_t::step()`); strides over `convoys_next_step` | `step_convoys_barrier_internal` (po+1) |
| `step_convoys_threaded` (master) | 1 | fills `convoys_next_step` (backwards convoy order) | `step_convoys_barrier_external` (2: master+main), internal barrier |
| `path_explorer_threaded` | 1 | `path_explorer_t::step()`; gated by thread_local `allow_path_explorer_on_this_thread` | `path_explorer_barrier` (2); `path_explorer_await_mutex` (protects concurrent awaiters) |

- Route unreserving is on-demand from main-thread code: `convoi_t::unreserve_route()` sets
  `current_unreserver`, trips `unreserve_route_barrier` twice, clears it.
- Private-car route searches yield mid-search: after `max_route_tiles_to_process_in_a_step`
  (settings; separate paused-server value) route.cc hits `private_car_barrier` twice
  ("intentional"), so long searches do not block the frame indefinitely.
- `route_t::suspend_private_car_routing` (plain global bool) parks the private-car workers;
  `karte_t::suspend_private_car_threads()` = await → set flag under `private_car_route_mutex` →
  one barrier cycle → clear.
### Map-loop workers (`karte_t::world_xy_loop`, simworld.cc)

Lazily spawned DETACHED on first call (`spawned_world_threads`), reused for every later call;
split map rows into `env_t::num_threads` slices; `SYNCX_FLAG` adds `sem_t` pacing so threads
advance in x-lockstep (≤64 columns per block); start/end barriers per call; the calling thread
runs the last slice itself. Runs under `INTERACTIVE_RANDOM`. Users: `plans_finish_rd` (savegame
load; min/max-height aggregation guarded by recursive `height_mutex`), `recalc_transitions_loop`,
map-creation loops (`perlin_hoehe_loop`), `rotate90_plans` (rotation, after `await_all_threads`),
`update_map_intern` (image recalculation; `weg_calc_image_mutex`). (The season/snowline tile loop
in `karte_t::step` is a MAIN-THREAD loop, not a `world_xy_loop` user.) These threads are never
joined (process lifetime).

### Display helpers (display/simview.cc)

`display_region_thread`: spawned DETACHED once at the first multithreaded frame
(`spawned_threads`); screen split into vertical strips; per-thread clip index (`thread_num`) →
per-thread clipping state `clips[MAX_THREADS]` (simgraph16.cc; `CLIP_NUM_*` macros in
display/clip_num.h thread the parameter through the whole display call chain; the MT builds of
`display_after`/`display_overlay` signatures differ accordingly); `display_barrier_start/end`
bracket each frame; the main thread draws the last strip. Tooltips/overlays (`display_overlay`)
are drawn single-threaded after the threaded pass. Smart-cursor pause protocol (`hide_mutex`,
`hiding_cond`, `waiting_cond`) halts all display threads to draw the cursor region single-threaded.
Never joined (process lifetime).

### Save/load I/O (dataobj/loadsave.cc)

One joinable thread per buffered `loadsave_t` session (`set_buffered`): `save_thread`
flushes/compresses the second buffer while the main thread fills the first, or `load_thread`
fills/decompresses ahead; `loadsave_barrier` (2) + `loadsave_mutex`; load errors/EOF are
communicated via `readdata_flag` under `readdata_mutex`/`readdata_cond` (ASCII protocol diagram
in the file). Joined when buffering ends. Moves and compresses bytes only — no game-state work.

### Not threaded

Network I/O and command processing run on the main thread (`karte_t::interactive` →
`process_network_commands`; no thread primitives in network/ → [network](network.md)).
Sound/music: no game-spawned threads (backend-internal audio threads, e.g. SDL, only).
GUI/events: main thread.

## Shared-state mutation inventory

What non-main threads write beyond per-thread buffers (rule 1); locks → Lock inventory, windows → Worker inventory [CODE master @ 78a4bb3b9, 2026-09-07 inventory]. ✗ = unprotected (barrier discipline only).

- Private-car workers: `stadt_t::connected_cities/industries/attractions` (private_car_route_mutex; via `add_road_connexion` in `route_t::find_route` checker mode); `stadt_t::private_car_route_finding_in_progress` (✗; no reader found — Known problems); `weg_t::private_car_routes` writing element + backtrace statics (route_map_mtx); `karte_t::cities_to_process`/`cities_awaiting_private_car_route_check`. `connected_*` READERS (`stadt_t::check_road_connexion_to`, called from passenger generation) hold no lock — safety rests on the await placement before passenger generation plus the mid-search suspend mechanism, not on the mutex.
- Route-unreservation workers: `schiene_t::reserved` cleared (✗; main thread blocked on the barrier meanwhile); `obj_t::dirty` when `show_reservations`.
- Passenger/mail workers (under step_passengers_and_mail_mutex in `karte_t::generate_passengers_or_mail`): city history counters, gebaeude statistics, halt unhappy/no-route counters (`add_pax_unhappy` also books finance + `recalc_status` when not networked), fabrik mail-departed stats, checklist-fed `add_to_debug_sums`; `next_step_passenger/mail` after the barrier. Outside any mutex: `haltestelle_t::resort_freight_info` (`add_to_waiting_list`).
- Convoy workers (under step_convois_mutex, via `threaded_step`→`drive_to`): convoy route/state fields (incl. `wait_lock_next_step`, `allow_clear_reservation`), schedule/line-entry reverse flags, `simlinemgmt_t::update_line` (after `await_path_explorer`), `simline_t::set_state`, message system via `report_vehicle_problem`; plus `convoys_next_step` (master worker).
- Path-explorer worker (✗ throughout): halt cargo lists, connexion swaps + resort flags, schedule counts, reroute flags (`prepare_goods_list`/`swap_connexions`/`set_schedule_count`/`set_reroute_goods_next_step`); line/convoy average-journey-time entry removal; path_explorer_t statics incl. limit_set_t `local_*` copies (read by `process_network_commands` → `nwc_routesearch_t`).
- Map-loop workers (main thread blocked inside the loop; simulation not stepping): plan/ground/object state via callbacks — `plans_finish_rd` (load; object finish_rd into global lists under the gebaeude/label/leitung2 mutexes; heights under height_mutex), `perlin_hoehe_loop`, `recalc_transitions_loop`, `rotate90_plans`, `update_map_intern`.
- Display workers (display barriers; may overlap convoy/path-explorer workers, never main-thread simulation code): simgraph16 shared image cache, `grund_t::dirty` (smart cursor), hide/pause state (hide_mutex), framebuffer.
- Save/load threads: byte buffers + flags only — no game state.

## Lifecycle & startup order

- `karte_t::init()` (new map): `init_threads()` at the end.
- `karte_t::load(loadsave_t*)`: `destroy()` (which begins with `suspend_private_car_threads()` +
  `destroy_threads()`) →
  `init_threads()` EARLY, before the gamestate is read (code comment: destroy() destroyed the
  threads, so this must be here) → the whole load runs with workers alive → after
  `parallel_operations` is read from the save (server writes its own `num_threads-1`; clients
  adopt it; single-player resets to -1) → `destroy_threads()` + `init_threads()` again to resize
  all per-thread buffers. `karte_t::load(filename)` additionally calls
  `suspend_private_car_threads()` first ("Necessary here to prevent thread deadlocks").
- Consequence: workers run (parked/looping, reading world and settings state) while `load`
  rewrites that state — the TSan data-race family (→ [known-bugs](known-bugs.md)).
- `destroy_threads()`: awaits convoy/path-explorer/passenger subsystems, sets
  `terminating_threads`, trips every barrier once to release parked workers, joins all, destroys
  barriers/mutexes, frees the per-thread buffer arrays, resets flags.
- Display, map-loop threads: detached, process lifetime. Save/load thread: per file session.

## Per-step choreography (placement only — sync rules in the sync doc)

In `karte_t::step()`: queue cities → `start_private_car_threads` → `await_path_explorer` (before
using its results) → `await_convoy_threads` → single-threaded `convoi_t::step()` loop → city
stepping → `await_private_car_threads` → `weg_t::apply_travel_time_updates` →
`start_passengers_and_mail_threads` → citizen accounting → `await_passengers_and_mail_threads` →
merge thread-created private cars/pedestrians into the world in fixed buffer order → factories,
power, players, halts → periodic path-explorer category refresh → `check_transferring_cargoes` →
`start_path_explorer` → `start_convoy_threads` (last; "END OF THREADABLE AREA" follows).
`karte_t::pause_step()` (paused background server) runs a reduced version of the same cycle.
Convoy threads therefore run across the following `sync_step()`/display until the next step's
await — the code-stated caveat (forum topic 20994) → [known-bugs](known-bugs.md). Checkpoint and
debug-sum placement (rands[]/debug_sums[]) → [sync-and-determinism](sync-and-determinism.md).

## Per-thread state & buffers

- Buffer arrays allocated in `init_threads()` with `parallel_operations + 2` entries:
  `private_cars_added_threaded`, `pedestrians_added_threaded`, `transferring_cargoes`,
  `start_halts`, `destination_list`; `marker_t::markers` with `parallel_operations * 2`
  (main thread uses `marker_t::the_instance`; convoy workers index `markers[thread_number]`,
  private-car workers `markers[thread_number + po]`, selected via thread_local
  `karte_t::marker_index`; UINT32_MAX_VALUE = main).
- thread_local: `karte_t::marker_index`, `karte_t::passenger_generation_thread_number`;
  `route_t::_nodes[MAX_NODES_ARRAY=2]`/`_nodes_in_use` (per-thread A* node pools —
  `INIT_NODES`/`GET_NODES`/`RELEASE_NODES`/`TERM_NODES`; workers must call `TERM_NODES` before
  exiting — done in their loops), `route_t::MAX_STEP`/`max_used_steps`; the simrandom Mersenne
  state + `random_origin` + `noise_seed` (+`thread_seed` under DEBUG_SIMRAND_CALLS);
  `path_explorer_t::allow_path_explorer_on_this_thread`.
- NOT thread_local: `async_rand_seed` (utils/simrandom.cc global) — every passenger worker's
  startup `setsimrand()` writes it concurrently (TSan-flagged). It feeds only `sim_async_rand()`
  (unsynced), so no determinism impact is expected [UNVERIFIED impact].
- weg_t private-car route data is double-buffered: `private_car_routes[2][…]` reading/writing
  element; `swap_private_car_routes_currently_reading_element()` only from single-threaded
  context (`karte_t::refresh_private_car_routes`, after suspending the private-car threads;
  writes go through `private_car_route_map::route_map_mtx` via the backtrace functions).

## Lock inventory (game code)

- Simulation aggregates: `karte_t::private_car_route_mutex` (ERRORCHECK type; route queue, city
  road connexions in route.cc, suspend flag), `karte_t::step_passengers_and_mail_mutex` (also
  held around rdwr of `next_step_passenger`/`next_step_mail`), `path_explorer_await_mutex`
  (file-static), `step_convois_mutex` (simconvoi.cc; schedule/reverse-flag updates from
  `threaded_step` contexts), `weg_t::private_car_route_map::route_map_mtx`, `netlist_mutex`
  (powernet.cc), `load_mutex` (player/simplay.cc, `book_maintenance`), `freelist_mutex`
  (dataobj/freelist.cc — every freelist alloc/free; tpl/freelist_tpl.h's own mutex code sits
  under a never-defined `MULTI_THREADx` guard and is dead). `karte_t::unreserve_route_mutex` is
  never locked (vestigial).
- Display/image: simgraph16 `rezoom_img_mutex[MAX_THREADS]` + `recode_img_mutex`; recursive
  `calc_image` mutexes (weg, wayobj, tunnel, bruecke, crossing, leitung2); `height_mutex`
  (simworld.cc, `plans_finish_rd`); gebaeude `sync_mutex`/`add_to_city_mutex`; label
  `add_label_mutex`; leitung2 `verbinde_mutex`/`pumpe_list_mutex`/`senke_list_mutex`.
- Display frame: `hide_mutex` + `hiding_cond`/`waiting_cond` (smart cursor).
- Save/load: `loadsave_mutex`, `readdata_mutex` + `readdata_cond`.

## Known problems

- TSan data races between `karte_t::load` and the workers spawned by `init_threads` on every CI
  run, both branches; threading bug family (deadlocks topic 23021, `objlist_t::remove` race
  topic 20994): details and triage state → [known-bugs](known-bugs.md).
- Barrier-wait multiplicity ("having two/three of these is intentional") must balance exactly
  across all participants; nothing documents the accounting; fragility evidenced by the deadlock
  family above.
- Await gaps around map operations (→ [known-bugs](known-bugs.md) P2): `karte_t::enlarge_map`'s
  live path awaits the path explorer but NOT the convoy/passenger threads before reallocating
  and swapping the plan arrays; `karte_t::update_map` performs no awaits at all
  [CODE master @ 78a4bb3b9].
- Stray `pthread_mutex_unlock` with no matching lock in `unreserve_route_threaded`
  (`current_unreserver == 0` path); `unreserve_route_mutex` is never locked;
  `stadt_t::private_car_route_finding_in_progress` is written by workers without a mutex,
  persisted, and has no reader — dead state [CODE master @ 78a4bb3b9].
- MSVC "single threaded" configurations compile MT code (Build configuration) — user decision
  2026-09-07: very low priority, leave for now; fix-or-delete undecided
  (→ [known-bugs](known-bugs.md) P4).
- Sync-critical caveats (convoy threads during sync step, multi-city private-car threading) →
  [sync-and-determinism](sync-and-determinism.md).

## Provenance

Verified against master @ 78a4bb3b9. Structurally identical on ex-15 @ 91d9b252e: same worker
set, barrier counts, lifecycle calls, feature guards, primitives, thread_local declarations and
`async_rand_seed` global (ex-15's convoy/path-explorer internals differ heavily, but the
threading model does not) — this doc applies to both branches [CODE ex-15 @ 91d9b252e].
Simulation-worker threading is Extended-specific ([project-notes](project-notes.md));
display/save-load/map-loop threading predates the fork [PRIOR — coarse; not verified against
Standard sources]. Walkthrough 2026-09-07: rules 3–5, build configuration, primitives, worker
inventory and lifecycle confirmed as recorded (worker/barrier numbers could not be confirmed
from memory — they rest on agent code verification [CODE]); rules 1–2 rewritten from a code
inventory of non-main-thread mutations after a user correction [RECOLLECTION:2026-09-07].

## Open questions

- Exact barrier-trip accounting for `private_car_barrier` across cycles (main thread vs po
  workers, mid-route-search yields): not derivable by static reading; relevant to the deadlock
  family (→ [known-bugs](known-bugs.md)). Asked user 2026-09-07: unknown — resolve dynamically
  at deadlock triage.
- Passenger/mail generation split: `init_threads()` creates `parallel_operations + 1` worker
  threads (po = `get_parallel_operations()`, normally `num_threads - 1`), numbered 1..po+1, but
  each worker divides the per-step quota (`next_step_passenger`/`next_step_mail`) by po — not
  po+1 — and the remainder and small-quantity fallback branches are assigned to thread number 0,
  which no worker holds (the main thread never generates inside the threaded window). Defect, or
  benign because the quota accumulators self-correct and preserve the long-run generation rate?
  User could not confirm from memory (asked 2026-09-06); verify at triage.
