---
status: draft
verified: ex-15 @ 684d570f9 plus working-tree recombination runtime (2026-11-13); reservation audit 2026-11-14; reservation-index implementation and call-on/refactor changes (2026-11-14)
---
# Block reservations and reservation clearing

**Covers:** boden/wege/schiene.* (reservation state on every rail-family way) and boden/wege/runway.cc,
dataobj/crossing_logic.* and obj/crossing.* (level crossings), vehicle/rail_vehicle.* (`block_reserver`,
per-vehicle releasing), vehicle/air_vehicle.cc (runway reservation), simconvoi.*
(`convoi_t::reserve_own_tiles`, `convoi_t::unreserve_route`, `release_target_reservations`), simworld.*
(the reservation buckets and the retained route-unreserve worker pool), simtool.cc (`tool_clear_reservation_t`), and old_blockmanager.*
(legacy savegame conversion only). Parent article and player-facing model:
[signals-and-blocks](../signals-and-blocks.md). Threading: [threading](../threading.md). Determinism:
[sync-and-determinism](../sync-and-determinism.md).

Sub-document: [per-holder reservation index](reservation-index.md) — the replacement
for map-wide reservation clearing; its memory model, registration rules, call-on dependency, implementation
sequence, tests, and requirement to fix incidental signalling defects found during subsequent refactoring.
Read it before changing broad reservation clearing or its worker pool.

## Terms

The parent article defines **way**, **reservation**, **block**, **convoy**, **working method**, **aspect**
and **stale reservation**; this article uses those terms. The reservation types are `block` (the
ordinary claim on a tile), `directional` (a claim for one travel direction), `priority` (a type no code
currently requests) and `stale_block` (a block reservation deliberately kept behind a train under
token block or one train staff working); `reservation_type::end` is a query sentinel meaning any
reservation, not a type a train requests.

## Reservation state on the way

Reservations are the mechanism behind block working; the player does not create them. A train that
cannot reserve the next block stops at the signal (where the working method uses one), and the
clear-reservation tool draws reserved tiles with a colour per reservation type and clears a clicked
convoy's claim. Runways are `schiene_t` instances and use the same reservation record; level crossings
additionally coordinate their own road and rail state machine.

`schiene_t` holds `reserved` (`convoihandle_t`, unbound when free), `type` (`reservation_type`: `block`, `directional`, `priority`,
`stale_block`) and `direction` (`ribi_t::ribi`, the eight-direction bit mask type, holding the permitted direction).
`schiene_t::can_reserve(c, dir, type, check_directions_at_junctions)` decides admission: `block` succeeds when free, held by `c`, held by
a `directional` reservation whose direction permits this travel direction (with bend, diagonal and junction allowances), or held by a
`priority` reservation (unconditional in the current code); `directional` succeeds when free, held by `c`, held by a `priority`
reservation, when the travel direction equals the reserved direction or is `ribi_t::all`, or when the tile is a junction and
`check_directions_at_junctions` is false; `priority` succeeds only when free or held by `c`.

`schiene_t::reserve` applies the type, direction and handle, returning false without changing anything if `can_reserve` fails. A
`directional` request on a tile holding a `block` or `stale_block` reservation returns true without changing the handle, so a directional
reservation can be reported as available over an existing block reservation. `schiene_t::is_reserved(t)` reports `stale_block` as a block
and `t == reservation_type::end` as any reservation. `schiene_t::set_stale` converts `block` to `stale_block`, `schiene_t::is_stale` tests
it, and `schiene_t::get_reservation_type` reports a stale reservation as `block`. `schiene_t::cleanup`, the pre-deletion hook, sets the
way's `ribi` mask to `ribi_t::none` and asks the reserving convoy to find a new route; `schiene_t::rotate90` rotates the reserved
direction. `schiene_t::show_reservations` draws reserved tiles with an outline coloured by type (`schiene_t::get_outline_colour`);
`tool_clear_reservation_t` toggles it on tool entry and exit and, on a click, clears the reservation of the convoy holding the clicked
tile.

`schiene_t::rdwr` defines what is saved for a way, including the reservation handle id, type and direction, and on saving discards a
reservation whose front vehicle's waytype is not a rail-family waytype (`is_type_rail_type`) rather than writing a corrupt handle. Every
rail-family way class calls `schiene_t::rdwr`, so rail reservations are saved. Runways are `schiene_t` instances, but `runway_t::rdwr`
handles reservation state separately: it saves and restores the holder id but not reservation type or direction. Aircraft make further
reservations through `air_vehicle_t::block_reserver`. Level crossings reserve independently:
`block_reserver` calls `crossing_t::request_crossing` before reserving tiles beyond a crossing and `crossing_t::release_crossing` when
giving tiles back, with the state machine in `crossing_logic_t`. `old_blockmanager_t` holds no runtime state: `old_blockmanager_t::rdwr`
(from `karte_t::rdwr_gamestate` while loading) reads the block records of saves old enough to carry them, and
`old_blockmanager_t::finish_rd` (from `karte_t::load`) pairs them and constructs the equivalent `signal_t` objects.

## Who reserves, and up to what

This is the routine that makes signals effective. When a train wants to enter the next tile, it tries
to reserve the block ahead according to its working method; a failed reservation leaves the train
standing at the signal, where the working method uses one. The walk described here is the code that
implements that attempt.

`rail_vehicle_t::block_reserver(route, start_index, modified_sighting_distance_tiles, next_signal_index, count, reserve, force_unreserve,
...)` walks the route from `start_index`, applies the working method in force, reserves each way tile (`reserve = true`) or releases it
(`reserve = false`), and sets the aspects of the signals it passes. `count` limits how many signals the walk may pass (call sites use 0, 1
and effectively unlimited values such as 100000 and 1001); `next_signal_index` receives the last route index to which the train may
proceed. The main call sites:

- `rail_vehicle_t::can_enter_tile` decides whether the leading vehicle may enter the next tile; it is called from the main-thread
  movement code (`vehicle_t::can_enter_tile` via `vehicle_t::hop_check`, once per tile per step through
  `convoi_t::get_checked_tile_this_step`), asserts that the convoy is not in `ROUTING_1` or `ROUTING_2`, reserves where the working method
  allows, and sets `convoi_t::next_stop_index`.
- `rail_vehicle_t::enter_tile`: the leading vehicle reserves the tile it has entered with `schiene_t::reserve(cnv->self,
  get_direction())` (block type) unless reversing. Every vehicle crosses the tile but only the leading vehicle reserves it and only the
  last vehicle releases it (`rail_vehicle_t::leave_tile`), so every occupied tile stays reserved while any part of the train is on it.
- `rail_vehicle_t::set_convoi` re-reserves the route with `block_reserver` during loading when the leading vehicle's
  `next_reservation_index` is 0; `rail_vehicle_t::~rail_vehicle_t` and `rail_vehicle_t::release_target_reservations` release ahead of
  teardown and of re-routing; `rail_vehicle_t::activate_choose_signal` (the choose-signal path, in which the signal selects an alternative
  route beyond itself) reserves a candidate route with `count = 100000` and commits it with `convoi_t::update_route` only if the
  reservation succeeds.

Two rules matter for consist recombination ([consist-recombination](../consist-recombination.md)). First, a
standing train stops the reservation: if a tile is reserved by another convoy whose front
vehicle stands on that tile, the walk sets `reserving_beyond_a_train`, sets `success = false` and stops; if the tile is merely reserved by
another convoy, `schiene_t::reserve` returns false and the ordinary failure path applies. Second, call-on is the only admission to an
occupied block: in `rail_vehicle_t::can_enter_tile`, when a train is at a stand under absolute block, track circuit block or cab
signalling, the signal is permissive (`get_permissive()`) and `signal_t::no_junctions_to_next_signal` is true, a failed reservation sets
`roadsign_t::call_on`, switches the train to `drive_by_sight` and re-runs `block_reserver` with `from_call_on = true`; a failed
reservation under `from_call_on` keeps the rest of the walk in `drive_by_sight` instead of stopping the train.

Under `token_block` and `one_train_staff`, `rail_vehicle_t::leave_tile` calls `schiene_t::set_stale()` instead of `schiene_t::unreserve`,
and `rail_vehicle_t::clear_token_reservation` releases the stale tiles when the train passes the exit signal, walking the route backwards
and stopping after the second run of tiles not reserved by this convoy. If the route was recalculated meanwhile, `convoi_t::calc_route`
sets `needs_full_route_flush`, and the next `clear_token_reservation` visits this convoy's reservation bucket for its stale reservations
instead of walking the route.

## Releasing reservations

Reservations must be released behind the train so that following trains can use the block. The
common path is the per-tile release in `rail_vehicle_t::leave_tile`; the broad release below is
for teardown, depot entry, consist changes and recovery from a failed reservation.

`convoi_t::unreserve_route()` releases every reservation in this convoy's bucket and also clears `needs_full_route_flush`. Callers:
`convoi_t::enter_depot`, `convoi_t::destroy`, `rail_vehicle_t::release_target_reservations` and `rail_vehicle_t::block_reserver` (token
block and one train staff), `rail_vehicle_t::unreserve_station` (fallback), `convoi_t::commit_recombined_consist` and
`tool_clear_reservation_t::work`. The halt-deletion path instead calls `convoi_t::reserve_own_tiles(true)`. The narrower paths:

- `rail_vehicle_t::unreserve_in_rear()` walks the route backwards from the vehicle's `route_index` and releases the tiles this convoy
  holds.
- `rail_vehicle_t::unreserve_station()` walks backwards through the stop's tiles with `grund_t::get_neighbour`; when the route no longer
  matches the position it falls back to `convoi_t::unreserve_route()` plus `convoi_t::reserve_own_tiles()`.
- `rail_vehicle_t::leave_tile()` releases the tile being left with `schiene_t::unreserve(vehicle_t*)`, which clears the handle
  unconditionally (unlike `schiene_t::unreserve(convoihandle_t)`, which clears only a matching handle), except under token block and one
  train staff and while reversing.
- `convoi_t::reserve_own_tiles(bool unreserve = false)` iterates every vehicle, looks up the way of `front()->get_waytype()` on that
  vehicle's tile, and reserves or unreserves it. The reserve direction is one `ribi_type` from the first two route positions, not each
  vehicle's heading, and the result of `schiene_t::reserve` is ignored. It is used after a reversal (`convoi_t::vorfahren`), after a
  consist or working-method change (`commit_recombined_consist`, `convoi_t::set_working_method`), in the no-route states of
  `convoi_t::step`, by `release_target_reservations` and by the brute-force fallbacks.
- `convoi_t::destroy()` awaits the convoy route-finding threads, then calls `unreserve_route()`: `rail_vehicle_t::leave_tile` clears only
  the tile the last vehicle leaves, not reservations held ahead of the train or elsewhere on the convoy's route, so the broad release
  is required before the vehicles leave the map. The call is skipped while the world itself is being destroyed.

## The threaded route unreserver (retained, unused)

This is internal performance machinery, not a player-facing rule. The route-unreserve worker pool still exists, but
`convoi_t::unreserve_route()` no longer uses it: the [per-holder reservation index](reservation-index.md) clears
the convoy's bucket on the main thread. The worker pool is now unused by its only former consumer. Remove it
only after the new clearing path has been profiled and shown to be a real saving; the description below records
the retained code so that its removal can be verified. The measured cost and worker-thread attribution of the
previous map-wide sweep are in the parent article's Performance hotspots section.

- `karte_t::init_threads` creates `parallel_operations + 1` threads running `unreserve_route_threaded` and initialises
  `karte_t::unreserve_route_barrier` with `parallel_operations + 2` participants (those workers plus the main thread). After a normal
  release each worker waits at the top of its loop, scans its slice, then waits again. A worker that observes `current_unreserver == 0`
  instead executes `continue`, skipping the scan and second wait before returning to the top-of-loop wait. Do not assume balanced wait
  counts on that zero-target branch without re-auditing the complete barrier cycle.
- With `MULTI_THREAD_ROUTE_UNRESERVER` defined (the default under `MULTI_THREAD` unless `FORBID_MULTI_THREAD_ROUTE_UNRESERVER` is
  defined) and `karte_t::threads_initialised` true, `convoi_t::unreserve_route()` sets `convoi_t::current_unreserver` to its own handle
  id, waits at the barrier, waits again, then sets `current_unreserver` to 0. The first wait releases the workers into the sweep; the
  second returns only after every worker has finished.
- `convoi_t::unreserve_route_range(route_range_specification)` scans a closed index range of `weg_t::get_alle_wege()`, releases
  reservations whose `get_reserved_convoi().get_id()` equals `current_unreserver`, and returns immediately when `range.start` is at or
  beyond the way count.
- Slice bounds (`unreserve_route_threaded`, simworld.cc): with `P = get_parallel_operations()` and `max_count = way_count - 1`, `fraction
  = max_count / P`; worker `t` starts at `t * fraction`; worker `P - 1` ends at `max_count`; every other worker ends at `min((t + 1) *
  fraction - 1, max_count)`, with `(t + 1) * fraction == 0` mapped to `max_count`. Coverage is complete because worker `P - 1` always
  reaches `max_count`. Worker `P` (the extra thread required by the `+2` count) scans a further slice that overlaps worker `P - 1`, and
  when `fraction` is 0 every worker scans the whole list; overlap is harmless because releasing a matching reservation is idempotent.
- `karte_t::threads_initialised` is set at the end of `karte_t::init_threads`, which `karte_t::load` calls only after all state the
  workers read has been loaded, so loading uses the single-threaded fallback. `karte_t::destroy_threads` sets `terminating_threads` and
  trips the barrier once more.

Two observations on the retained worker path. It selects `way->is_rail_type() || way->get_waytype() == air_wt`, so it
releases rail-family ways and runways; the old single-threaded fallback selected `way->get_waytype() == wt` (the waytype
the release was requested for) and then required `way->is_rail_type()`, so for an aircraft (`wt == air_wt`) no runway
reservation was released. `convoi_t::current_waytype` is assigned in the worker path but never read. Neither path is
now used by `convoi_t::unreserve_route()`; both belong to the retained worker-pool code marked for removal.

## Threading and the single-threaded constraint

None of this is visible in play; it explains why reservations may change only in the main simulation
step and why route-finding workers may only compute routes.

Reservation state is main-thread state. Convoy workers run `convoi_t::threaded_step`, which only calls `convoi_t::drive_to`, and the
reservation release happens before they are released: `convoi_t::prepare_for_routing()` calls `front()->release_target_reservations()`
before setting `ROUTING_2`, and `convoi_t::finish_rd()` does the same for a convoy loaded in `ROUTING_2`.
`vehicle_t::release_target_reservations()` is documented (vehicle.h) as main-thread only: way reservations and halt reservation slots are
read by the main thread without synchronisation while workers run route finding, so a worker write would be a data race and a determinism
hazard. `rail_vehicle_t::calc_route`, `road_vehicle_t::calc_route` and `air_vehicle_t::calc_route` contain no reservation writes; they set
convoy fields (`next_reservation_index`, `next_stop_index`) and defer messages. `convoi_t::unreserve_route()` must not run on a worker: it
mutates the world reservation buckets and the ways in them without synchronisation; every call site is in the single-threaded step, route
release, tool or teardown path. `convoi_t::destroy()` and the `convoi_t` constructor call `karte_t::await_convoy_threads()` before
touching state the workers read.

## Saved state and determinism

A saved game must restore the reservation state a train already holds, together with the signalling
state it depends on (signal aspects, signalbox membership and working methods), so that a reloaded
train does not re-enter a block it has claimed. The fields below define that state; the rest of the
section states why a divergence matters.

The following state is saved; each named method defines the conditions that govern the fields it writes: `schiene_t::rdwr` (reservation
handle id, type and reservation direction on rail-family ways), `convoi_t::rdwr` (`next_stop_index`, `next_reservation_index`,
`needs_full_route_flush`), `rail_vehicle_t::rdwr_from_convoi` (`working_method`), `roadsign_t::rdwr` and `signal_t::rdwr_signal` (aspect,
direction, signalbox position, `no_junctions_to_next_signal`, `train_last_passed`), `signalbox_t::rdwr` (signal list), and
`runway_t::rdwr` (runway reservation holder id only). Not saved: `convoi_t::allow_clear_reservation` (set per step in
`convoi_t::drive_to`), the derived reservation buckets and back-indices, the worker state, and runway reservation type and direction.
The index is rebuilt from the saved raw holder IDs on load.

- Reservations decide whether `rail_vehicle_t::can_enter_tile` succeeds, so they change movement and therefore the synced simulation. A
  divergence surfaces as a convoy speed or position divergence (`debug_sums[0]` to `[3]`, `rands[4]` after the main sync list; the
  checklist layout is in [sync-and-determinism](../sync-and-determinism.md)); no checklist field samples reservations directly.
- Heavy mode (the network diagnostic mode selected by `env_t::network_heavy_mode`) hashes the whole game state through the savegame
  writer (`karte_t::get_gamestate_hash` → `karte_t::rdwr_gamestate`, which writes the map tiles and hence the ways), so it covers the
  saved reservation fields, not the derived index.
- Bucket clearing produces the same releases on every peer because it visits the same holder bucket and releasing is idempotent;
  bucket membership is derived from the saved way fields and does not depend on `parallel_operations` or thread timing.
- Changing what is saved, or adding state a reservation decision depends on, is change-restricted under AGENTS.md rule 5; read
  [savegame-versioning](../savegame-versioning.md) first.

## Open questions

- `convoi_t::current_waytype` is assigned in the retained worker path but never read. Was the sweep intended to be filtered by the
  calling convoy's waytype, and is the broader filter (all rail-family ways plus runways) intended instead? The worker path is no longer
  used by `convoi_t::unreserve_route()`.
- The retained single-threaded fallback cannot release runway reservations while the worker path can. Is that asymmetry intended (runways
  being re-acquired on demand) or an oversight?
- Worker `P` in `unreserve_route_threaded` scans a slice overlapping worker `P - 1`, and when `fraction` is 0 every worker scans the
  whole list. Is the duplicate work intended as a margin for the range arithmetic? The pool is retained but unused by its former consumer.
- `schiene_t::reservation_type::priority` is displayed, saved and tested but never requested, and `schiene_t::can_reserve` lets a block
  reservation through a priority holder unconditionally. Is priority working planned, or is the type vestigial?
- `rail_vehicle_t::unreserve_in_rear()` uses `schiene_t::unreserve(convoihandle_t)`, but `rail_vehicle_t::leave_tile()` uses
  `schiene_t::unreserve(vehicle_t*)`, which clears any handle. Can that overload clear another convoy's reservation on a tile the leaving
  vehicle does not hold?

## Provenance

The base reservation reference was code-verified against ex-15 @ 684d570f9; the measured hotspot figures retain their own execution
records and were not re-measured. Runway persistence, broad-clear callers, way destruction and the zero-target worker branch were checked
against ex-15 @ ed1461fce plus the working-tree call-on, reservation-index and signalling-refactor changes on 2026-11-14. The
[per-holder reservation index](reservation-index.md) records the clearing path; the retained worker pool is unused by
`convoi_t::unreserve_route()` and its removal is conditional on profiling the new path. Block reservation and the route-unreserve worker pool are Extended-era developments on
the Standard Simutrans way and signal base; coarse provenance only, per [conventions](../conventions.md).
