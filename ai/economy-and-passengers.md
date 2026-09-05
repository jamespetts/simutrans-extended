---
status: stub
verified: none
---
# Economy, finance & passengers

**Covers:** simfab.*, simcity.*, simware.*, player/, bauer/fabrikbauer.*, bauer/hausbauer.* (shared with [objects](objects.md)), descriptor/factory_desc.*, descriptor/goods_desc.*, gui factory/city/money frames (→ [gui](gui.md)).

## Seed facts

- `player/simplay.h` is very widely included — finance/player hub [CODE].
- `simcity.h` is widely included [CODE].
- `player/simplay.cc` receives periodic ex-15 attention [CODE].
- Root holds local-only diagsession artefacts named "…passenger-gen-efficiency-changes-phase-1/2…" — passenger generation received performance work in the past; a lead, not a fact [local-only].

## Planned sections

- Factory/industry model: production, chains, delivery (simfab; fabrikbauer).
- Goods (simware) & category system (verify against descriptor/goods_desc).
- Cities & growth (simcity; city buildings; passenger demand origins).
- Passenger generation: Extended-specific mechanics & the efficiency work above [PRIOR → verify; interview candidate].
- Players & finance (player/): companies, public player, AI players (gui/ai_option as lead), insolvency (local-only base-texts artefact "insolvency" as lead).
- Load/save coupling → [savegame-versioning](savegame-versioning.md).

## Open questions

- Where passenger generation actually lives (simcity? simhalt? simworld?) — verify, do not assume.
