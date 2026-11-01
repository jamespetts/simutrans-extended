---
status: draft
verified: ex-15 @ 1c8d9cadf
---
# Vehicles, consists & convoys

**Covers:** vehicle/, bauer/vehikelbauer.*, convoy.cc/h + convoihandle_t.h, simconvoi.*, simdepot.*, simline.* + linehandle_t.h, simlinemgmt.*, dataobj/consist_order_t.* (ex-15 only), gui/consist_order_gui.*, gui/components/gui_convoy_assembler.* (→ [gui](gui.md)), descriptor/vehicle_desc.*, descriptor/writer/vehicle_writer.cc, descriptor/reader/vehicle_reader.cc.

## Initial facts

- This is the most-churned area of ex-15: the convoy system (`simconvoi.cc`) and consist ordering (`gui/consist_order_gui.cc`) dominate the branch's own commit history [CODE].
- `dataobj/consist_order_t.{cc,h}` exists only on ex-15 (added vs. master) — consist ordering is the principal in-progress ex-15 feature here [CODE].
- `simconvoi.h` is widely included across the codebase [CODE].

## Mirrored schedules and skipped depots (ex-15)

- `convoi_t::advance_schedule` skips `conditional_skip` entries (a depot is skipped unless `is_maintenance_needed()`).
- `convoi_t::turn_at_skipped_depot()` (top of `hat_gehalten`) handles skipped depots at the **start or end of a
  mirrored schedule only**: if all entries up to the turning point are skipped depots, it sets `reverse_schedule` to
  the post-turn direction, so the current stop acts as the schedule end and is served once. It needs 3+ entries and
  does nothing for mid-schedule skips (plain advancing is correct there) or skipped non-depot stops.
- Loading depends on it: `haltestelle_t::fetch_goods` loads nothing for a stop the convoy returns to.
- Decided once on arrival, after `laden()`'s journey-time statistics (keyed on arrival direction). [CODE ex-15 @ 7534ddc74]

## Depot maintenance visits (ex-15)

- A scheduled depot entry with `conditional_skip` is visited only when `is_maintenance_needed()`
  (`convoi_t::advance_schedule`); `enter_depot` converts the flag into `maintain_or_overhaul`.
- Urgently overdue convoys set no_load and divert to a depot via `emergency_go_to_depot(false, true)`
  at the first stop after the threshold (`convoi_t::step`); see
  [ex-15 economy registry](ex-15/economy-and-vehicles.md).
- `depot_t::convoi_arrived`: `delete_entry` removes the temporary schedule entry; only a `store`
  visit detaches the convoy from its line — a maintain/overhaul visit keeps it and resumes at the
  current schedule entry. [CODE ex-15 @ e440e8710]

## Depot storage & convoy teardown [CODE ex-15 @ 343b6b9cb]

- `depot_t` (simdepot.*) holds two things: `vehicles` (`slist_tpl<vehicle_t*>` of stored,
  un-convoied vehicles) and `convois` (convoys assembled inside the depot, state INITIAL).
  Stored vehicles are NOT on the map: they carry `obj_t::not_on_map` ("e.g. vehicles in a
  depot"); `depot_t::append_vehicle` sets the vehicle's pos to the depot when moving it into a
  convoy. Per-vehicle ownership is preserved through all depot list operations.
- Suitability: `depot_t::is_suitable_for(vehicle, traction_types)` (checks same owner and
  waytype inside, plus suitable traction); `depot_t::find_depot(start, depot_type, player, next)`
  is a next/previous cycle finder used by depot-window navigation and `emergency_go_to_depot`,
  NOT a nearest-depot search. Nearest by straight line:
  `depot_t::find_nearest_depot(pos, vehicle)` (used for displaced convoys' home depots and the
  loose-vehicle evacuation on stop deletion); `convoi_t::go_to_depot` selects its target by
  route search.
- Convoy → stored vehicles: `depot_t::disassemble_convoi(cnv, sell)` loops
  `cnv->remove_vehicle_at(0)` (`discard_cargo`, `set_leading(false)`, `set_last(false)`,
  append to `vehicles`), removes the convoy from the depot lists and calls
  `cnv->self_destruct()`. Single-vehicle variants: `remove_vehicle`/`remove_vehicles_to_end`
  (convoy → `vehicles`); `append_vehicle` (`vehicles` → convoy); `sell_vehicle`
  (`book_new_vehicle(sale value)`, `book_vehicle_number(-1)`, `before_delete()`, delete).
- `convoi_t::remove_vehicle_at(i)` only detaches (`set_convoi(NULL)` + array compaction +
  `recalc_metrics`); it does NOT remove the vehicle from the map or release reservations.
- On-map convoy teardown (`convoi_t::self_destruct` → `destroy()` next step; `destroy()` awaits
  convoy threads, then per vehicle: `set_last(true)`, `leave_tile()` (releases crossings and
  removes the object from the ground), crossing release, `set_flag(not_on_map)`,
  `discard_cargo()`, `cleanup(owner)`, `delete`), booking `owner->book_new_vehicle(
  calc_sale_value())` once and `book_vehicle_number(-1)` per vehicle. `book_convoi_number(-1)`
  is booked only `if (front())` — an emptied convoy books nothing (matching: convoys created
  outside the depot tools book no `+1`). `calc_sale_value()` is the sum over vehicles, so an
  emptied convoy books 0. `leave_tile()` does NOT release `schiene_t` reservations: `destroy()`
  calls `unreserve_route()` to clear them (the waytype falls back to the schedule when `front()`
  is absent, for an emptied convoy) and deregisters the convoy from its stop's layover pool
  (removal attempted unconditionally).
- Per-km cost: `vehicle_t::get_running_cost(welt)` is the wear- and inflation-adjusted per-km
  figure; driving books it via `convoi_t::add_running_cost` → `player_t::book_running_costs`
  (negative amounts = cost; also books way tolls and convoy-level CONVOI_* accounts).
  Distance recording for movement is `convoi_t::increment_odometer` (per `steps_per_km`):
  `v.step_km(km)` advances `km_since_new` / `km_since_last_overhaul` /
  `km_since_last_maintenance` / `km_since_last_replenish` (which drive maintenance-due
  (`vehicle_t::is_maintenance_needed`), overdue-overhaul availability/cost decay and
  replenishment range), plus `book(km, CONVOI_DISTANCE)`, `owner->book_convoy_distance`,
  `total_distance_traveled` and per-way-owner way-usage (`departures[].increment_way_distance`).

## Planned sections

- Convoy lifecycle & state machine (simconvoi/convoy split — why two files? verify).
- Consist ordering: data model, GUI, save/load (ex-15; → [ex-15](ex-15.md) registry).
- Lines vs. convoys (simline/simlinemgmt); line replacement work (ex-15 branches exist) [CODE].
- Depots (simdepot; depot GUI → [gui](gui.md)).
- Vehicle physics hooks: braking/acceleration (Extended-specific — verify scope) [PRIOR → verify].
- Descriptors & makeobj side → [data-and-pak](data-and-pak.md).
- Load/save coupling → [savegame-versioning](savegame-versioning.md).

## Performance hotspots

Measured on the gargantuan fixture (method and full inventory: [performance](performance.md))
[EXECUTION-VERIFIED:2026-09-10 master @ d40847e90]:
- Convoy physics/stepping is a top cost: `convoi_t::sync_step` 22.4% incl; `calc_acceleration`
  18.9% incl / 8.3% self; `vehicle_base_t::do_drive` 10.0% incl; `convoy_t::calc_move` 6.1% incl;
  `calc_min_braking_distance` 3.0% incl; the sync-safe fixed-point `float32e8_t` operators ~7%
  self combined.
- Reservation clearing (`convoi_t::unreserve_route_range`) is 12.4% self on master, roughly 26%
  on ex-15 (single run) → [signals-and-blocks](signals-and-blocks.md).
- Loading a large save triggers a mass reroute wave in the first steps.

## Open questions

- How complete is consist ordering on ex-15 (registry will answer, code-first)?
