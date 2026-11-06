---
status: draft
verified: ex-15 @ bf447619a
---
# ex-15: Y/H path explorer — code-verified current state

**Covers:** the pre-implementation state of path_explorer.{cc,h} and the schedule/convoy/halt code
that the Y/H traversal design modifies. Parent design: [yh-path-explorer](../yh-path-explorer.md);
runtime status registry: [schedule-and-consists](../schedule-and-consists.md). Inline provenance
records retain their original commits; untagged prose is [CODE] at the frontmatter value.

- The explorer computes connexions within one schedule only. `phase_rebuild_connexions` walks each
  operating line or lineless convoy's schedule through `schedule_t::increment_index` (linear,
  cyclic, and the mirrored return pass). It reads only the `set_down_only`, `pick_up_only`,
  `lay_over` and `discharge_payload` entry flags and, on ex-15, the consist-order tables derived
  by `schedule_t::parse_orders` (`get_catg_carried_from`/`..._to`, min-class variants)
  [CODE ex-15 @ d4893f372; CODE master @ dc2fc3fcd].
- No code path in path_explorer.{cc,h} on ex-15 reads `target_id_couple`, `target_id_uncouple`, or
  `target_unique_entry_uncouple`; a search of the ex-15 files for couple/uncouple/divide returns
  nothing in those two files [CODE ex-15 @ d4893f372; rechecked ex-15 @ bf447619a]. Journeys
  through a split or join are therefore neither discovered nor time-estimated.
- The runtime cannot yet execute a divide: `schedule_t::get_uncouple_target` is a TODO in
  schedule.h; `convoi_t::hat_gehalten` carries `// TODO: Implement logic for dividing`, and
  exchange failure remains unhandled (`// TODO: Consider what happens if the exchange itself
  fails`). Joining data is stored, editable and serialised and the schedule GUI sets the
  `couple`/`uncouple` flags (bt_couple/bt_uncouple), so the join path is reachable in play but
  unpaired and untimed [rechecked ex-15 @ bf447619a].
- Budget and parity: `limit_set_t` holds per-phase iteration limits that a feedback controller
  recalibrates toward the configured `path_explorer_time_midpoint` (simuconf; default 64 ms). In
  network mode peers write local limits and `nwc_routesearch_t` broadcasts the minimum set, so all
  peers perform identical iteration counts. Phase 2 counts one iteration per linkage; phase 5
  counts one per origin row [CODE master @ dc2fc3fcd; mechanism: [network](../../network.md)].
- Rebuild triggers today: schedule and vehicle changes call `haltestelle_t::refresh_routing`
  from simconvoi.cc and simline.cc; halt destruction calls `path_explorer_t::refresh_category`
  from simhalt.cc; a flag-only pass runs every `reroute_check_interval_steps` (simuconf; default
  8192) from simworld.cc [CODE master @ dc2fc3fcd].
- Bound: one schedule holds at most 254 entries (`schedule_t::insert`/`append` cap, message
  "Maximum 254 stops in a schedule!"); the entry count is `uint8`
  (`tpl/minivec_tpl.h`, `schedule_t::get_count`) [CODE master @ dc2fc3fcd; same cap on ex-15].
  The mirrored expansion `(count * 2) - 2` in `phase_rebuild_connexions` already truncates for
  counts of 129 or more on assignment to `uint8`, producing an incomplete mirrored walk (no
  out-of-bounds access, because `increment_index` keeps the position in range) [CODE master @
  dc2fc3fcd].
- Matrix layout: `path_element_t` is an array-of-structs cell of `uint32 aggregate_time` plus a
  halt handle; `transport_element_t` holds two `uint16` transport identifiers; the matrices are
  allocated as arrays of row pointers. The phase-5 relaxation is gather-bound as written: origin and
  target halt indices come from per-transport cluster member vectors and index the matrices at
  scattered addresses [CODE master @ dc2fc3fcd; reformulation:
  [path-explorer-optimisation](../../path-explorer-optimisation.md)].
- Journey times come from per line/convoy `average_journey_times` keyed by halt pair, booked in
  `convoi_t::laden` from the interval between the booked departure at the previous stop and the
  arrival here; because departure is booked at departure, the recorded leg time excludes the dwell
  at the stop. A distance-and-speed estimate replaces the table when no history exists. Waiting
  time comes from `haltestelle_t::get_average_waiting_time` (history mean, else half the service
  frequency); walking transfer time comes from `haltestelle_t::calc_transfer_time`
  (`get_transfer_time` / `get_transshipment_time`). The connexion aggregate used to fill the
  matrix is `waiting + journey + transfer` [CODE master @ dc2fc3fcd].
