---
status: stub
verified: none
---
# Signals, signalboxes & blocks

**Covers:** simsignalbox.*, signal objects in obj/ (inventory to verify → [objects](objects.md)), reservation mechanics (location to verify — likely vehicle/ and boden/wege/), gui/signal_info.*, gui/signal_spacing.*, gui/signal_connector_gui.*, gui/signalboxlist_frame.*, gui/onewaysign_info.* (→ [gui](gui.md)), old_blockmanager.* (root; legacy status to verify → [repo-map](repo-map.md)).

## Seed facts

- Signalboxes are an Extended-specific concept [PRIOR → verify against simsignalbox.*].
- Root holds local-only historical artefacts: "2019 server signal desync logs.txt", "Commands for debugging server.txt" — signals were a historical desync source; treat this area as sync-critical until proven otherwise [local-only lead].
- `old_blockmanager.cc/h` are tracked at root but suspected dead (a block-manager rewrite happened at some point) [UNVERIFIED].

## Planned sections

- Signal types & block working model (verify against code; Standard priors are unreliable here).
- Signalboxes: purpose, data model, catchment (Extended feature).
- Reservations: who reserves what, when released; interaction with convoy movement (→ [vehicles-and-convoys](vehicles-and-convoys.md)).
- Network-sync sensitivity: which signal/reservation state is checklist-relevant (→ [network](network.md); change-restricted under AGENTS.md rule 4).
- History: block-manager rewrite; desync incidents (interview + local artefacts).
- Load/save coupling → [savegame-versioning](savegame-versioning.md).

## Open questions

- Is old_blockmanager referenced anywhere?
- Which ex-15 branches touched signals (e.g. base_texts_experimental_signals.dat lead)?
