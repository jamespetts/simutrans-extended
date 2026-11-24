---
status: reviewed
verified: ex-15 @ 0d41455e7; maintenance article and status ex-15 @ 2777c3800a plus working-tree changes (2026-11-24)
---
# ex-15 feature status: economy, vehicles & replacer

**Covers:** descriptor/vehicle_desc.*, vehicle/vehicle.*, vehicle/air_vehicle.cc, simdepot.cc,
simconvoi.cc (maintenance/replace/finance), dataobj/replace_data.h, gui/replace_frame.*,
gui/convoi_detail_t.*, gui/prices_frame.*, simworld.{cc,h} (prices/fuel), player/finance.cc, dataobj/settings.*
(maintenance intervals), descriptor/{reader,writer}/vehicle_*.cc, building_desc (depot capacity).

Per-feature registry for the 15.x programme. Intent sources: forum threads 16980/17852/14991/22054
(post URLs inline); parent doc: [ex-15](../ex-15.md). Untagged claims are [CODE] at the frontmatter
value.

Detailed routine-maintenance status, defect links and outstanding design decisions:
[depot-maintenance](depot-maintenance.md) — read when working on availability, depot visits,
maintenance queues, maintenance duration, overhauls or overhaul finance on ex-15.

## Usage-based (wear) maintenance — IMPLEMENTED

- Intent: running cost rises with km since last overhaul (integer sigmoid), capped at
  max_running_cost; availability decays on the same curve
  [FORUM:https://forum.simutrans.com/index.php/topic,16980.msg161553.html,
  https://forum.simutrans.com/index.php/topic,17852.msg200693.html].
- .dat parameters: runningcost, max_running_cost (reader default running_cost×3),
  availability_decay_start_km, max_distance_between_overhauls, starting_availability,
  minimum_availability, maintenance_interval_km; aircraft analogues: max_takeoffs,
  availability_decay_start_takeoffs (`air_vehicle_t::get_availability` on takeoff cycles).
- `vehicle_t::get_running_cost(welt)` / `get_availability()`: sigmoid interpolation between the
  bounds (integer fixed-point `sigmoid()` in utils/simrandom.h — see below). Both are
  inflation-adjusted: the underlying `vehicle_desc_t::get_running_cost/get_max_running_cost`
  use `karte_t::get_inflation_adjusted_price`.
- Booking uses the wear-adjusted cost: `convoi_t::increment_odometer` bills
  `vehicle_t::get_running_cost` (not the descriptor-level cost), so the usage-based increase reaches
  the finances.
- The curve is driven by the integer fixed-point `sigmoid()` (utils/simrandom.h): a cubic
  smoothstep on [0, upper_bound] returning [0, `SIGMOID_SCALE`] (`SIGMOID_SCALE = 100000`),
  integer-only so it is network-safe (→ [sync-and-determinism](../sync-and-determinism.md)).
  Every caller divides the result by `SIGMOID_SCALE` to map the range, so the scale is shared
  rather than duplicated. `get_availability`/`get_running_cost` interpolate from the decay-start
  distance to `max_distance_between_overhauls`; the aircraft analogue interpolates from
  `availability_decay_start_takeoffs` to `max_takeoffs` [EXECUTION-VERIFIED:2026-09-20, exact
  fixed-point values and saturation checked by a transient test].
- REQUIREMENT: no booking of cost or revenue may use base-level .dat prices, or any function of
  them, that bypasses inflation adjustment [RECOLLECTION:2026-09-07]. A full audit of all booking
  paths against this requirement is outstanding (open question below).

## Vehicle overhauls — PARTIAL

- .dat: initial_overhaul_cost (reader default base_cost/5), max_overhaul_cost,
  overhauls_before_max_cost, max_distance_between_overhauls, max_takeoffs, overhaul_month_tenths
  (depot time; writer defaults per waytype/engine), auto_upgrade_index.
- `vehicle_t::overhaul()`: resets km counters and the active-service counter, increments overhaul
  count, calculates the escalating cost before the increment (sigmoid over overhauls_before_max_cost
  between the inflation-adjusted initial and max costs), charges the negated cost through
  `player_t::book_vehicle_maintenance_one_off()` (no monthly scaling), applies auto-upgrade
  (`get_auto_upgrade` → set_desc; upgrade price used instead; preserves maintenance history), then
  `update_livery()` to the latest available livery in the current scheme — own vehicles only: a
  vehicle owned by another player in a mixed-owner consist keeps its livery
  ([schedule-and-consists](schedule-and-consists.md)). `overhaul_time` is updated for display.
- Affordability: `vehicle_t::can_afford_overhaul()` uses `finance_t::can_afford()` for the
  overhaul or upgrade price. A scheduled conditional depot entry is skipped before routing when an
  overhaul is unaffordable and the convoy is not urgently overdue; an urgently overdue convoy enters
  the depot, performs no unaffordable overhaul and remains there in `INITIAL`.
- Deferral: do_not_overhaul per-vehicle flag (excluded at depot); overdue penalty = running cost
  stays at max and availability at minimum beyond the interval, lengthening the next maintenance.
- Depot integration: `depot_t::convoi_arrived` sorts vehicles into vehicles_to_overhaul /
  vehicles_to_maintain; convoy states OVERHAUL / MAINTENANCE.
- GUI: convoy detail maintenance tab — per-vehicle rows (last overhaul date/count, km/takeoffs
  since, three-segment overhaul bar, cost + delta %, do_not_overhaul and do_not_auto_upgrade
  toggles, auto-upgrade target).
- Gaps: a separate overhaul financial category remains outstanding; the payment uses
  `ATV_VEHICLE_MAINTENANCE`. Livery fallback to an available scheme, the cargo policy at a
  maintenance visit, the per-line depot limit and the open calibration question about independently
  timed vehicles are detailed in [depot-maintenance](depot-maintenance.md). Overhaul/replacement
  integration is future work.

## Depot visits & availability — PARTIAL

- The depot time depends on active-service time and availability. `vehicle_t::maintain()` uses
  `active_service_time * (100 - availability) / 100`; `convoi_t::sync_step()` accumulates that time
  while the convoy is in an active state, and `vehicle_t::rdwr()` saves the counter under Extended
  save revision 21. `convoi_t::enter_depot()` and `convoi_t::ziel_erreicht()` preserve the wait.
  The settled rules, remaining decisions and resumption order are in
  [depot-maintenance](depot-maintenance.md). Maintenance is non-interruptible (`convoi_t::start`
  proceeds only from INITIAL/ROUTING states).
- Depot queue: `depot_t::under_maintenance`, register_for_maintenance (time multiplier when over
  capacity), is_awaiting_attention, queue rdwr gated extended ≥ 15; capacity .dat
  max_vehicles_under_maintenance (default 4, building_tile_desc_t).
- Triggers: maintenance_interval_km per vehicle, else months-based settings
  (maintenance_interval_months default 12, extended_maintenance_interval_months default 18);
  simplified_maintenance setting disables the whole system.
- Skippable scheduled depot entry: `conditional_skip` on a depot entry skips it unless
  `is_maintenance_needed()`. `convoi_t::enter_depot` adds `maintain_or_overhaul` to the visit flags
  for such entries (no stored per-entry flag), so `depot_t::convoi_arrived` overhauls
  (`is_overhaul_needed()`, not `do_not_overhaul`) or maintains each vehicle and resets its counters.
  Entries are auto-appended with `conditional_skip` only (`convoi_t::create_schedule`, line-from-depot tool).
- After maintenance, `convoi_t::step` advances the schedule past the depot entry (if current) and
  sets `ROUTING_1`; `LEAVING_DEPOT` follows via `depot_t::start_convoi`.
- Stepping home: convoys in `MAINTENANCE`/`OVERHAUL` are kept out of `welt.sync` and stepped once
  via `depot_t::sync_step`; `convoi_t::start` re-adds (remove-before-add; `sync_list_t::remove`
  clears all occurrences). `validate_maintenance_queue` pairs queues with convoys at load (drops
  stale entries, re-queues missing ones without touching `wait_lock`).
- Old-save warts (no migration; few affected saves): schedules written before the flag fix keep a
  spurious `ignore_choose` bit on auto-appended depot entries; pre-15 saves have no maintenance
  queue (rebuilt by the validator).
- Urgent maintenance — IMPLEMENTED: `is_maintenance_urgently_needed()` (1.5× km interval, or past
  `extended_maintenance_interval_months`) sets no_load and diverts to a depot via
  `emergency_go_to_depot(false, true)` at the first stop after the threshold (`convoi_t::step`);
  the existing teleport/no-depot fallbacks apply. A maintain/overhaul visit keeps the line and
  resumes at the current schedule entry; only `store` visits detach it (`depot_t::convoi_arrived`).
- Status text: `MAINTENANCE`, `OVERHAUL` and `REPLENISHING` display a translated status and the
  remaining wait in the convoy information window.
- Gaps: `prioritise_for_maintenance` / `depriortise_for_maintenance` have no callers — depot queue
  controls are future user-interface work. The cargo policy at a maintenance visit and the
  per-line depot limit are open questions in [depot-maintenance](depot-maintenance.md).

## Staff costs — IMPLEMENTED

- staff.tab: wages per staff type per year with linear interpolation; read and salary deduction
  implemented (Jun 2022 [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg200693.html]).
- Model: drivers + staff_hundredths (fractional staff in hundredths, rounded up per convoy);
  multiple_working_type lets multiple powered units need one driver (`convoi_t::book_salaries` /
  get_salaries); layover time discounts salaries (last_salary_point_ticks).
- Open: Matthew's staff-as-objects alternative has no recorded resolution
  [FORUM:https://forum.simutrans.com/index.php/topic,16980.msg200606.html].

## Fuel consumption — IMPLEMENTED

- fuel.tab: absolute unit prices per fuel/engine type per year (unit-agnostic);
  `karte_t::fuel_init` / get_fuel_cost (linear interpolation) / fuel_rdwr.
- `vehicle_t::calc_fuel_consumption` / consume_fuel / book_fuel_consumption; .dat fuel_per_km,
  calibration_speed, cut_off_speed (two-stage formula
  [FORUM:https://forum.simutrans.com/index.php/topic,14991.msg183022.html]).
- Design decision: static averaged calibration preferred over full dynamic simulation (dynamic-only
  would misprice low-power vehicles)
  [FORUM:https://forum.simutrans.com/index.php/topic,14991.msg174482.html].

## prices.tab: inflation, interest, corporation tax — IMPLEMENTED

- config/prices.tab: percentage factors indexed by year per price type, plus a "general" fallback;
  linear interpolation; 100 = current .dat prices. `karte_t::get_inflation_adjusted_price`;
  `price_type` enum: general, passenger_fare, mail_rate, goods_rate, vehicle_purchase,
  vehicle_maintenance, buildings, infrastructure, city_land, country_land, corporation_tax,
  base_rate.
- Applied across: vehicle running/fixed/upgrade costs & purchase; fares (goods_desc get_base_fare/
  get_total_fare); way/bridge/tunnel/signal/roadsign/wayobject maintenance & construction;
  buildings/depots/stations; all cst_* settings; corporation tax (player/finance.cc); land values;
  overdraft interest (base_rate + overdraft_percent_above_base_rate setting).
- Land purchase (`karte_t::get_land_value`): `cst_buy_land` outside city limits and
  `cst_buy_city_land` inside them (`cost_buy_land` and `cost_buy_city_land` in simuconf, both
  scaled by the pakset's meters per tile and stored negative). Inside a city the base is
  multiplied by 3 (below `city_threshold_size`), 4 (below `capital_threshold_size`) or 6 (at or
  above it); the same multiplied base feeds the wayleave deduction for elevated or underground
  ways (a wayleave is the payment for the right to pass over or under a building). The purchase
  price and the wayleave are each adjusted by the `country_land` or `city_land` price index.
- prices_rdwr gated extended ≥ 15; pakset-dir override re-read on load. Forum confirms implemented
  on 15.x [FORUM:https://forum.simutrans.com/index.php/topic,22054.msg207373.html].
- Fuel and staff sit outside prices.tab (absolute yearly prices in their own .tab files).

## Vehicle replacer enhancements — MIXED

- (a) Stored vehicles pooled across all of a player's depots — ABSENT: replace_now /
  find_oldest_newest are single-depot (home depot only); replace_frame.cc carries a comment
  acknowledging the limitation. Still required for 15.x [RECOLLECTION:2026-09-07; roadmap <15>].
- (b) Replacement without depot visits / staggered — PARTIAL: replace_data_t::replacement_time
  enum (immediate, on_replenish, on_maintenance, on_overhaul, manual, automatic), rdwr ≥ 15; all
  execution paths require the convoy to be in a depot (convoi_t::step INITIAL,
  depot_t::convoi_arrived, manual send); on_replenish has no trigger code and its intended
  location is unresolved → [range-stops-replenishment](range-stops-replenishment.md); the replace
  window sets `manual` through the temporary `bt_mark` path; staggering exists only as the
  replace/sell/skip "Replace cycle" across matching convoys.
- (c) Order-insensitive matching of identical consists — ABSENT: `convoi_t::has_same_vehicles`
  compares descriptor pointers position-by-position (forward and mirrored only). Still required
  for 15.x [RECOLLECTION:2026-09-07; roadmap <15>].
- (d) Replace all convoys on a line regardless of composition — IMPLEMENTED:
  replace_frame_t::all_convoys_of_this_line mode (line-mode constructor, magic_replace_line),
  opened from line management (schedule_list bt_replace); merged from the ex-15-line-replacement
  lineage; completion message when the line's replacing count reaches 0.
- Replacer↔overhaul interaction ("replace instead of overhaul") — execution code present but not
  reachable through the interface: depot_t::convoi_arrived executes replace_now instead of
  overhauling or maintaining when replace_at is on_overhaul or on_maintenance (remaining vehicles
  are still overhauled normally). `on_maintenance` now triggers on any maintenance visit, including
  a visit made only for overhauls; `on_overhaul` requires at least one vehicle due for overhaul. No
  graphical control sets either value, so they are reachable only through savegame or tool data.
  The convoy-trigger decision and the broader integration are future work in
  [depot-maintenance](depot-maintenance.md).

## Other vehicle features

- Mothballing — vehicle_t::mothball / un_mothball / get_is_mothballed (rdwr ≥ 15); mothballed
  vehicles excluded from re-combination pools. Simulation side implemented (Jan 2023
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg202994.html]); the flag's readers run
  (fixed-cost booking in simconvoi.cc, depot handling in simdepot.cc), but the setters
  mothball()/un_mothball() have no callers and there is no UI, so the flag cannot be set in play.
  Cost mechanics verified as intended: the vehicle is taken out of monthly fixed costs and one
  month's fixed cost is charged at each transition (`add_maintenance`/`book_vehicle_maintenance` in
  mothball/un_mothball).
- Self-contained catering — self_contained_catering .dat flag: catering serves only that vehicle
  rather than the whole consist. Implemented (Jun 2022
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg200693.html]).

## Open questions

- Inflation audit: starting-money paths are done — company endowment, takeover refund (on the
  target's founding-year basis, derived from company age), net-wealth warning threshold and the
  server-announce figure are all inflation-adjusted; founding month/year is shown in the finance
  window and player list (derived from company age, no stored datum). Every other cost/revenue
  booking path still needs verification against the requirement above [CODE ex-15 @ fd60082b6].
