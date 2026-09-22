---
status: draft
verified: ex-15 @ d1c49a519
---
# Industry (factories, chains, generation)

**Covers:** simfab.*, bauer/fabrikbauer.*, descriptor/factory_desc.*, descriptor/goods_desc.h, the industry-density state and factory stepping in simworld.h/simworld.cc, `stadt_t::check_bau_factory` (simcity.cc), `tool_increase_industry_t` (simtool.cc). NOT covered here: cities/city growth, passenger/mail generation, players/finance → [economy-and-passengers](economy-and-passengers.md); descriptor/pak loading, makeobj, settings mechanics → [data-and-pak](data-and-pak.md); place-finding, ground, climates, regions → [world-and-ground](world-and-ground.md).

## Overview & architecture

The industry subsystem models factories that consume input goods and produce output goods, linked into supplier→consumer chains. Chains create the freight transport demand that players serve. Each factory instance owns a `gebaeude_t` building on the map; the world keeps every factory in `karte_t`'s fab_list (`karte_t::add_fab`, `karte_t::get_fab_list`; `fabrik_t::get_fab` resolves a factory from a koord).

Principal classes:

- `fabrik_t` (simfab.h/.cc) — one factory instance: base production, per-goods input/output slots, supplier/consumer links, statistics, power demand, fields (farms), contracts, monthly closure/upgrade logic (`fabrik_t::step`, `fabrik_t::new_month`, `fabrik_t::rdwr`).
- `ware_production_t` (simfab.h) — the per-factory, per-good slot object. One per input good and per output good (`fabrik_t::get_input()`/`get_output()` arrays). Holds stock (`menge`), capacity (`max`), in-transit figures, per-goods statistics, and the good's link list (`links` + `link_aux` contract amounts). NOT the same as `ware_t` (simware.h), which is an individual in-transit goods shipment.
- `factory_builder_t` (bauer/fabrikbauer.h/.cc) — static builder and the single funnel for all industry generation: descriptor table (`desc_table`), `build_factory`, `build_link`, `build_chain_link`, `increase_industry_density`, site search (`factory_site_searcher_t`, `find_random_construction_site`), and a spacing-exclusion bitmap (`fab_map`; `init_fab_map`/`add_factory_to_fab_map`, driven by `settings_t::get_min_factory_spacing`).
- `factory_desc_t`, `factory_supplier_desc_t`, `factory_product_desc_t`, `field_group_desc_t`, `field_class_desc_t`, `smoke_desc_t` (descriptor/factory_desc.h) — static type data (see below).
- `goods_desc_t` (descriptor/goods_desc.h) — goods types; `goods_manager_t` (bauer/goods_manager) is the registry.
- World-side state (simworld.h): `industry_density_proportion`, `actual_industry_density`, `closed_factories_this_month`, `should_close_factories_this_month`.

`fabrik_t` classifies itself into a sector (`fabrik_t::ftype`: `marine_resource`, `resource`, `resource_city`, `manufacturing`, `end_consumer`, `power_plant`, `unknown`; `fabrik_t::get_sector`, set by `fabrik_t::set_sector`).

## Factory descriptors & goods

`factory_desc_t` (loading → [data-and-pak](data-and-pak.md)) carries:

- `get_placement()` (`factory_desc_t::site_t`: `Land`, `Water`, `City`, `river`, `shore`, `forest`, `river_city`, `shore_city`) — where the builder may place it; city sites are placed inside city limits via `factory_site_searcher_t` in `factory_builder_t::build_link`.
- `get_distribution_weight()` — dual purpose: (1) the weighted-pick weight when selecting consumer/producer types (`factory_builder_t::get_random_consumer`, `find_producer`; weight 0 excludes a type entirely), and (2) the industry-density accounting unit (each factory counts `100 / distribution_weight`; see below).
- `get_productivity()` + `get_range()` — instance base production is `productivity + simrand(range)` (`fabrik_t` constructor).
- `is_consumer_only()` (no products), `is_producer_only()` (no suppliers), `is_electricity_producer()`.
- Electricity/boost data: `get_electric_boost`/`get_pax_boost`/`get_mail_boost`, `get_electric_amount`, `get_electricity_proportion`/`get_inverse_electricity_proportion`.
- `get_upgrades(i)`/`get_upgrades_count()` — upgrade target types; `get_expand_probability`/`get_expand_minumum`/`get_expand_range`/`get_expand_times` — growth of fieldless factories in `fabrik_t::rescale_delta`.
- `get_max_distance_to_consumer()`/`get_max_distance_to_supplier()` — chain-linking distance caps (65535 = unlimited); rescaled with map size by `factory_desc_t::set_scale` (via `karte_t::set_scale`, `factory_builder_t::modifiable_table`).
- `get_field_group()`/`get_field_output_divider()` — farm fields; `field_group_desc_t` holds probability, min/max/start field counts and field classes; `field_class_desc_t` per-class production, storage capacity, spawn weight.
- `pax_level`/`pax_demand`/`mail_demand` are legacy fields kept for backwards compatibility — the live values come from the associated `gebaeude_t` (comments in factory_desc.h).

Timeline and climate/region data are NOT on `factory_desc_t` itself: they come from the building descriptor — `get_building()->is_available(timeline)`, `get_intro_year_month()`, `get_retire_year_month()`, `get_allowed_climate_bits()`, `get_allowed_region_bits()`/`is_allowed_region()` (used in `find_producer`, `count_producers`, `power_stations_available`, `build_link`, `check_construction_site`, and the retirement check in `fabrik_t::new_month`).

Sub-descriptors: `factory_supplier_desc_t` — `get_input_type()` (goods), `get_consumption()` (input units per unit of production), `get_supplier_count()` (how many suppliers to link), `get_capacity()`. `factory_product_desc_t` — `get_output_type()`, `get_factor()` (output units per unit of production; 256 = 1.0), `get_capacity()`.

`goods_desc_t`: `catg`/`catg_index`/`goods_index`; goods with equal `catg_index` are interchangeable (`is_interchangeable`); `weight_per_unit`; `speed_bonus` (deprecated except for discarding long-waiting goods); passenger/mail class counts and fare stages (fares → [economy-and-passengers](economy-and-passengers.md)).

## Industry density system

The core Extended mechanism controlling how much industry exists. Two `uint32` world fields (simworld.h): `industry_density_proportion` and `actual_industry_density`.

- Unit: every factory contributes `100 / distribution_weight` (integer division) to `actual_industry_density` — a type twice as likely to be picked counts half as much. Increments/decrements: `factory_builder_t::build_factory` (+), `fabrik_t::~fabrik_t` (−), the upgrade path in `fabrik_t::new_month` (− old type, + new type), and recomputation/legacy paths in `karte_t::init`, `karte_t::rdwr_gamestate` and `fabrik_t::rdwr` (old saves).
- Target: `karte_t::get_target_industry_density()` = `finance_history_month[0][WORLD_CITIZENS] * industry_density_proportion / 1000000` — the proportion is calibrated as density units per million citizens, so target industry scales with total population.
- Baseline: `karte_t::new_month` sets the proportion once, the first month it is still 0 with nonzero world citizens, from `actual_industry_density * 1000000 / citizens` — i.e. the mapgen result defines the game's target. The simuconf key `industry_density_proportion_override` (`settings_t::get_industry_density_proportion_override`) replaces it every month when > 0.
- Accessors/mutators: `karte_t::get_actual_industry_density`, `karte_t::increase_actual_industry_density`, `karte_t::decrease_actual_industry_density`.
- Power stations are excluded from density recomputation — `karte_t::init` (mapgen) and the legacy reconstruction in `karte_t::rdwr_gamestate` both skip `is_electricity_producer()` types, with the code comment "a different system is used for them": power stations are instead governed by the electricity balance (`settings_t::get_electric_promille` vs the ratio of electricity production to total demand, checked in `karte_t::new_month` and `factory_builder_t::increase_industry_density`), and they are also exempt from the `do_not_add_beyond_target_density` cap in `increase_industry_density`. Note the unconditional build/destroy increments do NOT have this exemption — see Open questions.
- Ongoing growth: `karte_t::new_month` loops (max 8 iterations) while `actual < target`, each iteration drawing `simrand(100)` against `max(deficit percentage, 8)` and calling `factory_builder_t::increase_industry_density(true, true)` — the deficit-proportional chance means a shortfall is on average remedied within about a year. Separately, `stadt_t::check_bau_factory` (called from `stadt_t::step_grow_city`) fires when population crosses a multiple of `settings_t::get_industry_increase_every` that is an exact power of two (`bev / inc == 1<<i`), gated on `actual < target`, with `force_consumer = 2`.
- Savegame sensitivity: both fields are persisted (`karte_t::save`, `karte_t::rdwr_gamestate`, with version-dependent conversion/reconstruction paths). They are synced world state; changes here are savegame- AND network-sensitive → [savegame-versioning](savegame-versioning.md), [sync-and-determinism](sync-and-determinism.md) (AGENTS.md rule 5).

## Chain building & inter-factory linking

- `factory_builder_t::build_factory` — constructs the `fabrik_t`, registers it (`karte_t::add_fab`, fab_map exclusion, world list), marks connected roads, increments industry density, creates a water halt for `Water` placement, and scales the building's jobs/visitor/mail demand up when the rolled prodbase exceeds the descriptor productivity.
- `factory_builder_t::build_link(parent, info, initial_prod_base, rotate, pos, player, number_of_chains, ignore_climates)` — builds ONE factory at pos via `build_factory`, then calls `build_chain_link` for supplier slot `i` for all `i` if `number_of_chains < 0`, else only for `i < number_of_chains` (so `<0` = build the complete supplier tree; callers also pass a large constant, e.g. 10000, with the same effect). Handles city-site search, rotation choice, and temporarily rotating the world for non-rotatable factory trees (`can_factory_tree_rotate`).
- `factory_builder_t::build_chain_link(origin_fab, info, supplier_nr, player, no_new_industries)` — satisfies one input good of one consumer: demand = consumer prodbase × supplier `get_consumption()`. First cross-connects existing producers, subject to `get_min_factory_spacing`, both max-distance caps, an ocean-route check for `Water` placements (`has_ocean_route`), remaining supplier capacity, and a `get_crossconnect_factor`-percent chance to connect even when full. Then, unless `no_new_industries`, builds new producers via recursive `build_link` (random prodbase, retry limit, `ignore_climates` after failures) until supplier count/demand is satisfied, and re-links any consumers it stole capacity from.
- `factory_builder_t::new_world` — rebuilds the fab_map exclusion bitmap at map load/generation (calls `init_fab_map`).
- Link maintenance API on `fabrik_t`: `add_consumer(koord, goods, contract)`, `remove_consumer`, `add_supplier(koord, goods)`, `add_supplier(fabrik_t*, product)`, `remove_supplier`, `add_customer(fabrik_t*)`, `add_all_suppliers` (bulk re-link). Link lists live in `ware_production_t`, i.e. they are PER-GOOD, not per-factory: each input/output good has its own supplier/consumer list, kept distance-ordered (`RelativeDistanceOrdering`). `add_consumer` also registers the reverse link (`add_supplier` on the counterpart) and updates `total_contracts` on both sides.
- `fabrik_t::disconnect_supplier(pos, supplier)` / `disconnect_consumer(pos)` — with a valid koord, remove that link; with `koord::invalid`, remove nothing but check for missing links, attempt re-linking against the whole fab_list (in contracts mode `disconnect_supplier` first calls `build_chain_link` with `no_new_industries=true`), and return true when the factory is orphaned and must close: end consumers only when ALL inputs are missing; other industries when ANY input is missing; producers when ALL outputs are missing.

## Contracts mode

`settings_t::using_fab_contracts()` is true exactly when `just_in_time == 5`. The `just_in_time` field (settings.h) documents the supply models: 0 classic, 1 JIT classic, 2 JIT v2, 5 contract-based JIT.

- Non-contracts mode: `fabrik_t::step` produces against stock; output is capped by the smallest input stock and by storage capacity; distribution to linked consumers happens in `verteile_waren` on a fixed production interval.
- Contracts mode: `fabrik_t::step` branches to `fabrik_t::step_contracts`; each output link carries an explicit contracted monthly amount (`ware_production_t` `link_aux`, per-good totals via `get_total_contracts`). `fabrik_t::negotiate_contracts` (called from `fabrik_t::new_month` and from `increase_industry_density`) trims output contracts exceeding monthly production (furthest links first), derives a manufacturing factor so input contracts follow used output, and scales end-consumer demand via `adjust_consumption_by_passenger_level`. Per-slot contract state is switched on/off by `fabrik_t::init_contracts`/`remove_contracts` and `ware_production_t::set_using_contracts`/`reset_using_contracts` (driven from `settings_t::set_just_in_time`).
- Chain building and the unlinked-consumer detection in `increase_industry_density` both branch on the mode: contracts mode compares contracted totals against `get_monthly_production` — an input counts as under-supplied when its contracts are below 8/9 of monthly consumption demand, and an output is listed as oversupplied when its contracts are below 9/8 of monthly production.

## Production & consumption model

- Base production: `fabrik_t::get_base_production()` (`prodbase`; `set_base_production`). Boosted production: `get_prodfactor()` = 256 (`DEFAULT_PRODUCTION_FACTOR`) + `prodfactor_electric` + pax + mail boost factors; `get_current_production()` and `get_monthly_production(pfactor)` scale prodbase by the boost and month length (`karte_t::calc_adjusted_monthly_figure`), where `pfactor` is a product `get_factor()` or supplier `get_consumption()`.
- `fabrik_t::step` (non-contracts): per-tick production = prodbase × boost × delta_t in integer fixed point (`PRODUCTION_DELTA_T`, `fabrik_t::precision_bits`, with a carried remainder to avoid cumulative error). Manufacturers are limited by the SMALLEST input stock; inputs are consumed only when production occurred; output stops at each good's `max`. End consumers and power stations consume stock directly; staff shortage scales output (`gebaeude_t::get_staffing_level_percentage`, `fabrik_t::is_staff_shortage`). Accumulated output is dispatched by `verteile_waren` each production interval; statistics roll monthly (`fabrik_t::new_month`, `ware_production_t::roll_stats`).
- Passenger-level consumption adjustment: `fabrik_t::adjust_consumption_by_passenger_level` — for consumer-only industries, scales a consumption figure UP (never below 100%) by the average arriving consumers (`FAB_CONSUMER_ARRIVED`, months 1–3) relative to the building's adjusted visitor demand. Used in `calc_max_intransit_percentages` and `negotiate_contracts`; the code comment notes actual consumption figures are deliberately not used, to avoid deadlocks.
- Fields: field factories spawn fields at build (`field_group_desc_t` min/start fields) and expand in `fabrik_t::rescale_delta` — every 256 active production rounds, a field factory may add a field (group probability, up to `get_max_fields`), a fieldless factory may expand prodbase (`get_expand_probability`/10000, + `get_expand_minumum` + rand(`get_expand_range`), up to `get_expand_times`, tracked in `times_expanded`). Field output feeds back via `adjust_production_for_fields` and `get_field_output_divider`.

## Closure & upgrade

Closure triggers, all evaluated in `fabrik_t::new_month`:

- Orphaned links: when the factory status is `missing_connections`/`material_not_available`/`missing_consumer`, `disconnect_supplier(koord::invalid)`/`disconnect_consumer(koord::invalid)` decide `must_close`.
- Timeline obsolescence: past the building's `get_retire_year_month`, closure is probabilistic per month (`simrand` over the remaining months) and guaranteed after `settings_t::get_factory_max_years_obsolete` further years.
- Contracts mode only: `months_unproductive` (> 60, from zero `FAB_PRODUCTION`) or `months_missing_contracts` (> 12, zero-contract output/input goods) appends the factory to `karte_t::should_close_factories_this_month`, weighted by the month counts; `karte_t::new_month` closes up to 16 per month via `pick_any_weighted`.

On a closure trigger, the upgrade path runs first: candidate upgrades (`factory_desc_t::get_upgrades`) must have identical building size, the same `is_electricity_producer` flag, be inside their timeline window, and satisfy the density cap — `max_density` = 150% of target density (unbounded when target is 0) and the world's adjusted density (actual minus this factory's own `100/weight` contribution) must be below `max_density + 100/new_type weight`. The chance to upgrade at all derives from the factory's operation rate: the greater of the mean and half the max of `FAB_PRODUCTION` over the last 11 months, halved and then offset by a fixed 50 percentage points. On success: the descriptor is swapped, prodbase re-rolled (city consumers re-scaled by relative city size), fields rescaled, the input/output `ware_production_t` arrays rebuilt keeping matching goods and unlinking obsolete ones, demands recalculated, and density accounting swapped (− old type weight, + new type weight). Otherwise the factory goes to `karte_t::closed_factories_this_month`; `karte_t::new_month` then removes those buildings via `hausbauer_t::remove`. `fabrik_t::~fabrik_t` decrements density, unlinks the factory from all chains (closing newly orphaned counterparts) and messages the players.

## Generation entry points

All runtime and mapgen industry generation funnels through `factory_builder_t::increase_industry_density(tell_me, do_not_add_beyond_target_density, power_stations_only, force_consumer)`: it first completes unfinished chains for unlinked consumers, else builds either a power station (electricity shortfall vs `get_electric_promille`, and not `force_consumer`) or a new consumer chain (`get_random_consumer` + `build_link` with `number_of_chains = -1`, i.e. the full supplier tree). `force_consumer`: 0 neutral (75% chance to prefer a new consumer), 1 disallow forcing, 2 always force a consumer. Callers:

- `karte_t::init` (mapgen): after `factory_builder_t::new_world()`, loops `increase_industry_density(false, false, false, 1)` while `fab_list.get_count() < settings.get_factory_count()`, breaking after more than 3 consecutive build failures (map nearly full); the final count is written back via `settings_t::set_factory_count`. Mapgen runs under `MAP_CREATE_RANDOM` outside networked play → [sync-and-determinism](sync-and-determinism.md).
- `karte_t::new_month` (called from `karte_t::step` at month rollover): the density top-up loop above, plus a `power_stations_only` call guarded by `factory_builder_t::power_stations_available()`.
- `stadt_t::check_bau_factory` (from `stadt_t::step_grow_city`): population-doubling trigger, `force_consumer = 2`.
- `tool_increase_industry_t::init` (simtool.cc): player tool, uncapped (`do_not_add_beyond_target_density = false`).

## Invariants & sync/save sensitivity

- Persisted industry state: `fabrik_t::rdwr`, `ware_production_t::rdwr`, `fabrik_t::arrival_statistics_t::rdwr` (per factory); `industry_density_proportion` and `actual_industry_density` in `karte_t::save`/`karte_t::rdwr_gamestate` (with version-dependent reconstruction). Never enumerate stored data in docs — the rdwr methods are authoritative → [savegame-versioning](savegame-versioning.md).
- Randomness: all industry generation/stepping randomness uses the SYNCED `simrand` stream; there are no `sim_async_rand` calls in simfab.cc or fabrikbauer.cc. Mapgen draws happen under `MAP_CREATE_RANDOM` before/outside networked play; `karte_t::new_month` runs inside `karte_t::step` (checkpoint rands[9] follows it) and factory stepping (`fabrik_t::step` over the fab_list) is bracketed by checkpoint rands[20] — checkpoint map and rules: [sync-and-determinism](sync-and-determinism.md).
- Threading: the path explorer must be parked before `new_month`'s factory in-transit recalculation → [threading](threading.md).
- Any change to density accounting, link lists or generation order is savegame- and desync-sensitive (AGENTS.md rule 5).

## Performance

No measured industry-specific hotspot inventory is recorded yet; do not assume hotness without profiling. Method and full inventory: [performance](performance.md); measured passenger-generation figures: [economy-and-passengers](economy-and-passengers.md). Factory stepping runs under `karte_t::step` and generation under `karte_t::new_month`, so both are performance-critical by the project-notes default rule.

## Known problems

- Open bug list: [known-bugs](known-bugs.md).
- Industry-generation crash/hang/overflow/save-load defects: [bug-industry-generation](bug-industry-generation.md).
- Generation-rework assessment and mechanics comparison: [industry-generation-rework](industry-generation-rework.md).

## Nested documents

Keyed from here per [conventions](conventions.md); the files live flat in `ai/`.

- [industry-generation-rework](industry-generation-rework.md) — read when: touching industry generation, industry infill or the industry-density basis on the consumption-centric rework; assessing that rework against design intent; or planning town-growth / ports-and-imports work that builds on it. Records the base (chain-complete) and rework (consumption-centric) mechanics side by side.
- [bug-industry-generation](bug-industry-generation.md) — read when: working on the consumption-centric generation branch, or triaging an industry-generation crash, hang, overflow or save-load defect. Also keyed from [known-bugs](known-bugs.md).

## Open questions

- Power-station density accounting is asymmetric: `factory_builder_t::build_factory` and `fabrik_t::~fabrik_t` increment/decrement `actual_industry_density` unconditionally (including power stations), while the recomputation paths (`karte_t::init`, `karte_t::rdwr_gamestate`) skip power stations. In-game build/destroy of power stations therefore drifts the actual density relative to a recomputed one. Intended, or a defect? Not resolved from code.
- `settings_t::just_in_time`: the settings.h comment documents values 0, 1, 2 and 5 only. Whether values 3/4 are reachable or dead was not verified.
- `factory_builder_t::build_chain_link` passes `number_of_chains = 10000` in its recursive `build_link` call where other "all chains" callers pass -1; both exceed any realistic supplier count, but whether 10000 was chosen deliberately (vs. the documented `<0` sentinel) is unverified.
