---
status: stub
verified: none
---
# Objects on tiles

**Covers:** obj/; bauer/hausbauer.* (building builder, shared with [economy-and-passengers](economy-and-passengers.md)); descriptors → [data-and-pak](data-and-pak.md); signals → [signals-and-blocks](signals-and-blocks.md).

## Initial facts

- `obj/simobj.h` is the widely-included tile-object base class [CODE].
- Pier descriptors/writers exist (`descriptor/pier_desc.*`, root `pier_writer.obj` artefact) — piers appear to be an Extended-specific object family [UNVERIFIED — verify inventory].

## Planned sections

- simobj base class & the tile-object model (how objects attach to grund).
- Object type inventory (verify against obj/ contents; do not trust priors).
- Buildings & construction (hausbauer; city buildings → [economy-and-passengers](economy-and-passengers.md)).
- Signals/roadsigns (boundary with [signals-and-blocks](signals-and-blocks.md) — decide canonical location during breadth pass).
- Extended-specific objects (piers; others TBD).
- Load/save coupling → [savegame-versioning](savegame-versioning.md).

## Performance hotspots

Measured on the gargantuan fixture (method and full inventory: [performance](performance.md))
[EXECUTION-VERIFIED:2026-09-10 master @ d40847e90]: city traffic objects are a first-class per-step
cost on big maps — `private_car_t::sync_step` 18.6% incl / 9.0% self (`hop_check` 7.4% incl);
`pedestrian_t::sync_step` 1.2% self of in-game CPU. (Generation/city side:
[economy-and-passengers](economy-and-passengers.md).)

## Open questions

- Exact obj/ inventory and which families are Extended-only vs. inherited.
