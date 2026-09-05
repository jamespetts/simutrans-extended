---
status: stub
verified: none
---
# Economy, finance & passengers

**Covers:** simfab.*, simcity.*, simware.*, player/ (10 files), bauer/fabrikbauer.*, bauer/hausbauer.* (shared with [objects](objects.md)), descriptor/factory_desc.*, descriptor/goods_desc.*, gui factory/city/money frames (→ [gui](gui.md)).

## Seed facts

- `player/simplay.h` is included by 74 files — finance/player hub [CODE ex-15 @ b06e8fa14].
- `simcity.h` has 30 includers [CODE ex-15 @ b06e8fa14].
- `player/simplay.cc` touched in 8 ex-15-only commits (whole branch history vs master) [CODE].
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
