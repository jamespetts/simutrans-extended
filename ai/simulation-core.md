---
status: stub
verified: none
---
# Simulation core (root-level sim* files)

**Covers:** simworld.*, simmain.*, simintr.*, simunits.*, simtool.*, simtool-dialogs.h, simmenu.*, siminteraction.*, simevent.*, simmesg.*, simdebug.*, simmem.*, simio.*, simskin.*, simsound.*, simticker.*, simloadingscreen.*, unicode.*, simconst.h, simtypes.h, macros.h, pathes.h, scrolltext.h. Handles: convoihandle_t.h, halthandle_t.h, linehandle_t.h (→ [vehicles-and-convoys](vehicles-and-convoys.md), [routing-and-scheduling](routing-and-scheduling.md)). simcolor.h → [rendering](rendering.md).

## Seed facts

- Structural hubs: `simworld.h` (world state), `simtypes.h` and `simdebug.h` are near-universal dependencies [CODE].

## Planned sections

- Main loop & stepping (simmain/simintr; interaction with network sync — change-restricted under AGENTS.md rule 4, → [network](network.md)).
- World lifecycle: load/save (coordinates with [savegame-versioning](savegame-versioning.md)), world lists.
- Tool system (simtool): how tools are dispatched and parameterised.
- Units & numeric types (simunits; float32e8_t → [utilities-and-threading](utilities-and-threading.md)).
- Invariants; gotchas & history (e.g. threading of world-list mutations — root has local-only diagsessions on this).

## Open questions

- Stepping order and which steps are network-sync-critical.
