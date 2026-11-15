---
status: stub
verified: none
---
# Economy, finance & passengers

**Covers:** simcity.*, simware.*, player/, bauer/hausbauer.* (shared with [objects](objects.md)), gui city/money frames (→ [gui](gui.md)). NOT covered here: factories/industries, factory & goods descriptors, industry density, chain building, contracts, closure/upgrade, industry generation → [industry](industry.md).

## Initial facts

- `player/simplay.h` is very widely included — the central finance/player header [CODE].
- `simcity.h` is widely included [CODE].
- `player/simplay.cc` receives periodic ex-15 attention [CODE].
- The root contains local-only diagsession artefacts named "…passenger-gen-efficiency-changes-phase-1/2…" — passenger generation received performance work in the past; an indication, not a verified fact [local-only].

## Planned sections

- Cities & growth (simcity; city buildings; passenger demand origins).
- Passenger generation: Extended-specific mechanics & the efficiency work above [PRIOR → verify; ask the user].
- Goods shipments (`simware_t`) in transit; the goods *descriptor* and category system live with the industry domain → [industry](industry.md).
- Players & finance (player/): companies, public player, AI players (gui/ai_option as an indication), insolvency (local-only base-texts artefact "insolvency" as an indication).
- Load/save coupling → [savegame-versioning](savegame-versioning.md).

## Verified facts (city growth config)

- City road/house placement is driven by pattern rules in the pakset's `config/cityrules.tab`
  (`stadt_t::cityrules_init`; a `<user_dir>/cityrules.tab` overrides the pakset copy). Each rule's
  evaluation weight gates it at `simrand(8 + weight) == 0`: negative = more likely; `-8` → `rd=0`
  → guaranteed AND consumes no RNG draw (`simrand` early-returns when `max<=1`, utils/simrandom.cc).
  [CODE master @ 206db8379]
- Property names: the weight key is `.distribution_weight`; the legacy `house_N.chance`/
  `road_N.chance` spelling is also read, `.distribution_weight` overriding `.chance` where
  present (Extended paksets write `.chance`). Weights stay network-synced via the
  unchanged `rule_t::rdwr`/`stadt_t::cityrules_rdwr` path.
  [CODE master @ 206db8379; EXECUTION-VERIFIED:2026-09-18]

## Private cars

- Car ownership is per passenger wealth class: `karte_t::get_private_car_ownership(monthyear, g_class)` interpolates that class's series from `config/privatecar.tab` (`car_ownership[class]`), falling back to the single untagged `car_ownership` series; with no data, the highest class of a multi-class pakset has 100% ownership and the others use `default_car_ownership_percent`. [CODE ex-15 @ 422122533]
- `karte_t::generate_passengers_or_mail` draws one car type per packet that has access to a private car, before the mode choice, from the era-filtered, distribution-weighted pool for the packet's class (`private_car_t::pick_citycar_desc(g_class)`). A class whose pool is empty makes no private car journeys, whatever its ownership percentage. [CODE ex-15 @ 422122533 plus working-tree private-car class changes]
- The pools are rebuilt monthly by `private_car_t::build_timeline_list` from the city-car descriptors the timeline makes available. Each descriptor's available classes come from the `class` key of its `.dat` file (comma-separated integer class indices, 0 lowest); a descriptor without the key is available to every class. [CODE ex-15 @ 422122533 plus working-tree private-car class changes]
- Private car journey times are per class: a journey whose destination is the origin city uses the generic road time per tile at that class's average speed, and a routed journey to another city or to an industry or attraction uses the time recorded for each class along the actual road route. The model and its storage: [comfort-routing-integration](path-explorer-optimisation/comfort-routing-integration.md).

## Performance hotspots

Measured on the gargantuan fixture (method and full inventory: [performance](performance.md))
[EXECUTION-VERIFIED:2026-09-10 master @ d40847e90]: passenger generation is a moderate,
monthly-cadence cost — `karte_t::generate_passengers_or_mail` 4.8% incl, `karte_t::find_destination`
3.0% incl of in-game CPU. City *growth* is not hot. City traffic (private cars) is much hotter:
[objects](objects.md).

## Open questions

- Where is passenger generation implemented (simcity? simhalt? simworld?) — verify, do not assume.
