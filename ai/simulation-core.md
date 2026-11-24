---
status: draft
verified: main loop and stepping @ ex-15 @ f74322b648 (2026-11-24)
---
# Simulation core (root-level sim* files)

**Covers:** simworld.*, simmain.*, simintr.*, simunits.*, simtool.*, simtool-dialogs.h, simmenu.*, siminteraction.*, simevent.*, simmesg.*, simdebug.*, simmem.*, simio.*, simskin.*, simsound.*, simticker.*, simloadingscreen.*, unicode.*, simconst.h, simtypes.h, macros.h, pathes.h, scrolltext.h. Handles: convoihandle_t.h, halthandle_t.h, linehandle_t.h (→ [vehicles-and-convoys](vehicles-and-convoys.md), [routing-and-scheduling](routing-and-scheduling.md)). simcolor.h → [rendering](rendering.md).

## Initial facts

- Structural headers: `simworld.h` (world state), `simtypes.h` and `simdebug.h` are near-universal dependencies [CODE].

## Main loop and stepping

`karte_t::interactive()` (simworld.cc) is the loop that runs a game world. It schedules
`karte_t::sync_step()` and `karte_t::step()` differently for each value of `step_mode`
[CODE ex-15 @ f74322b648]:

- `NORMAL` (offline play): `karte_t::step()` runs when `next_step_time` is reached; each
  `step()` sets `next_step_time` to `dr_time() + 3200/get_time_multiplier()` in its normal path.
  `karte_t::sync_step()` is called from `interrupt_check()` (simintr.cc) at the frame rate.
- `FIX_RATIO` (server, and network client): `sync_step()` runs every frame and `step()` every
  `settings.get_frames_per_step()` frames.
- `FAST_FORWARD`: `sync_step(100, true, false)` and then `step()` on every iteration.

`karte_t::sync_step(delta_t, do_sync_step, display)` advances `ticks` by `delta_t` when
`do_sync_step` is true, and sync-steps the moving-object lists in this order: `sync_eyecandy`,
`sync_way_eyecandy`, then `sync` (the `karte_t::sync_list_t` that holds the moving vehicles). That
last walk is the largest single steady-state cost on the gargantuan fixture
(→ [performance](performance.md)).

### The month boundary

`karte_t::step()` checks the month boundary before any simulation work: after parking the worker
threads (next paragraph) and computing `delta_t` as `ticks - last_step_ticks`, it compares `ticks`
with `next_month_ticks`. If `ticks > next_month_ticks`, it advances `next_month_ticks` by
`ticks_per_world_month` and calls `karte_t::new_month()`. `next_month_ticks` is set to one month
in `karte_t::init()`; after a load, it is recomputed as the next multiple of
`ticks_per_world_month` above the saved `ticks`.
`ticks_per_world_month` is `1 << ticks_per_world_month_shift`, and the shift comes from
`settings.get_bits_per_month()`. The comparison is strict `>`, so the boundary is processed on the
first step whose `ticks` value is greater than the boundary value.

`new_month()` runs synchronously on the main thread. At the head of `step()`, before the month
check, the step awaits the convoy route-finding workers (`await_convoy_threads()`, compiled under
`MULTI_THREAD_CONVOYS`) and the path explorer (`await_path_explorer()`, compiled under
`MULTI_THREAD_PATH_EXPLORER`), so both are parked for the whole of `new_month()` [CODE ex-15 @
f74322b648]. Two properties require this:

- The workers read map tiles and convoy state, and `new_month()` rewrites convoy statistics and
  can liquidate convoys.
- The factory intransit gating in `new_month()` (`fabrik_t::calc_max_intransit_percentages`) reads
  the path explorer's live progress state, which the explorer thread mutates. A mid-run read would
  make the gate result depend on thread scheduling, and the result feeds saved factory state
  (`max_transit`), so it is a desynchronisation vector.

The private-car, passenger and mail worker pools are started later in the same step, after
`new_month()`, so they do not run during it. `new_month()` itself awaits the path explorer again
before requesting a refresh; that await returns immediately because the explorer is already
parked.

`step()` abandons the remainder of the step, including all work after the month change, when
`world_generation` has changed during the step (the map was replaced, loaded or destroyed). Every
such check is `abort_if_world_changed()`.

### Order of work in karte_t::step()

After the month check and the step-pacing block, `step()` does the following, in this order
[CODE ex-15 @ f74322b648]:

1. Processes pending season and snowline changes over a bounded slice of the tile array
   (`planquadrat_t::check_season_snowline`), so a change completes over several steps.
2. Refreshes the private-car route check when no earlier check is outstanding
   (`refresh_private_car_routes()`), then publishes cities to the private-car workers
   (`start_private_car_threads()`) in multi-threaded builds; in single-threaded builds each
   published city checks its routes inline (`stadt_t::check_all_private_car_routes()`).
3. Steps the convoys: `convoi_t::threaded_step()` (only when `MULTI_THREAD_CONVOYS` is not
   defined), then `convoi_t::step()` in reverse index order, because convoys can be deleted
   during the step.
4. Awaits the private-car workers, merges duplicate destination lists
   (`weg_t::private_car_route_map::merge_duplicate_destination_lists`) and applies travel-time
   updates (`weg_t::apply_travel_time_updates`).
5. `stadt_t::step()` for every city.
6. Generates passengers and mail (`start_passengers_and_mail_threads()`, or
   `step_passengers_and_mail()` when the threads are not used), awaits those workers, and adds
   the private cars and pedestrians they created to the tile object lists and the `sync` list in
   a fixed order, so that the addition order is identical on every peer.
7. `fabrik_t::step()` for every factory; `pumpe_t::step_all()`, `senke_t::step_all()` and
   `powernet_t::step_all()`; `player_t::step()` for every player.
8. `haltestelle_t::step_all()`; `path_explorer_t::refresh_all_categories(false)` every
   `settings.get_reroute_check_interval_steps()` steps; `check_transferring_cargoes()`.
9. The deferred autosave when `autosave_pending` is set, then `start_path_explorer()` and
   `start_convoy_threads()` for the next step.
10. `check_midi()` every eighth step, `recalc_season_snowline(true)`,
    `step_time_interval_signals()`, the client-count message when players join or leave a
    server, and `scenario->step()` for a scripted scenario.

### Phases of karte_t::new_month()

The phases run in the order below, each in the function named [CODE ex-15 @ f74322b648]:

1. `update_history()` aggregates the cities' month and year statistics into the world
   `finance_history_month`/`finance_history_year` tables; the world monthly history is then
   rolled one slot and `current_month` and `last_month` advance. December wraps `last_month` to 0,
   and the timeline is switched off permanently once `current_month` passes
   `DEFAULT_RETIRE_DATE * 12`.
2. `weg_t::new_month()` for every way in `weg_t::get_alle_wege()`.
3. `calc_max_vehicle_speeds()`.
4. `minimap_t::get_instance()->new_month()`.
5. `player_t::new_month()` for every player slot; a player whose call returns false is removed.
   The company window (`ki_kontroll_t`) is then refreshed if it is open.
6. `convoi_t::new_month()` for every convoy in `convoi_array`; `base_pathing_counter` increments.
7. `fabrik_t::new_month()` for every factory in `fab_list`; a factory that closes during its
   update appends itself to `closed_factories_this_month`. The closing factories are then
   processed. Up to four attempts are then made to add industry chains while
   `actual_industry_density` is below `get_target_industry_density()`, each attempt gated on
   `simrand(100)` against a probability derived from the deficit (minimum 8 per cent), through
   `factory_builder_t::increase_industry_density()`.
8. `gebaeude_t::check_road_tiles(false)` for every attraction in `world_attractions`.
9. `cities.update_weights(get_population)`, then `stadt_t::new_month()` for every city,
   accumulating the cities' power demand; `recheck_road_connexions` is cleared.
10. If power stations are available and `electric_productivity * 4000 / total_electric_demand`
    is below `get_settings().get_electric_promille()`,
    `factory_builder_t::increase_industry_density()` runs to add industries, which causes power
    stations to be built.
11. `haltestelle_t::new_month()` for every halt. This is the waiting-times update described in
    [routing-and-scheduling](routing-and-scheduling.md).
12. `depot_t::new_month()` for every depot in `depot_t::get_depot_list()`.
13. `scenario->new_month()`.
14. When `last_month == 0` (January), `new_year()`.
15. `way_builder_t::new_month()`, then `hausbauer_t::new_month()`.
16. For every substation in `senke_t::senke_list`: if a city now covers it, the substation is
    appended to that city's list (`stadt_t::add_substation`); otherwise
    `check_industry_connexion()` runs.
17. `recalc_average_speed(false)`.
18. `tool_t::update_toolbars()`.
19. `autosave_pending` is set when autosave is enabled and due for this month, the game is not in
    network mode, and the new-world dialogue is not open. The save itself runs at the end of
    `karte_t::step()`.
20. `recalc_passenger_destination_weights()`.
21. `set_citycar_speed_average()`, `calc_generic_road_time_per_tile_city()`,
    `calc_generic_road_time_per_tile_intercity()`, `calc_max_road_check_depth()`.
22. `pedestrian_t::check_timeline_pedestrians()`.
23. `await_path_explorer()` (under `MULTI_THREAD`) and
    `path_explorer_t::refresh_all_categories(false)`.

`karte_t::step_month()` calls `new_month()` once per month; it is used to place a new world at its
starting month.

## Planned sections

- World lifecycle: load/save (coordinates with [savegame-versioning](savegame-versioning.md)), world lists.
- Tool system (simtool): how tools are dispatched and parameterised.
- Units & numeric types (simunits; float32e8_t → [utilities](utilities.md)).
- Invariants; known problems & history (e.g. threading of world-list mutations → [threading](threading.md); root has local-only diagsessions on this).

## Performance hotspots

Measured on the gargantuan fixture (method and full inventory: [performance](performance.md))
[EXECUTION-VERIFIED:2026-09-10 master @ d40847e90]:
- The per-step walk over synced moving objects is the single hottest leaf on the fixture:
  `karte_t::sync_list_t::sync_step` 23.7% self / 67.3% incl of in-game CPU; `karte_t::sync_step`
  overall 69.5% incl. Treat everything under `step()`/`sync_step()` as hot.
- Other simworld.cc costs: `karte_t::lookup` 2.4% self, `karte_t::check_transferring_cargoes`
  2.6% self, `karte_t::generate_passengers_or_mail` 4.8% incl
  (→ [economy-and-passengers](economy-and-passengers.md)).
- Savegame load (`karte_t::load`) ~80 s on the fixture; dominates server rotations and client joins.

Month change, measured on the month-change fixture (`month-change-test.sve`, defined in
[performance](performance.md)): `karte_t::new_month()` takes 172-178 ms, all of it a single
main-thread stall. The largest phases are halts 41-42 ms (of which the waiting-times ageing loop
in `haltestelle_t::new_month()` is 36 ms), convois 35-36, ways 33-37, cities 26-28, factories
13-14 and players 12-13 ms; every other phase costs 0-1 ms
[EXECUTION-VERIFIED:2026-11-24 ex-15 @ f74322b648]. Phase table and measurement method:
[performance](performance.md).

## Open questions

- Which calls in `karte_t::step()` are network-sync-critical is not researched. The per-step
  order is documented above, but which of those phases must not be reordered for sync is open
  (the `rands`/`debug_sums` checklist fields and the `sync_step`/`step` split are the starting
  points; → [sync-and-determinism](sync-and-determinism.md)).
- Whether any phase of `karte_t::new_month()` can run concurrently with a worker pool is
  unassessed; the current code parks the convoy and path-explorer pools for the whole call.
