---
status: stub
verified: none
---
# Simulation core (root-level sim* files)

**Covers:** simworld.*, simmain.*, simintr.*, simunits.*, simtool.*, simtool-dialogs.h, simmenu.*, siminteraction.*, simevent.*, simmesg.*, simdebug.*, simmem.*, simio.*, simskin.*, simsound.*, simticker.*, simloadingscreen.*, unicode.*, simconst.h, simtypes.h, macros.h, pathes.h, scrolltext.h. Handles: convoihandle_t.h, halthandle_t.h, linehandle_t.h (→ [vehicles-and-convoys](vehicles-and-convoys.md), [routing-and-scheduling](routing-and-scheduling.md)). simcolor.h → [rendering](rendering.md).

## Initial facts

- Structural headers: `simworld.h` (world state), `simtypes.h` and `simdebug.h` are near-universal dependencies [CODE].

## Planned sections

- Main loop & stepping (simmain/simintr; interaction with network sync — change-restricted under AGENTS.md rule 5, → [sync-and-determinism](sync-and-determinism.md), [network](network.md)).
- World lifecycle: load/save (coordinates with [savegame-versioning](savegame-versioning.md)), world lists.
- Tool system (simtool): how tools are dispatched and parameterised.
- Units & numeric types (simunits; float32e8_t → [utilities](utilities.md)).
- Invariants; known problems & history (e.g. threading of world-list mutations → [threading](threading.md); root has local-only diagsessions on this).

## Performance hotspots

Measured on the gargantuan fixture (method and full inventory: [performance](performance.md))
[EXECUTION-VERIFIED:2026-09-10 master @ d40847e90]:
- The per-step walk over synced moving objects is the single hottest leaf on the fixture:
  `karte_t::sync_list_t::sync_step` 23.7% self / 67.3% incl of in-game CPU; `karte_t::sync_step`
  overall 69.5% incl. Treat everything under `step()`/`sync_step()` as hot.
- Other simworld.cc costs: `karte_t::lookup` 2.4% self, `karte_t::check_transferring_cargoes`
  2.6% self, `karte_t::generate_passengers_or_mail` 4.8% incl
  (→ [economy-and-passengers](economy-and-passengers.md)).
- Savegame load (`karte_t::load`) ~80 s on the fixture; dominates server rotations and client joins.

## Open questions

- Stepping order and which steps are network-sync-critical.
