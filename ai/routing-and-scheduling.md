---
status: draft
verified: ex-15 @ 1c8d9cadf; waiting-time estimates @ ex-15 @ f809a1e55 plus working-tree changes (2026-11-12); service-frequency computation @ ex-15 @ f74322b648 (2026-11-24); schedule-spacing link and save conversion @ ex-15 @ 55c91fcd7b (2026-11-27)
---
# Routing, halts & scheduling

**Covers:** path_explorer.*, simhalt.* + halthandle_t.h, dataobj/schedule.*, dataobj/schedule_entry.h, gui/schedule_gui.*, gui/schedule_list.* (→ [gui](gui.md)), route-finding classes (location to verify), finder/ (→ [world-and-ground](world-and-ground.md)).

## Initial facts

- The schedule system (`dataobj/schedule.*`, `schedule_entry.h`) and its GUI (`gui/schedule_gui.cc`) are heavy ex-15 hotspots, alongside `path_explorer.cc` and `simhalt.cc` [CODE].
- `simhalt.h` is widely included [CODE].
- The schedule system is under heavy active revision on ex-15 → [ex-15](ex-15.md) registry.

## Schedule spacing

- The timetable frequency mechanism (`schedule_t::spacing`, per-entry `spacing_shift`,
  departure slots, queue position, timetable projection, service-frequency coupling and
  save/network format): [schedule-spacing](routing-and-scheduling/schedule-spacing.md). Read
  it before touching spacing, departure timing at timed stops, or any feature that derives
  the number of convoys on a line from a service frequency.

## Halts (haltestelle_t)

- Handles: `halthandle_t` quickstones; registry `alle_haltestellen`; a halt owns its tiles
  (`add_grund`/`rem_grund`), cargo arrays per goods category, connexion tables, waiting times,
  registered lines/convoys and coverage marking [CODE ex-15 @ 343b6b9cb].
- Lifecycle [CODE ex-15 @ 343b6b9cb]:
  - `haltestelle_t::create(koord, player)` factory; stops are created/extended by building stop
    buildings (each stop tile joins the halt).
  - `haltestelle_t::remove(player, koord3d)` (static) removes ONE tile (called from the removal
    tool paths: `grund_t` removal, `hausbauer_t::remove`, `brueckenbauer_t`); removes the station
    building via `hausbauer_t::remove` when the tile carries one, and calls `destroy()` only when
    the last tile goes (`!halt->existiert_in_welt()`).
  - `haltestelle_t::destroy(halthandle_t)` (static) books `book_stop_number(-1)` and deletes the
    halt object. It is called ONLY from `remove()` (player/world tile removal) and
    `destroy_all()`; `destroy_all()` is world teardown (game end/new world) and must not carry
    gameplay side effects.
  - There is no halt-merge: two overlapping stops never merge (only cargo entries merge
    internally, `merge_ware`).
- The layover registry ("loose-vehicle pool"): `laid_over` is an `slist_tpl<convoihandle_t>` of
  convoys in `convoi_t::LAYOVER` at the halt (`add_laid_over`/`remove_laid_over`/`get_laid_over`).
  Registered by `convoi_t::enter_layover` (which also sets the `min_layover_overhead_seconds`
  wait) and removed by `exit_layover`. Two sources [CODE ex-15 @ 343b6b9cb]:
  1. a scheduled consist at an entry flagged `lay_over` where the halt `can_lay_over()`
     (forces full cargo discharge first); and
  2. "loose" convoys assembled by `convoi_t::commit_recombined_consist` from displaced vehicles
     (`new convoi_t(owner)`, `set_home_depot` copied, `set_blank_route`, `reserve_own_tiles`,
     `enter_layover`, empty schedule) — see [ex-15 schedule-and-consists](ex-15/schedule-and-consists.md).
  Consumer: `convoi_t::process_consist_order` draws pool vehicles (skipping convoys in
  MAINTENANCE/OVERHAUL/REPLENISHING/SHUNTING states and mothballed vehicles). Per-vehicle
  `loose_recipient_*` fields record who may take a released vehicle; the eligibility check at
  pickup is not yet written.
  Consistency: the pool mirrors exactly the convoys in LAYOVER standing at the stop — the
  SHUNTING transition and departures call `exit_layover()`, `convoi_t::destroy()` and the
  emptied-joining-convoy path deregister, and `karte_t::load` repairs the pools
  (`haltestelle_t::validate_laid_over` plus re-registration of LAYOVER convoys); pool iteration
  is defensive about unbound/stale handles.
- Stop deletion with a pool: `haltestelle_t::destroy` calls `evacuate_loose_vehicles()` (skipped
  at world teardown, `karte_t::is_destroying`): each vehicle is moved into the nearest depot of
  the right type owned by the vehicle's owner (charged to that owner at 1.1x the straight-line
  km at the vehicle's per-km running cost), or sold at its current value when no such depot
  exists; the emptied layover convoys `self_destruct()` after `reserve_own_tiles(true)`, and
  each affected owner is notified (dialogue and message) [CODE].
- Savegame: `laid_over` is serialised in `haltestelle_t::rdwr` (count + handles), gated on
  extended ≥ 15 [CODE ex-15 @ 343b6b9cb]; → [savegame-versioning](savegame-versioning.md).
- Halt GUI: `gui/halt_info.*` (station information window) and `gui/halt_detail.*` (station
  details window) show the stop's status, services and cargo and, where loose vehicles are
  stored, a count icon and a per-vehicle list with owners [CODE ex-15 @ 343b6b9cb].

### Waiting-time estimates

- `haltestelle_t::waiting_times` holds retained observations in a fixed 32-entry
  list per goods category, class and next-transfer halt. `convoi_t::book_waiting_times`
  records the wait of newly boarded packets when their convoy departs.
  `haltestelle_t::step` records four times the elapsed wait for a packet that
  gives up, to estimate how long it would have needed to wait for transport.
  `haltestelle_t::new_month` ages these retained lists and can clear them or add
  fallback estimates to stale entries.
- The service frequency for a (goods category, destination halt) pair is the interval
  between successive services, in tenths of a minute, that the lines registered at
  this halt provide to that destination [CODE ex-15 @ f74322b648].
  `haltestelle_t::calc_service_frequency()` folds one interval per registered line that
  carries the category and whose schedule contains the destination, through
  `combine_service_frequency()`: the first line's interval becomes the value, and each
  later line shortens it by merging the two intervals (two equal intervals halve).
  `calc_line_timing()` supplies a line's round-trip time (the sum of the line's per-leg
  average journey times over its schedule; a leg without a recorded average uses the
  straight-line distance at the line's recorded average speed, or at half the first
  convoy's minimum top speed when no average speed is recorded) and the number of calls
  at this halt. `calc_line_interval()` divides the round-trip time by `count_convoys()`
  and by the number of calls at this halt, raises the result to the minimum interval
  implied by the schedule spacing when the spacing setting is greater than zero, and
  floors it at 1. The value is per category only: classes of the same category share it.
- `get_service_frequency()` is the entry point used by callers that do not compute the
  value themselves: it returns the `service_frequencies` cache value when present and
  otherwise calls `calc_service_frequency()` [CODE ex-15 @ f74322b648].
  The cache is filled by `get_fallback_waiting_time()`, which caches every value that it
  computes because `ALWAYS_CACHE_SERVICE_INTERVAL` is defined in simhalt.h; `add_line()` and
  `add_convoy()` call `clear_service_intervals()` to remove the entries for the
  destinations in the new schedule, `remove_line()` and `remove_convoy()` call
  `update_service_intervals()` to recompute and cache them, and `haltestelle_t::rdwr`
  clears the whole table on load.
- The same values are also computed in bulk during the month change: for every halt,
  `karte_t::new_month()` calls `haltestelle_t::new_month()`, which calls
  `build_service_frequency_batch()` before the loop over the waiting-time lists. That
  function makes one pass over this halt's `registered_lines` and fills
  `batch_service_frequencies`: for each line, one call to `calc_line_timing()` and one to
  `calc_line_interval()`, then one `combine_service_frequency()` fold per (category, halt)
  pair in that line's schedule [CODE ex-15 @ f74322b648]. The per-line
  interval does not depend on the destination, so the pass computes it once per line. While
  the table exists, `get_service_frequency()` returns the cache value when present, otherwise
  the table value, otherwise 0, which is the value that `calc_service_frequency()` also
  returns for a pair that no registered line carries and serves. `haltestelle_t::new_month()`
  deletes the table after the loop, so the table exists only for the duration of the update
  and the pointer is NULL at all other times. During the update the lookup therefore never
  reaches the "Unknown frequency" `dbg->message` or a schedule walk; at all other times the
  lookup falls through to the message and `calc_service_frequency()`.
- `get_base_average_waiting_time()` returns the retained-sample mean when
  samples exist. Otherwise, `get_fallback_waiting_time()` supplies half the
  calculated service frequency, subject to the airport minimum and the
  airstop assumption.
- The current-wait snapshot contains replaceable observations from packets
  whose elapsed wait exceeds twice this base mean. On each 256th call to a
  halt's `step()`, `haltestelle_t::current_waiting_times` is cleared and
  rebuilt with the exact sum and count of qualifying packets, grouped by this
  halt, the packet's next-transfer halt, goods category and class. An included
  observation uses the greater of elapsed wait and airport minimum. These
  observations are not appended to retained history and do not alter its month
  counter or their own base mean.
- `get_average_waiting_time()` divides the combined retained and current-wait
  sums by their combined counts. If neither source has observations, it returns
  the fallback estimate. Ageing decisions in `new_month()` use the base mean,
  excluding the snapshot.
- Snapshot storage is sparse: each halt owns one pointer, and the map is
  allocated only while qualifying observations exist. The map is deleted when
  a poll produces no observations. Halt destruction removes entries referring
  to the destroyed halt after waiting for path-explorer workers to finish.
- A snapshot describes the queue at its last poll. Departure and give-up
  samples enter retained history immediately, but the last snapshot remains
  until the next scheduled poll. Snapshots are not serialised: after loading,
  a halt has no snapshot observations until its next poll, while retained
  history and any saved path-explorer matrix remain available.

## Planned sections

- Routing pipeline: passenger & goods routing; where the route-search implementation is (path_explorer vs. a route class — verify, do not assume).
- path_explorer: role and interaction with the main loop; threading model documented in (→ [threading](threading.md)).
- Connection scoring: Extended criteria (journey time, congestion, comfort, classes — verify which exist and where) [PRIOR → verify].
- Halts: catchments, coverage, platform/stop mechanics (Extended-specific elements to identify).
- Schedule system & the ex-15 overhaul (intent + implemented state, code-first).
- Load/save coupling → [savegame-versioning](savegame-versioning.md).

## Performance hotspots

Measured on the gargantuan fixture (method and full inventory: [performance](performance.md))
[EXECUTION-VERIFIED:2026-09-10 master @ d40847e90]:
- A* route search (`route_t::intern_calc_route`) is minor in steady state (`route_t::find_route`
  ~1.2% self) but there is a mass reroute wave right after loading large saves; heuristic-failure
  diagnostics (`heur` ~10x `cost`) fire continuously at -debug >= 2.
- The path explorer runs concurrently on its own worker and only when a refresh has been
  requested; its per-step work is budget-capped in origins (`path_explorer_time_midpoint`). It
  governs how quickly in-game routes update, not framerate. The routing search is a per-origin
  Dijkstra over the expanded `(halt, arriving transport)` state graph built from the full
  connexion-rebuild candidate list; the matrices remain O(H²). Engine, measurements and options →
  [path-explorer-optimisation](path-explorer-optimisation.md).
- Halt cargo handling: `karte_t::check_transferring_cargoes` 2.6% self.
- Month change: on the month-change fixture (`month-change-test.sve`, defined in
  [performance](performance.md)), `haltestelle_t::new_month()` takes 41-42 ms of the 172-178 ms
  `karte_t::new_month()`, and its waiting-times ageing loop 36 ms of that
  [EXECUTION-VERIFIED:2026-11-24 ex-15 @ f74322b648]. Full phase breakdown:
  [performance](performance.md).

## Open questions

- Is there a `route` class distinct from path_explorer, and who calls which?
- What exactly changed in schedule_entry on ex-15?
