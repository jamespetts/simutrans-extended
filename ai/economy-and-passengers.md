---
status: stub
verified: none
---
# Economy, finance & passengers

**Covers:** simfab.*, simcity.*, simware.*, player/, bauer/fabrikbauer.*, bauer/hausbauer.* (shared with [objects](objects.md)), descriptor/factory_desc.*, descriptor/goods_desc.*, gui factory/city/money frames (→ [gui](gui.md)).

## Initial facts

- `player/simplay.h` is very widely included — the central finance/player header [CODE].
- `simcity.h` is widely included [CODE].
- `player/simplay.cc` receives periodic ex-15 attention [CODE].
- The root contains local-only diagsession artefacts named "…passenger-gen-efficiency-changes-phase-1/2…" — passenger generation received performance work in the past; an indication, not a verified fact [local-only].

## Planned sections

- Factory/industry model: production, chains, delivery (simfab; fabrikbauer).
- Goods (simware) & category system (verify against descriptor/goods_desc).
- Cities & growth (simcity; city buildings; passenger demand origins).
- Passenger generation: Extended-specific mechanics & the efficiency work above [PRIOR → verify; ask the user].
- Players & finance (player/): companies, public player, AI players (gui/ai_option as an indication), insolvency (local-only base-texts artefact "insolvency" as an indication).
- Load/save coupling → [savegame-versioning](savegame-versioning.md).

## Open questions

- Where is passenger generation implemented (simcity? simhalt? simworld?) — verify, do not assume.
