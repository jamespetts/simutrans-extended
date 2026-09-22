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
- Property-name history: the weight key was `house_N.chance`/`road_N.chance` until commit aa88e8679
  (2017-04) renamed it to `.distribution_weight` with no pakset updated — every Extended pakset still
  writes `.chance`, so from 2017 to 2026 all rule weights were silently 0 (flat 1/8 gates, no
  guaranteed rules; a major contributor to stalled city growth in mapgen → see
  [performance](performance.md) § Mapgen). Fixed 2026-09-18 (master 206db8379): both keys read,
  `.distribution_weight` overriding `.chance` where present. Weights stay network-synced via the
  unchanged `rule_t::rdwr`/`stadt_t::cityrules_rdwr` path. [EXECUTION-VERIFIED:2026-09-18]

## Performance hotspots

Measured on the gargantuan fixture (method and full inventory: [performance](performance.md))
[EXECUTION-VERIFIED:2026-09-10 master @ d40847e90]: passenger generation is a moderate,
monthly-cadence cost — `karte_t::generate_passengers_or_mail` 4.8% incl, `karte_t::find_destination`
3.0% incl of in-game CPU. City *growth* is not hot. City traffic (private cars) is much hotter:
[objects](objects.md).

## Open questions

- Where is passenger generation implemented (simcity? simhalt? simworld?) — verify, do not assume.
