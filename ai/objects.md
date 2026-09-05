---
status: stub
verified: none
---
# Objects on tiles

**Covers:** obj/; bauer/hausbauer.* (building builder, shared with [economy-and-passengers](economy-and-passengers.md)); descriptors → [data-and-pak](data-and-pak.md); signals → [signals-and-blocks](signals-and-blocks.md).

## Seed facts

- `obj/simobj.h` is the tile-object base-class hub [CODE].
- Pier descriptors/writers exist (`descriptor/pier_desc.*`, root `pier_writer.obj` artefact) — piers appear to be an Extended-specific object family [UNVERIFIED — verify inventory].

## Planned sections

- simobj base class & the tile-object model (how objects attach to grund).
- Object type inventory (verify against obj/ contents; do not trust priors).
- Buildings & construction (hausbauer; city buildings → [economy-and-passengers](economy-and-passengers.md)).
- Signals/roadsigns (boundary with [signals-and-blocks](signals-and-blocks.md) — decide canonical home during breadth pass).
- Extended-specific objects (piers; others TBD).
- Load/save coupling → [savegame-versioning](savegame-versioning.md).

## Open questions

- Exact obj/ inventory and which families are Extended-only vs. inherited.
