---
status: draft
verified: ex-15 @ d1c49a519 (base) / ex-15-new-industry-generation @ 7f6176960 (rework)
---
# Industry generation: chain-complete (base) vs consumption-centric (rework)

**Covers:** the generation/infill/density-basis mechanics only — `factory_builder_t::increase_industry_density` and its helpers, `factory_builder_t::build_link`/`build_chain_link`, the density accounting sites in simfab.cc and simworld.cc, `karte_t::init`/`karte_t::new_month`/`karte_t::load` industry paths, `stadt_t::check_bau_factory`, and the mapgen industry-count setting/label. Base subsystem architecture, descriptors, contracts, production model, closure/upgrade → [industry](industry.md). Defects in the rework → [bug-industry-generation](bug-industry-generation.md). Merge/branch status → [ex-15](ex-15.md).

Two designs coexist on two branches. This doc records both, mechanism by mechanism, because both are live code. When the rework lands, collapse this to the single current design.

## Design intent (normative)

The rework exists to serve these stated goals; they are the test it must be judged against.

- Industry production is demand-led: *"the intention of the current code is to ensure that consumer industries always have sufficient supply somewhere in the world. The idea is that industry production is demand led, which is based on reality."* [FORUM:https://forum.simutrans.com/index.php/topic,23478.msg209917.html]
- Proportionality: *"the different producer industries… are in proportion to each other in accordance with the distributionweight, and that the industry density generally on the map is in proportion to the population. We need industries to close down when it is realistic for them to do so (the eventual plan being to simulate ports and importing/exporting)."* [FORUM:https://forum.simutrans.com/index.php/topic,23478.0.html]
- The rework is in scope for 15.x; the *replacement* of consumer generation by a town-growth overhaul is explicitly later: *"consumer industries will be generated as part of the town growth algorithm … leaving only industries higher in the chain (and power stations…) to spawn using the current code"*, and the rework was asked to be *"a temporary change… pending that time"* [FORUM:https://forum.simutrans.com/index.php/topic,23478.0.html]. Scope confirmed: *"All that this needs now is the code for adapting older saved games… and this should be ready to incorporate."* [FORUM:https://forum.simutrans.com/index.php/topic,23478.msg210617.html]
- Rework's own target: *"balanced supply chains, consumers-first … instead of the current system where frequently, well-fed manufacturer industries do not even have 10% of their production capability in use"*; surplus weighting *"so a good that is 50:1 overproduced will naturally get more consumers allocated than, say, one that is 2:1 overproduced"* [FORUM:https://forum.simutrans.com/index.php/topic,23509.0.html].
- Calibration doctrine: *"creating any new asset or system can always rely on the assumption that everything else is calibrated to real life values"*; and *"we should be aiming for most of the countryside covered in farmland… It would not be realistic if most of the countryside were wilderness, and this would also not balance economically."* [FORUM:https://forum.simutrans.com/index.php/topic,23509.0.html]
- Prior warning: an earlier demand-driven spawning patch *"failed on paksets with a deliberate shortage of certain goods"* (prissi) [FORUM:https://forum.simutrans.com/index.php/topic,23478.0.html].
- General goals → [high-level-design-goals](high-level-design-goals.md); the balance-critical prerequisite "Towns generated to serve industries" and the town-growth/goods-pricing/journey-tolerance items → [project-roadmap](project-roadmap.md).

## Mechanism comparison (old state → new state)

### Counted unit of industry density

- Base: every non-power factory contributes `100 / distribution_weight` to `actual_industry_density`; increments in `factory_builder_t::build_factory`, decrements in `fabrik_t::~fabrik_t`, swapped in the upgrade path of `fabrik_t::new_month`, recomputed in `karte_t::init` and the legacy path of `karte_t::rdwr_gamestate`.
- Rework: every one of those sites is gated on `factory_desc_t::is_consumer_only()`. Producers, manufacturers and raw extractors no longer debit the density budget, and the upgrade density cap in `fabrik_t::new_month` is bypassed for non-consumer-only types.
- Consequence: `distribution_weight` governs end consumers only. Producer counts become an emergent property of chain-link arithmetic rather than of the pakset weight.

### Generation order and modes

- Base: `increase_industry_density` takes `uint32 force_consumer` (0 neutral / 1 disallow forcing / 2 always force). A new consumer is built with `build_link(..., number_of_chains = -1, ...)`, i.e. the complete supplier tree at once.
- Rework: `enum density_options { NEUTRAL, NO_FORCE, CONSUMER_ONLY, FILL_MISSING_ONLY, FILL_UNDERSUPPLIED }` (bauer/fabrikbauer.h). Consumers are built with `number_of_chains = 0` — no upstream — and upstream is added by separate `FILL_MISSING_ONLY`/`FILL_UNDERSUPPLIED` passes. `FILL_*` modes return 0 when nothing more can be linked instead of falling through to random chain creation.
- `get_random_consumer` gains a `force_consumer_only` parameter so manufacturers can be selected as well as shops; placement is factored out into `factory_builder_t::find_valid_factory_pos`, a single funnel replacing three inlined copies.

### Chain-completion detection

- Base: an unsatisfied factory is recorded as `unlinked_consumer_t(fab, 0)` only when its `missing_goods` set is non-empty; which input is missing is then re-derived by scanning suppliers, with separate contracts/non-contracts branches.
- Rework: recorded per input slot as `unlinked_consumer_t(fab, i)`, including the zero-supplier case, and the contracts/non-contracts branches are unified.

### Shared-supplier apportionment

- Base: a supplier's output is charged at full nominal need to every consumer sharing it (`competing_consumer->get_base_production() * supplier consumption`, summed).
- Rework: each competing consumer's draw is weighted by `supplier production / total production of all that consumer's suppliers for the good`, accumulated in `sint64`. One hungry shop no longer attracts duplicate suppliers.

### Downstream bottleneck accounting

- Rework adds `factory_builder_t::adjust_input_consumption` (two overloads) and `adjust_output_production`. A factory's nominal need is scaled by the fraction of its own output actually taken by its linked consumers, apportioned across competing suppliers; a factory's effective production is scaled by its least-well-supplied input. Base has no equivalent — nominal figures are used throughout.

### World-level surplus measurement

- Base: `oversupplied_goods` weight is the local surplus `available_for_consumption - consumption_level` for the factory being examined.
- Rework: new `get_global_production`, `get_global_consumption`, `get_global_oversupply` scan the whole fab_list. `get_global_consumption` recurses downstream so an intermediate with no final outlet contributes less; electricity producers are excluded. `get_global_production` sums `adjust_output_production`, i.e. bottleneck-adjusted rather than nominal. Weight becomes `global_production * total_consumer_weight / global_consumption`, where `total_consumer_weight` is the summed `distribution_weight` of every available type accepting the good — surplus scaled by how many consumer types want it. Selection additionally re-verifies `get_global_oversupply > 0` over up to five draws.

### Supply tolerance

- Base (non-contracts): strict `available_for_consumption >= consumption_level` for supplied, `<` for undersupplied; global check `(global_production - global_consumption) > 0`.
- Rework: explicit tolerance bands — supplied when `available * 8 > consumption * 9`, undersupplied when `available * 9 < consumption * 8` or `available <= 0`, globally oversupplied when `production * 8 > consumption * 9`. The constants are hard-coded. (The contracts path already used 8/9 and 9/8 thresholds on the base branch.)

### Manufacturer placement and chain termination

- Base: the selected consumer type is built; if it is a manufacturer, nothing guarantees its output has an outlet.
- Rework: if the chosen type is not consumer-only, its products are checked for existing global oversupply and a downstream consumer is substituted if so. If a manufacturer is genuinely warranted, a second consumer is selected for one of its outputs and both are built, then the new consumer is cross-connected via `build_chain_link`. A new manufacturer type is avoided when an under-consumed instance of it already exists.

### Mapgen loop

- Base: `karte_t::init` loops `increase_industry_density(false, false, false, 1)` while `fab_list.get_count() < settings.get_factory_count()`, breaking after more than 3 consecutive failures.
- Rework: loop condition is `karte_t::count_consumers() < settings.get_factory_count()` (new private method counting consumer-only factories). Each iteration: a `NO_FORCE` starter, then a `FILL_MISSING_ONLY` sweep, then a `CONSUMER_ONLY` loop, then a final `FILL_UNDERSUPPLIED` sweep after the main loop. The setting therefore counts shops, not total factories; the mapgen label is renamed accordingly (gui/welt.cc, en.tab, fr.tab only).

### Monthly and city growth

- Base: `karte_t::new_month` loops up to 8 times while `actual < target`, calling `increase_industry_density(true, true)` (neutral). `stadt_t::check_bau_factory` makes a single `force_consumer = 2` call.
- Rework: `new_month` loops up to 4 times; each triggered iteration tries `CONSUMER_ONLY`, falls back to `NEUTRAL`, then runs a `FILL_MISSING_ONLY` sweep. `check_bau_factory` does the same `CONSUMER_ONLY` → `NEUTRAL` → sweep sequence. Power-station calls switch from `1` to `NO_FORCE`.

### Save-load density basis

- Base: `karte_t::load` reads `industry_density_proportion` per extended version (with a compressed-form conversion for the older Extended series) and reconstructs `actual_industry_density` counting all non-power factories.
- Rework: adds `karte_t::recalc_idp()` — derives consumer-only density, average overproduction across final goods, and back-derives a consumer-basis proportion clamped against the loaded value — and `karte_t::recalc_actual_density()`. Recalculation is invoked for pre-15 extended versions. No `EX_SAVE_MINOR` bump accompanies the change of basis.

## Invariants introduced or altered

- Density accounting is consumer-only; `get_target_industry_density()` now means "target consumer density per million citizens".
- The mapgen industry-count setting counts consumer-only factories. Its stored value is not migrated, so an existing value produces a different world.
- `actual_industry_density` is recomputed from fab_list on load, so the persisted value is not honoured.
- Generation becomes demand-led at world level: the surplus of a good, not local chain state, decides what is built next.
- Sync/save exposure widens: new load-time computation (`recalc_idp`) and new whole-world scans inside `karte_t::new_month` and `stadt_t::check_bau_factory`, both inside the synced step. Mapgen remains under `MAP_CREATE_RANDOM`. Rules → [sync-and-determinism](sync-and-determinism.md), [savegame-versioning](savegame-versioning.md) (AGENTS.md rule 5).

## Conformity to intent

| Intent | Assessment |
|---|---|
| Demand-led; consumers always sufficiently supplied | Conforms, and better than base: per-slot detection, competing-supplier apportionment, bottleneck accounting |
| Surplus-weighted consumer allocation | Conforms: global prod/cons × consumer-type weight, re-verified over multiple draws |
| Countryside mostly in production, not wilderness | Conforms better (more upstream generated); unmeasured |
| Realistic decisions rather than mechanics-reading ([high-level-design-goals](high-level-design-goals.md)) | Conforms better: fewer stranded and half-built chains, fewer manufacturers with no outlet |
| Producers in proportion per `distribution_weight` | **Regresses**: producers are removed from density accounting entirely, so the weight no longer governs them. Whether demand-derived producer counts are the intended replacement is an open decision |
| Density ∝ population | Form conforms; the *basis* changed without pakset-side recalibration of `distribution_weight`, against the calibration doctrine |
| Industries close when realistic | Neutral in code (closure thresholds unchanged), but a better-balanced closed economy accumulates fewer unproductive months, so fewer closures. The feature meant to restore churn is ports/import-export, which is unscheduled |
| Consumer generation eventually moves to town growth | Tension: the rework entrenches substantial consumer machinery in `factory_builder_t` against a request that it be temporary. The `density_options` seam makes the eventual cut local, and the `FILL_*` servo modes are exactly the future factory-builder role |
| Balance-critical prerequisite "Towns generated to serve industries" ([project-roadmap](project-roadmap.md)) | **Worsens exposure**: free producers plus map-wide placement generate more rural upstream industry, while the feature that would give those industries a workforce is unimplemented |

## Fitness as a base for planned future features

Durable assets: (1) the consumer/upstream seam is load-bearing, which is exactly where the town-growth plan cuts; (2) a world-level supply/demand measurement layer exists for the first time; (3) demand-led placement composes with injected supply.

| Planned feature | Fitness |
|---|---|
| Consumers move to town growth | Better — `density_options` separates the consumer half from the `FILL_*` servo half; deleting the consumer half is a local edit |
| Town growth responds to local industry supply / shop receipt success | Better as a template, not a drop-in: the apportionment maths is reusable, but it computes *structural potential* where town growth needs *realised delivery* |
| Ports / import-export | Much better. `get_global_oversupply` is the deficit query a port needs; if ports are factories, the existing balancing sees them unchanged. Required change: the sweeps must become defeasible for importable goods — the tolerance bands are the hook, so promoting them to parameters is what makes ports a configuration exercise rather than a rewrite |
| Industries close when realistic | Better — `get_global_consumption(good)` is the collapsed-demand signal that `months_unproductive` only approximates |
| Towns generated to serve industries [\*B] | Better — `find_valid_factory_pos` is a single placement funnel to hook |
| Varying price of goods / goods classes | Neutral-positive — chains terminate properly, so pricing has non-degenerate chains; would later need price-aware consumer selection |
| Journey-time tolerances for goods | Neutral-positive — per-good supplier lists make chain distances checkable; the rework lengthens chains, so the feature becomes more necessary |
| Private vans | Neutral-positive — same link graph, and the `tell_me` parameter added to `fabrik_t::add_supplier`/`disconnect_supplier` is what high-volume non-player link churn needs to avoid message spam |

What is thrown away when town growth lands: `recalc_idp` and the consumer-basis proportion machinery, the consumer placement and selection machinery, and the density-proportion control loop for consumers (town growth becomes the controller, the factory builder becomes a servo).

## Decisions required (not fixable in code)

- Should producer industries remain governed by `distribution_weight`, or are demand-derived producer counts the intended replacement?
- Should the basis change be accompanied by pakset-side recalibration of `distribution_weight`, and how are pakset authors told the weights' meaning changed?
- Savegame basis: bump `EX_SAVE_MINOR` now and again when town growth lands, or make the persisted representation survive the second change (basis tag, or derive at load and persist nothing)?
- Should the durable upstream-balancing half be split from the consumer half that town growth will displace?
- Sequencing against 15.x cost balancing (the rework changes industry count, mix and distribution, hence freight/mail/passenger volumes and therefore every calibration baseline) and against "Towns generated to serve industries [\*B]".
- Interim acceptance of reduced industry churn until ports exist; and whether the density-setting semantics change and the on-load consumer infill warrant a player-facing note rather than a silent change in ongoing games.

## Open questions

- Does `recalc_idp()`'s dependence on factory production figures settle before it runs at load? Finance history and fab_list are loaded first, but `fabrik_t::get_monthly_production` may depend on state settled later; an in-process server reload would mask the difference → [sync-and-determinism](sync-and-determinism.md). Not verified.
- Is the reduced industry churn acceptable for the whole period until ports/import-export exist, given that feature is unscheduled?
- Should the tolerance bands be simuconf/pakset parameters before merge (see the ports row above)?
