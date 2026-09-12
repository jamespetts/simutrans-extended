---
status: reviewed
verified: master @ e89843ec8
---
# Sync & determinism: rules for simulation code

**Covers:** the determinism requirements for ALL code that changes simulation state: utils/simrandom.*, utils/float32e8_t.*, the checklist coverage contract (utils/checklist.* + the checkpoint layout of `karte_t::step`/`karte_t::sync_step` in simworld.cc), threaded-work reintegration, and tool/command synchronisation. The underlying network system (transport, packets, connection lifecycle, checklist transmission) → [network](network.md).

Read this for ANY change to simulation code, even if it looks single-player-only: single-player uses the same queued command/tool execution path, and all simulation code must stay network-safe.

## Model in one paragraph

Network play is deterministic lockstep: server and every client independently run the *same* simulation. Only commands (tools, player changes, chat, checks, pacing) travel over the network; the server stamps each command with the sync step at which every peer executes it. Full game state travels only as a complete savegame when a client joins or a forced resync occurs. Anything not saved, or computed differently on any peer, therefore diverges; divergence is policed by the checklist system (below). Mechanism → [network](network.md). [CODE master @ 78a4bb3b9]

## RNG rules (utils/simrandom.*)

- Two generators, strictly separated [CODE]:
  - `simrand(max, caller)` / `simrand_normal` / `simrand_plain` / `pick_any` / `pick_any_weighted` — the synced Mersenne-Twister stream. Its full state is saved/loaded via `simrand_rdwr` (called from `karte_t::rdwr_gamestate`) and sampled by the checklist (`get_random_seed()` reads the state without advancing it).
  - `sim_async_rand(max)` — separate state, NOT saved, NOT checklist-sampled. Use only for UI, sound/music, animation phase, translation fallbacks, GUI previews — anything that must not perturb the synced stream. Code comment (simsound.cc): shuffling songs "must not use simrand()!".
- `simrand()` asserts `random_origin` is not `INTERACTIVE_RANDOM`: never draw synced randomness from interactive/display contexts. Random-mode flags (`STEP_RANDOM`, `SYNC_STEP_RANDOM`, `LOAD_RANDOM`, `MAP_CREATE_RANDOM`, `INTERACTIVE_RANDOM`, `MODAL_RANDOM`) mark the current context (set_random_mode/get_random_mode).
- The number of RNG draws must be identical on every peer and must not depend on data that can differ between peers. Where branches differ, make a dummy draw: the one-random-call pattern in city naming (stadt_t, simcity.cc) and stop naming (simhalt.cc) exists exactly "to avoid desyncs in network games" [CODE comments].
- The MT state is `thread_local`: every worker thread has its own stream. Threads consuming randomness must be seeded deterministically from synced values — e.g. `karte_t::step_passengers_and_mail_threaded` seeds per-thread RNG explicitly, with a code comment that the seed "must be initialised with values deterministic between network clients" [CODE].
- Old saves: pre-Extended-14.51 network RNG state lived in `settings_t::random_counter`; newer saves use `simrand_rdwr` (both paths in simworld.cc / dataobj/settings.cc) [CODE].
- Debug compile switches in simconst.h: `DEBUG_SIMRAND_CALLS` (logs every caller+seed — built for network-desync hunting), `DEBUG_SIMRAND_CALLS_1`, `DISABLE_RANDOMNESS` [CODE].

## Floating-point rules

- Full constraint (canonical): [project-architecture](project-architecture.md) constraint 1. Rule: integers may be used anywhere; `double`/`float` may be used anywhere that never needs to be kept in sync between network servers/clients; the fixed-point `float32e8_t` (utils/float32e8_t.h) only where sync-critical code genuinely needs decimals (e.g. physics) — it is much slower than float or int [RECOLLECTION:2026-09-07].
- Observed boundary discipline [CODE]: simulation state is integer or `float32e8_t` (convoy physics `calc_move`, resistances/brake factors in convoy.h/cc; velocity `convoi_t::v`; unit conversions in simunits.h; vehicle residual values). `to_double()`/`to_sint32()` appear only at display/debug/export boundaries. Only the fixed-point *mantissa* crosses the network (checklist feed in `convoi_t::sync_step` uses `v.get_mantissa()`).
- `double` appears in map-creation-only code (`stadt_t::random_place`, Pareto city sizing in `karte_t::distribute_cities`, Perlin noise) — map creation is never synced; see "Map creation" below [CODE].

## State & serialisation rules

- Every piece of intermediate simulation state that can influence future behaviour must be serialised in rdwr, because joining/resyncing clients re-baseline by loading the server's savegame; unsaved state silently diverges. Code-documented examples [CODE comments]: fractional city growth accumulator (`unsupplied_city_growth`), city private-car counters, world road/travel-time averages (simworld.cc), vehicle `direction_steps`/`pre_corner_direction`.
- rdwr mechanics and version-condition rules → [savegame-versioning](savegame-versioning.md). Changing serialisation of synced data is change-restricted (AGENTS.md rule 5).
- Settings are server-authoritative in network mode (special-cased rdwr in dataobj/settings.cc); loading very old saves in network mode forces fixed defaults for certain settings "to prevent desyncs" [CODE comment].
- Algorithms fed by non-deterministic iteration order must still decide deterministically — e.g. passenger-class downgrading in simhalt.cc handles classes arriving in non-deterministic vehicle order [CODE comment].

## Threading rules

- Work-split identity: `karte_t::parallel_operations` (simworld.h/cc) fixes the number of worker splits in network mode; the server's value is transmitted in the savegame so all peers split threaded work identically regardless of local hardware. Threaded simulation features are compile-guarded: `MULTI_THREAD_PASSENGER_GENERATION`, `MULTI_THREAD_CONVOYS`, `MULTI_THREAD_PATH_EXPLORER`, `MULTI_THREAD_ROUTE_UNRESERVER` [CODE].
- Peers must nevertheless run the SAME `threads` setting (simuconf `threads` / `-threads` → `env_t::num_threads`): the checklist compares the local thread count every sync step (`debug_sums[4]`), so differing thread counts cause checklist mismatch → kick/disconnect. There is no warning or auto-correction; this is a de facto configuration requirement [CODE].
- The reintegration model [CODE master @ 78a4bb3b9]: workers compute into per-thread buffers (sized `parallel_operations + 2`) or thread-local RNG; the main thread awaits at fixed points (`karte_t::await_all_threads`, per-subsystem await helpers) and merges world-list insertions in fixed thread/index order. Workers DO directly mutate shared state inside their bounded start→await windows, under named mutexes or barrier discipline — including convoy route/state fields, way reservation clears, city connexion maps, halt connexion tables and statistics counters, and the checklist-fed `debug_sums`; the per-thread-class inventory of shared writes, locks and windows lives in [threading](threading.md). Determinism requires every worker-side write to be reproducible identically on all peers (deterministic split + seeding) and confined to a window with no concurrent main-thread consumer [CODE; user correction RECOLLECTION:2026-09-07]. Examples:
  - Passenger/mail generation: deterministic quota split + deterministic per-thread seeding; thread-created private cars/pedestrians are re-inserted into the world by the main thread in fixed order — code comment: "necessary in network mode to ensure that all cars ... are added to the world list in the same order even when the creation of those objects was multi-threaded" (simworld.cc).
  - Convoys: `convoi_t::threaded_step()` does route-finding only (no movement/physics; reservations/block working stay in the single-threaded `convoi_t::step()`), but it writes convoy route/state fields, schedule reverse flags, line state and the message system on the worker under `step_convois_mutex` (→ [threading](threading.md) inventory); some changes remain deferred to the single-threaded step (e.g. via `wait_lock_next_step`).
  - Private-car route checking: threads queue travel-time updates (`weg_t::add_travel_time_update`); the main thread merges them (`weg_t::apply_travel_time_updates`). In network mode this is clamped to one city per step — code comment: multi-threaded multi-city processing "is not network safe" (reason unresolved in code).
  - Path explorer: per-peer work quantum is negotiated over the network (`nwc_routesearch_t` broadcasts the minimum iteration-limit set) so every peer does identical work per step → [network](network.md). Goods rerouting requests are queued (`set_reroute_goods_next_step`) and consumed single-threaded in `haltestelle_t::step()` — "not compatible with multi-threading" [CODE comment].
- Await before modify: convoy and path-explorer threads run across the sync_step/display phases (started near the end of `karte_t::step()`, awaited in the next one), and private-car threads run inside step()/pause_step windows. ANY main-thread code that modifies data a live worker may be reading — tools, way/signal building or removal, halt tile changes, anything touching "potential routes" — must call the matching await helper first (`await_convoy_threads`, `await_private_car_threads`, `await_path_explorer`; `await_all_threads` when in doubt, as before save/rotate). Missing it is a data race → non-deterministic route finding or crash → desync. The step() comment on `start_convoy_threads` states the rule ("safe to have this concurrent with everything but the single-threaded convoy step, and anything that modifies potential routes"). Precedents [CODE]: `grund_t::weg_entfernen`, `weg_t::init`/`degrade`/`apply_travel_time_updates`, `roadsign_t::init`, `haltestelle_t::add_grund`/`rem_grund`/`destroy_all`, sites in simdepot.cc and simtool.cc.
- New threaded simulation work must follow the same pattern: deterministic seeds, deterministic split via `get_parallel_operations()`, per-thread buffers or mutex-protected aggregates, fixed-order main-thread merge.
- Threading machinery (thread lifecycle and the start_*/await_* helper mechanics, barrier/mutex/semaphore inventory, thread_local state and per-thread buffers, display/save/map-loop threads, race/deadlock diagnosis, MULTI_THREAD build configuration) → [threading](threading.md): MANDATORY when a change reaches any of that; not needed for work that stays within the patterns above.

## Tool & command rules

- Tools that change game state are NOT executed locally in network mode: `karte_t::call_work`/`call_work_api` send a `nwc_tool_t` to the server, which authorises it (`clone()`: player unlock, scenario rules, editor-tool player checks), stamps `sync_step = sync_steps + 1`, and broadcasts it; every peer (server included) then executes the same tool identically at that sync step. Single-player uses the same queued path, so tool behaviour is uniform [CODE].
- Tools that provably do not change game state may run locally: override `is_init_keeps_game_state()` / `is_work_keeps_game_state()` (simtool.h). The server rejects networked init/work for such tools. Getting this wrong is a desync or a rejected command [CODE].
- `WFL_LOCAL` is set only for the originating peer (local-only side effects such as result callbacks); `WFL_SCRIPT` is honoured only from the server — scripts run server-only [CODE; script items unconfirmed from user experience — the user has not worked with scripts, RECOLLECTION:2026-09-07].
- Tool `custom_data` transported in a command is capped (256 bytes in `nwc_tool_t`); tools needing more must not rely on the network path [CODE].
- Some tools build their own `nwc_tool_t` directly for drag operations (several sites in simtool.cc) [CODE].

## Map creation & enlargement

- Map creation runs under `MAP_CREATE_RANDOM` with its own seeding (`setsimrand(0xFFFFFFFF, map_number)` in `karte_t::enlarge_map`) and may use `double`; it happens before/outside networked play (`karte_t::init` shuts the network down first) [CODE].
- Runtime map enlargement is disabled in network mode ONLY by UI/tool guards (enlarge-map dialog not created when `env_t::networkmode`; tooltip "deactivated in online mode"). `karte_t::enlarge_map()` itself has NO networkmode guard and reseeds the RNG — any future code path calling it while networked would bypass the protection and desync. Treat as a hazard [CODE].

## Checklist coverage contract

`checklist_t` (utils/checklist.h) is created EVERY sync step in `karte_t::interactive`. Periodic server-side comparison runs every `env_t::server_sync_steps_between_checks` sync steps (simuconf `server_frames_between_checks` — often set to 1 when desync-debugging [RECOLLECTION:2026-09-07]), and checklists are additionally compared at every tool command and at join (transmission/comparison mechanism → [network](network.md)). Fields [CODE]:

- `random_seed` — `get_random_seed()` at checklist creation.
- `halt_entry`/`line_entry`/`convoy_entry` — quickstone allocation counters (`halthandle_t::get_next_check()` etc., tpl/quickstone_tpl.h): detect divergent object creation/deletion counts.
- `ss`/`st`/`nfc` — sync step, step, network frame count.
- `rand[CHK_RANDS=32]` — RNG-state snapshots taken at fixed checkpoints inside `karte_t::sync_step` and `karte_t::step` (the world arrays `rands[]` in simworld.h). Any new per-step simulation work is automatically bracketed by existing checkpoints; a desync in a new subsystem shows up in the rand slot following it.
- `debug_sum[CHK_DEBUG_SUMS=10]` — per-sync-step accumulators (world array `debug_sums[]`, fed via `karte_t::add_to_debug_sums`), reset at the top of every `sync_step`.
- `hash` — used only in heavy mode (whole-game-state adler32).

### rands[] checkpoint map [CODE master @ 78a4bb3b9]

| # | Checkpoint (after …) | # | Checkpoint (after …) |
|---|---|---|---|
| 0 | entry to `sync_step()` | 14 | single-threaded convoy stepping |
| 1 | unused (stays cleared) | 15 | city stepping loop |
| 2 | `sync_eyecandy` (animations) | 16 | private-car thread await + `apply_travel_time_updates` |
| 3 | `sync_way_eyecandy` (pedestrians etc.) | 17 | passenger/mail generation |
| 4 | main `sync` list (vehicles, road traffic) | 18 | citizen/jobs/visitor accounting |
| 5 | ticker update | 19 | passenger/mail thread await |
| 6 | display/event/frame work, end of sync_step | 20 | factory stepping |
| 7 | explicitly zeroed | 21 | power network stepping |
| 8 | entry to `step()` | 22 | player stepping |
| 9 | `new_month()` | 23 | `haltestelle_t::step_all()` |
| 10 | private-car route-thread start | 24 | periodic path-explorer category refresh |
| 11 | season/snowline tile loop | 25 | `check_transferring_cargoes()` |
| 12 | `path_explorer_t::step()` | 26 | scenario step, end of `step()` |
| 13 | convoy threaded-step await | 27–31 | explicitly zeroed, unused |

The eyecandy lists are stepped under `INTERACTIVE_RANDOM` (not exactly synchronised); the main `sync` list holds the sync-critical moving objects (convoys/vehicles, road traffic, pedestrians) [CODE].

### debug_sums[] meanings [CODE master @ 78a4bb3b9]

| # | Meaning (feed site) |
|---|---|
| 0 | convoy fixed-point speed mantissa sum (`convoi_t::sync_step`) |
| 1 | same, weighted by convoy id — diff[1]/diff[0] identifies a single desynced convoy |
| 2 | current convoy speed km/h (`convoi_t::sync_step`) — reset comment in simworld.cc is STALE ("Einwhoner") |
| 3 | target convoy speed km/h — reset comment STALE ("Number of buildings") |
| 4 | local `env_t::num_threads` (see threading rules) |
| 5 | passengers/mail units generated this step |
| 6/7 | transferring cargoes before/after passenger generation |
| 8/9 | random direction choices by road vehicles with/without a route (vehicle/simroadtraffic.cc) |

When adding per-step diagnostic aggregates for a new subsystem, use an unused slot and keep the reset comments in simworld.cc consistent with the feed site.

## When a desync happens

- Comparison points: periodic `nwc_check_t` from the server; the checklist attached to every tool command; the ready-checklist at join. Mismatch → client disconnect ("Lost synchronisation with server") or kick; details → [network](network.md).
- Debug workflow: `-heavy 0..2` (`env_t::network_heavy_mode`): 1 = per-frame whole-game-state hash (`karte_t::get_gamestate_hash`, adler32 over the streamed save), 2 = additionally rotating savegame dumps `save/heavy/heavy-{server|client}-<sync_steps>.sve` (last 10 kept). Checklist printouts label the groups: `ssr` (rands 0–7), `str` (rands 8–23), `exr` (rands 24–31), `sums` [CODE].

## Known problems & caveats (code-stated)

- `debug_sums[2]/[3]` reset comments in simworld.cc contradict the actual feed sites (see table); the feed-site comments in simconvoi.cc are authoritative [CODE].
- Convoy threads running across the sync step are a TSan-verified race family (CI TSan smoke, 2026-09-12): convoy-worker route finding reads tile object lists/convoy state while the main thread mutates them (vehicle hops, `new_month`) — an actual defect (crash + desync family), so the "uncertain" code comment in simworld.cc is superseded. Details → [known-bugs](known-bugs.md).
- Multi-threaded private-car route checking across multiple cities is "not network safe"; cause unresolved in code — hence the one-city-per-step clamp in network mode [CODE comment].
- `ALWAYS_CACHE_SERVICE_INTERVAL` (simhalt.h): comment says network-safe but its test was not conclusive at the time [CODE comment caveat].
- A suspected-desync comment sits after the scenario step in `karte_t::step()` ("Loss of synchronisation suspected to be in a block of code ending here") [CODE comment]. The user could neither confirm nor disconfirm the suspicion [RECOLLECTION:2026-09-07].
- Dead code: `karte_t::set_rands`/`inc_rands` have no callers; `rands[1]`, `rands[27..31]` unused [CODE].
- Desync leads from local artefacts and forum reports (historically signals/reservations) → [known-bugs](known-bugs.md), the canonical home for unverified leads; the artefact files exist in the repo root [CODE].

## Provenance

Verified against master @ 78a4bb3b9. The covered files (utils/simrandom.*, utils/checklist.*, network/) are materially identical on ex-15 — the only branch difference in these files is an added integer `sigmoid()` helper (no RNG semantics change) — so this doc applies to both branches [CODE]. The lockstep/checklist model and threading design are Extended-era developments built on the Standard Simutrans network base; coarse provenance only, per conventions.

## Open questions

- Is the de-facto requirement for identical `threads` settings across peers intentional (given `parallel_operations` already makes the work split identical), or should the checklist tolerate differing thread counts? (Rationale evidenced only by a commit title; user could not confirm, asked 2026-09-07.)
- Why is multi-city threaded private-car route checking not network safe? (Code comment: reason unclear, route-finding determinism suspected. The user spent a long time debugging this without success — a hard problem [RECOLLECTION:2026-09-07].)
