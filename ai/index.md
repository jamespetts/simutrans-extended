---
status: draft
verified: none
---
# Knowledge base index

Retrieval protocol: choose docs from the **read when** keys below; load only what the task
needs; never bulk-load this folder. Claim tags and doc rules: [conventions](conventions.md).
Agent hard rules: [../AGENTS.md](../AGENTS.md).

## Orientation

- [project-overview](project-overview.md) — read when context is needed on what this project is, where it came from, and how it relates to Standard Simutrans.
- [glossary](glossary.md) — read when Simutrans/Extended vocabulary is unclear in code, comments, or conversation.
- [repo-map](repo-map.md) — read before navigating the tree; to tell real source from clutter, ignored artefacts, or dead legacy files.
- [conventions](conventions.md) — read before interpreting claim tags/stamps, or writing/updating any doc.
- [ex-15](ex-15.md) — read for ANY work on the ex-15 branch: the next major version, feature status, branch topology, contributors.

## Build, data & infrastructure

- [build-and-toolchain](build-and-toolchain.md) — read when compiling anything, touching build files/CI, packaging releases, or dealing with backend libraries (SDL, PNG, zlib, ICU).
- [savegame-versioning](savegame-versioning.md) — MANDATORY read before touching load/save code, simversion.h, rdwr methods, or anything version-negotiation related (AGENTS.md rule 4).
- [data-and-pak](data-and-pak.md) — read when touching descriptors (desc classes), pak/pakset data, makeobj, settings, translation files, or the dataobj/ layer.
- [network](network.md) — read when touching multiplayer code, the checklist/desync system, client-server behaviour, or nettools. Ring-fenced (AGENTS.md rule 4).

## Simulation

- [simulation-core](simulation-core.md) — read when touching root-level sim*.cc/h: world, main loop, stepping, tools, interaction, events, messages, units, memory, debug.
- [world-and-ground](world-and-ground.md) — read when touching the map/plan, ground tiles, ways, climates, terraforming, or place-finding.
- [objects](objects.md) — read when touching obj/: things placed on tiles (buildings, signals, signs, trees, pedestrians, city cars, labels, piers, etc.).
- [vehicles-and-convoys](vehicles-and-convoys.md) — read when touching vehicles, consists, convoys, depots, lines, or vehicle descriptors. **ex-15 hotspot.**
- [economy-and-passengers](economy-and-passengers.md) — read when touching factories/industries, cities, goods, players/finance, or passenger generation.
- [routing-and-scheduling](routing-and-scheduling.md) — read when touching path exploration, routing, stops (halts), connections, or schedules. **ex-15 hotspot.**
- [signals-and-blocks](signals-and-blocks.md) — read when touching signals, signalboxes, reservations, block working, or train movement authority. Sync-sensitive (AGENTS.md rule 4).

## Presentation & platform

- [gui](gui.md) — read when touching windows, frames, GUI components, themes, or any user-interface work.
- [rendering](rendering.md) — read when touching drawing, graphics backends, images, colours, or the viewport.
- [scripting-and-tests](scripting-and-tests.md) — read when touching the Squirrel API, scenarios, or the automated tests in tests/.
- [utilities-and-threading](utilities-and-threading.md) — read when touching tpl/ containers, utils/, sys/ platform backends, threading, sound/music, or unicode/ICU.
