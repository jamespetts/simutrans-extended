---
status: stub
verified: none
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
