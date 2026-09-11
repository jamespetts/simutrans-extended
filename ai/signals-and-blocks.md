---
status: stub
verified: none
---
# Signals, signalboxes & blocks

**Covers:** simsignalbox.*, signal objects in obj/ (inventory to verify → [objects](objects.md)), reservation mechanics (location to verify — likely vehicle/ and boden/wege/), gui/signal_info.*, gui/signal_spacing.*, gui/signal_connector_gui.*, gui/signalboxlist_frame.*, gui/onewaysign_info.* (→ [gui](gui.md)), old_blockmanager.* (root; legacy status to verify → [repo-map](repo-map.md)).

## Initial facts

- Signalboxes are an Extended-specific concept [PRIOR → verify against simsignalbox.*].
- The root contains local-only historical artefacts: "2019 server signal desync logs.txt", "Commands for debugging server.txt" — signals were a historical desync source; treat this area as sync-critical until proven otherwise [local-only indication].
- `old_blockmanager.cc/h` are tracked at root but suspected dead (a block-manager rewrite happened at some point) [UNVERIFIED].

## Planned sections

- Signal types & block working model (verify against code; Standard priors are unreliable here).
- Signalboxes: purpose, data model, catchment (Extended feature).
- Reservations: who reserves what, when released; interaction with convoy movement (→ [vehicles-and-convoys](vehicles-and-convoys.md)).
- Network-sync sensitivity: which signal/reservation state is checklist-relevant (→ [sync-and-determinism](sync-and-determinism.md), [network](network.md); change-restricted under AGENTS.md rule 5).
- History: block-manager rewrite; desync incidents (ask the user + local artefacts).
- Load/save coupling → [savegame-versioning](savegame-versioning.md).

## Performance hotspots

Measured on the gargantuan fixture (method and full inventory: [performance](performance.md))
[EXECUTION-VERIFIED:2026-09-10 / 2026-09-11]: route reservation clearing
(`convoi_t::unreserve_route_range` + `unreserve_route_threaded`) is a first-class hotspot with
many convoys running — 12.4% self on master, roughly 26% self on ex-15 (single run; repeat before
acting on the delta). It runs on its own worker thread (11.8% of in-game CPU on master, 26.8% on
ex-15) → [threading](threading.md).

## Open questions

- Is old_blockmanager referenced anywhere?
- Which ex-15 branches touched signals (e.g. base_texts_experimental_signals.dat indication)?
