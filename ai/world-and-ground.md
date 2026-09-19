---
status: stub
verified: none
---
# World & ground

**Covers:** boden/ (incl. boden/wege/ — way tiles), world/, simplan.*, finder/, get_climate (source location to verify), dataobj heightfield/relief (verify location → [data-and-pak](data-and-pak.md)).

## Initial facts

- `boden/grund.h` is the widely-included ground/tile header [CODE].
- `boden/wege/` (way tiles) is a notable divergence area between the branches [CODE].
- A contributor terraforming refactor exists as a remote branch (`Ceeac/extended-ex15-terraforming-refactor`), merge status unknown [CODE master].

## Planned sections

- Plan/tile model: simplan, coordinates (koord/ribi → [data-and-pak](data-and-pak.md)), climates, heights.
- Grund & way tiles: inventory of boden/ and boden/wege/ classes; way wear (Extended feature; root has a local-only notes file as an indication) [UNVERIFIED].
- Terraforming mechanics & ex-15 refactor status → [ex-15](ex-15.md).
- Place-finding (finder/).
- Contents/purpose of the `world/` directory (only 2 files).
- Load/save coupling → [savegame-versioning](savegame-versioning.md).

## Performance hotspots

Measured on the gargantuan fixture (method and full inventory: [performance](performance.md))
[EXECUTION-VERIFIED:2026-09-10 master @ d40847e90]: raw map/ground access is a standing per-step
cost at large map sizes — `grund_t::get_weg` 3.4% self, `karte_t::lookup` 2.4% self,
`grund_t::get_neighbour` 2.6% incl, `planquadrat_t::get_boden_in_hoehe` 1.0% self of in-game CPU.
Parts of the tile walk are multi-threaded → [threading](threading.md).

- Worldgen: the per-city growth loop dominates world-generation cost — ~87% of `karte_t::init` on
  a 1024²/42-city map, with the rule-evaluation loop (`stadt_t::bewerte_loc` 29% self), the
  candidate-collection sweep (`stadt_t::build` 10% self), the rule-gate RNG (`simrand` 10% self)
  and `reset_city_borders` (9% self) the hot spots; per-tile predicates are memoised per sweep
  (`stadt_t::compute_loc_flags`/`get_loc_flags`, 6% self). Method + full inventory:
  [performance](performance.md) [EXECUTION-VERIFIED:2026-09-18 mapgen-perf-fixes @ 652568623].

## Open questions

- Does `world/` duplicate or complement simworld? (Inventory pending.)
- Where is way-wear state stored and how is it saved?
