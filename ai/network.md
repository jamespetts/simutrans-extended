---
status: stub
verified: none
---
# Network & multiplayer

**Covers:** network/, utils/checklist.*, nettools/, SIM_SERVER_MINOR + announce infrastructure in simversion.h (→ [savegame-versioning](savegame-versioning.md)).

## Seed facts

- Server announcement infrastructure: `ANNOUNCE_SERVER list.extended.simutrans.org:8080`, plus IP-query URLs, defined in simversion.h [CODE].
- `utils/checklist.{cc,h}` implement the desync-detection checklist system (purpose inferred from name + local artefacts; verify) [UNVERIFIED].
- Root holds local-only historical artefacts: "2019 server signal desync logs.txt", "Commands for debugging server.txt" — desync debugging history; leads for the gotchas section [local-only].
- `SERVER_SAVEGAME_VER_NR` (simversion.h) couples network compatibility to versioning → [savegame-versioning](savegame-versioning.md) [CODE].

## Planned sections

- Protocol & packet structure (network/ inventory).
- Client/server modes; game-state synchronisation model (what is transmitted vs. recomputed).
- Checklist & desync mechanics: how checksums are taken/compared; which subsystems feed it (signals/reservations historically implicated — → [signals-and-blocks](signals-and-blocks.md)).
- Settings enforcement across the network (→ [data-and-pak](data-and-pak.md)).
- Version negotiation on connect → [savegame-versioning](savegame-versioning.md).
- nettools/ purpose; debugging workflows (interview + local artefacts).
- Threading interactions (→ [utilities-and-threading](utilities-and-threading.md)).

## Open questions

- Which code paths are checklist-relevant (the change-restricted set of AGENTS.md rule 4 needs to be made concrete).
- Is the BB server's desync tooling part of the repo or external? (Interview.)
