---
status: reviewed
verified: ex-15 @ 91d9b252e
---
# ex-15 feature status: economy, vehicles & replacer

**Covers:** descriptor/vehicle_desc.*, vehicle/vehicle.*, vehicle/air_vehicle.cc, simdepot.cc,
simconvoi.cc (maintenance/replace/finance), dataobj/replace_data.h, gui/replace_frame.*,
gui/convoi_detail_t.*, simworld.{cc,h} (prices/fuel), player/finance.cc, dataobj/settings.*
(maintenance intervals), descriptor/{reader,writer}/vehicle_*.cc, building_desc (depot capacity).

Per-feature registry for the 15.x programme. Intent sources: forum threads 16980/17852/14991/22054
(post URLs inline); parent doc: [ex-15](../ex-15.md). Untagged claims are [CODE] at the frontmatter
value.

## Usage-based (wear) maintenance — mechanism IMPLEMENTED, booking DEFECTIVE

- Intent: running cost rises with km since last overhaul (integer sigmoid), capped at
  max_running_cost; availability decays on the same curve
  [FORUM:https://forum.simutrans.com/index.php/topic,16980.msg161553.html,
  https://forum.simutrans.com/index.php/topic,17852.msg200693.html].
- .dat parameters: runningcost, max_running_cost (reader default running_cost×3),
  availability_decay_start_km, max_distance_between_overhauls, starting_availability,
  minimum_availability, maintenance_interval_km; aircraft analogues: max_takeoffs,
  availability_decay_start_takeoffs (`air_vehicle_t::get_availability` on takeoff cycles).
- `vehicle_t::get_running_cost(welt)` / `get_availability()`: sigmoid interpolation between the
  bounds (`sigmoid()` in utils/simrandom.h — carries "TODO: Implement a proper function"). Both are
  inflation-adjusted: the underlying `vehicle_desc_t::get_running_cost/get_max_running_cost`
  use `karte_t::get_inflation_adjusted_price`.
- DEFECT (user-confirmed bug [RECOLLECTION:2026-09-07]): the only booking path
  (`convoi_t::increment_odometer` → `add_running_cost` → `player_t::book_running_costs`) uses the
  descriptor-level cost — the wear increase never reaches the finances, while the GUI
  (gui_vehicle_maintenance_t) displays the wear-adjusted value as the operative $/km. Inflation IS
  accounted for in this path; the bug is solely the omitted wear increase → [known-bugs](../known-bugs.md).
- DEFECT: the sigmoid argument is `(km_since_last_overhaul − max_distance_between_overhauls)` —
  negative throughout the active branch (unsigned wrap); the aircraft version subtracts the
  decay-start value → [known-bugs](../known-bugs.md).
- REQUIREMENT: no booking of cost or revenue may use base-level .dat prices, or any function of
  them, that bypasses inflation adjustment [RECOLLECTION:2026-09-07]. A full audit of all booking
  paths against this requirement is outstanding (open question below).

## Vehicle overhauls — IMPLEMENTED

- .dat: initial_overhaul_cost (reader default base_cost/5), max_overhaul_cost,
  overhauls_before_max_cost, max_distance_between_overhauls, max_takeoffs, overhaul_month_tenths
  (depot time; writer defaults per waytype/engine), auto_upgrade_index.
- `vehicle_t::overhaul()`: resets km counters, increments overhaul count, escalating cost
  (sigmoid over overhauls_before_max_cost between initial and max cost), books via
  book_vehicle_maintenance, applies auto-upgrade (`get_auto_upgrade` → set_desc; upgrade price
  charged instead; preserves maintenance history), then `update_livery()` to the latest available
  livery in the current scheme.
- Deferral: do_not_overhaul per-vehicle flag (excluded at depot); overdue penalty = running cost
  stays at max and availability at minimum beyond the interval, lengthening the next maintenance.
- Depot integration: `depot_t::convoi_arrived` sorts vehicles into vehicles_to_overhaul /
  vehicles_to_maintain; convoy states OVERHAUL / MAINTENANCE.
- GUI: convoy detail maintenance tab — per-vehicle rows (last overhaul date/count, km/takeoffs
  since, three-segment overhaul bar, cost + delta %, do_not_overhaul and do_not_auto_upgrade
  toggles, auto-upgrade target).
- Gaps: separate financial category + affordability check are TODOs in overhaul() — both still
  intended for 15.x [RECOLLECTION:2026-09-07]; switching to a
  NEW livery scheme when the current scheme obsoletes is a TODO; calibration open — trains may
  overhaul repeatedly because carriages differ (forum proposal: overhaul together if next overhaul
  is near).

## Depot visits & availability — IMPLEMENTED

- `vehicle_t::maintain()`: depot time = elapsed revenue-service time × 100 / availability;
  non-interruptible (convoi_t::start proceeds only from INITIAL/ROUTING states).
- Depot queue: `depot_t::under_maintenance`, register_for_maintenance (time multiplier when over
  capacity), is_awaiting_attention, queue rdwr gated extended ≥ 15; capacity .dat
  max_vehicles_under_maintenance (default 4, building_tile_desc_t).
- Triggers: maintenance_interval_km per vehicle, else months-based settings
  (maintenance_interval_months default 12, extended_maintenance_interval_months default 18);
  simplified_maintenance setting is a kill-switch for the whole system.
- Skippable scheduled depot entry: conditional_skip + depot_flag; auto-appended (with
  maintain_or_overhaul) when a convoy/line is created from a depot.
- DEFECT (user-confirmed [RECOLLECTION:2026-09-07]): urgent maintenance (1.5× interval) only sets
  no_load; the documented intent (vehicle_desc.h comment) is an emergency depot visit wherever the
  convoy is → [known-bugs](../known-bugs.md).
- Gaps: prioritise_for_maintenance / depriortise_for_maintenance have no callers — a depot
  queue-management GUI is planned for 15.x [RECOLLECTION:2026-09-07]; MAINTENANCE/OVERHAUL/REPLENISHING have no status strings in the convoy info
  window (fall through to the default speed display).

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

## prices.tab: inflation, interest, corporation tax — IMPLEMENTED; display GUI ABSENT

- config/prices.tab: percentage factors indexed by year per price type, plus a "general" fallback;
  linear interpolation; 100 = current .dat prices. `karte_t::get_inflation_adjusted_price`;
  `price_type` enum: general, passenger_fare, mail_rate, goods_rate, vehicle_purchase,
  vehicle_maintenance, buildings, infrastructure, city_land, country_land, corporation_tax,
  base_rate.
- Applied across: vehicle running/fixed/upgrade costs & purchase; fares (goods_desc get_base_fare/
  get_total_fare); way/bridge/tunnel/signal/roadsign/wayobject maintenance & construction;
  buildings/depots/stations; all cst_* settings; corporation tax (player/finance.cc); land values;
  overdraft interest (base_rate + overdraft_percent_above_base_rate setting).
- prices_rdwr gated extended ≥ 15; pakset-dir override re-read on load. Forum confirms implemented
  on 15.x [FORUM:https://forum.simutrans.com/index.php/topic,22054.msg207373.html].
- Gap: NO GUI reads karte_t::prices — the planned display of price factors/rates over time
  [FORUM:https://forum.simutrans.com/index.php/topic,22054.msg202141.html] is outstanding.
- Fuel and staff sit outside prices.tab (absolute yearly prices in their own .tab files).

## Vehicle replacer enhancements — MIXED

- (a) Stored vehicles pooled across all of a player's depots — ABSENT: replace_now /
  find_oldest_newest are single-depot (home depot only); replace_frame.cc carries a comment
  acknowledging the limitation. Still required for 15.x [RECOLLECTION:2026-09-07; roadmap <15>].
- (b) Replacement without depot visits / staggered — PARTIAL: replace_data_t::replacement_time
  enum (immediate, on_replenish, on_maintenance, on_overhaul, manual, automatic), rdwr ≥ 15; all
  execution paths require the convoy to be in a depot (convoi_t::step INITIAL,
  depot_t::convoi_arrived, manual send); on_replenish has no trigger code (replenishment itself is
  unwired → [schedule-and-consists](schedule-and-consists.md)); GUI can only set immediate or
  manual ("HACK ... temporary" comment on bt_mark); staggering exists only as the replace/sell/skip
  "Replace cycle" across matching convoys.
- (c) Order-insensitive matching of identical consists — ABSENT: `convoi_t::has_same_vehicles`
  compares descriptor pointers position-by-position (forward and mirrored only). Still required
  for 15.x [RECOLLECTION:2026-09-07; roadmap <15>].
- (d) Replace all convoys on a line regardless of composition — IMPLEMENTED:
  replace_frame_t::all_convoys_of_this_line mode (line-mode constructor, magic_replace_line),
  opened from line management (schedule_list bt_replace); merged from the ex-15-line-replacement
  lineage; completion message when the line's replacing count reaches 0.
- Replacer↔overhaul interaction ("replace instead of overhaul") — backend IMPLEMENTED:
  depot_t::convoi_arrived executes replace_now INSTEAD of overhauling/maintaining when replace_at
  is on_overhaul/on_maintenance (remaining vehicles still overhauled normally); no GUI sets those
  values, so unreachable except via savegame/tool data. Related TODO in vehicle_t::overhaul.

## Other vehicle features

- Mothballing — vehicle_t::mothball / un_mothball / get_is_mothballed (rdwr ≥ 15); mothballed
  vehicles excluded from re-combination pools. Simulation side implemented (Jan 2023
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg202994.html]); no UI, and no callers
  outside vehicle.cc — unreachable in play. Cost mechanics verified as intended: the vehicle is
  taken out of monthly fixed costs and one month's fixed cost is charged at each transition
  (`add_maintenance`/`book_vehicle_maintenance` in mothball/un_mothball).
- Self-contained catering — self_contained_catering .dat flag: catering serves only that vehicle
  rather than the whole consist. Implemented (Jun 2022
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg200693.html]).

## Open questions

- Inflation audit: verify every cost/revenue booking path against the requirement above — deferred
  as a separate future task [RECOLLECTION:2026-09-07].
