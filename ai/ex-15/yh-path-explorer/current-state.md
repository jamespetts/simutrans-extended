---
status: draft
verified: ex-15 @ f4f6fd2b5 (through-journey implementation); ex-15 @ a203862c1 (routing-search engine); waiting-time estimates and connexion weighting @ ex-15 @ f809a1e55 plus working-tree changes (2026-11-12)
---
# ex-15: through-journey path explorer — current state

**Covers:** the current path_explorer.{cc,h} and the schedule/convoy/halt code that the
through-journey traversal modifies. Parent design: [yh-path-explorer](../yh-path-explorer.md); runtime status
registry: [schedule-and-consists](../schedule-and-consists.md). Untagged prose is [CODE] at the
frontmatter value.

- The connexion rebuild walks each operating line or lineless convoy's schedule through
  `schedule_t::increment_index` (linear, cyclic, and the mirrored return pass). It reads the
  `set_down_only`, `pick_up_only`, `lay_over` and `discharge_payload` entry flags and, on ex-15,
  the consist-order tables derived by `schedule_t::parse_orders`
  (`get_catg_carried_from`/`..._to`, min-class variants). [CODE ex-15 @ d4893f372; master @
  dc2fc3fcd]
- Coupling entries generate direct through connexions in the connexion rebuild: `schedule_entry_t`'s
  `target_unique_entry_couple` and `couple_target_sch_is_reversed` fields, the uncouple fields,
  and the depth limit in `settings_t::max_through_coupling_depth` supply the exact source and
  destination linkage/entry/direction. The through record is a direct candidate from the boarding
  halt to the downstream halt with the boarding linkage's transport. The routing search
  consumes it as an ordinary edge, so one transport field suffices. [CODE ex-15 @ f4f6fd2b5]
- The runtime cannot yet execute a divide: `schedule_t::get_uncouple_target` is a TODO in
  schedule.h; `convoi_t::hat_gehalten` carries `// TODO: Implement logic for dividing`, and
  exchange failure remains unhandled (`// TODO: Consider what happens if the exchange itself
  fails`). Joining data is stored, editable and serialised and the schedule GUI sets the
  `couple`/`uncouple` flags, so the join path is reachable in play but unpaired and untimed
  [rechecked ex-15 @ f4f6fd2b5].
- Budget and parity: `limit_set_t` holds per-phase iteration limits that a feedback controller
  recalibrates toward `path_explorer_time_midpoint` (simuconf; default 64 ms). In network mode
  peers write local limits and `nwc_routesearch_t` broadcasts the minimum set, so all peers
  perform identical work per step. The connexion rebuild counts one iteration per linkage; the routing search counts
  origins per step. [CODE ex-15 @ a203862c1; mechanism:
  [network](../../network.md)]
- Rebuild triggers: schedule and vehicle changes call `haltestelle_t::refresh_routing` from
  simconvoi.cc and simline.cc; halt destruction calls `path_explorer_t::refresh_category` from
  simhalt.cc; a flag-only pass runs every `reroute_check_interval_steps` (simuconf; default 8192)
  from simworld.cc [CODE master @ dc2fc3fcd].
- Bound: one schedule holds at most 254 entries (`schedule_t::insert`/`append` cap, message
  "Maximum 254 stops in a schedule!"); the entry count is `uint8`
  (`tpl/minivec_tpl.h`, `schedule_t::get_count`) [CODE master @ dc2fc3fcd; same cap on ex-15].
  The mirrored expansion `(count * 2) - 2` in `phase_rebuild_connexions` already truncates for
  counts of 129 or more on assignment to `uint8`, producing an incomplete mirrored walk (no
  out-of-bounds access, because `increment_index` keeps the position in range) [CODE master @
  dc2fc3fcd].
- Matrix layout: `finished_times` and `working_times` are flat `uint32` blocks,
  `finished_next_transfers` and `working_next_transfers` flat `uint16` blocks; a cell is
  `row * count + column` in `size_t`. The routing search is a per-origin Dijkstra over the expanded
  `(halt, arriving transport)` state graph built from the full connexion-rebuild candidate list. [CODE
  ex-15 @ a203862c1; engine: [path-explorer-optimisation](../../path-explorer-optimisation.md)]
- Journey times come from per line/convoy `average_journey_times` keyed by halt
  pair, booked in `convoi_t::laden` from the interval between the booked
  departure at the previous stop and the arrival here; because departure is
  booked at departure, the recorded leg time excludes the dwell at the stop. A
  distance-and-speed estimate replaces the table when no history exists.
  Waiting time comes from `haltestelle_t::get_average_waiting_time`, which
  pools retained samples with current-wait snapshot observations, or uses the
  airport/service-frequency fallback when neither source has observations
  (mechanism: [waiting-time estimates](../../routing-and-scheduling.md)).
  Walking transfer time comes from `haltestelle_t::calc_transfer_time`
  (`get_transfer_time` / `get_transshipment_time`). The connexion weight is
  `waiting + journey + transfer + comfort_cost`; comfort cost is zero for
  non-passenger categories and when comfort routing is disabled.
