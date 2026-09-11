---
status: reviewed
verified: none
---
# Knowledge base index

Retrieval protocol: choose docs from the **read when** keys below; load only what the task
needs; never bulk-load this folder. Claim tags and doc rules: [conventions](conventions.md).
Agent hard rules: [../AGENTS.md](../AGENTS.md).

Simulation and presentation domain docs each carry a **Performance hotspots** section: the
measured hot spots for that area in summary, so performance-aware work does not require reading
[performance](performance.md) (which holds the full inventory, the profiling method, and the
measured provenance) first.

Note: the documents may be nested. Only top level documents are shown here.

## Orientation

- [project-overview](project-overview.md) — read when context is needed on what this project is, where it came from, and how it relates to Standard Simutrans.
- [glossary](glossary.md) — read when Simutrans/Extended vocabulary is unclear in code, comments, or conversation.
- [repo-map](repo-map.md) — read before navigating the tree; to tell real source from clutter, ignored artefacts, or dead legacy files.
- [conventions](conventions.md) — read before writing/updating any doc.
- [claim-tags](claim-tags.md) — read before interpreting claims or statuses.
- [project-notes](project-notes.md) — read at the start of most code tasks: short cross-cutting notes that are easy to miss.
- [known-bugs](known-bugs.md) — read when starting any bug-fix task, triaging a failure, or after fixing any bug (the list must be updated); open bugs only, priority-ranked; entries are unverified forum-derived leads (doc exempt from user review — never mark reviewed).
- [project-architecture](project-architecture.md) — read when designing new systems or making cross-cutting changes: mandatory architectural constraints and invariants.
- [documentation-architecture](documentation-architecture.md) — read before authoring or restructuring any KB doc: what belongs in docs vs. code; visibility, size and canonical-location rules.
- [ex-15](ex-15.md) — read for ANY work on the ex-15 branch: the next major version, feature status, branch topology, contributors.

## Design & planning

- [high-level-design-goals](high-level-design-goals.md) — read when doing any design work on a feature: the project's normative design goals (realism, economics, multiplayer balance) that new features must serve.
- [project-roadmap](project-roadmap.md) — read when planning what features to add in what order: registry of planned features with balance-critical/simple/15.x markers; also check after implementing a feature to close its entry (per [conventions](conventions.md)).

## Build, data & infrastructure

- [build-and-toolchain](build-and-toolchain.md) — read when compiling anything, touching build files/CI, packaging releases or nightlies, dealing with the Bridgewater-Brunel VPS/server pipeline, or dealing with backend libraries (SDL, PNG, zlib, ICU).
- [savegame-versioning](savegame-versioning.md) — MANDATORY read before touching load/save code, simversion.h, rdwr methods, or anything version-negotiation related (AGENTS.md rule 5).
- [data-and-pak](data-and-pak.md) — read when touching descriptors (desc classes), pak/pakset data, makeobj, settings, or the dataobj/ layer.
- [translations](translations.md) — read when adding, changing or debugging any user-facing text: translation keys, language (.tab) files, Simutranslator workflow, city/street name lists, in-game help pages, or pak-embedded object texts; deeper subdocs are keyed from it.
- [network](network.md) — read when changing the networking system itself: transport/packets, command framework, connection lifecycle, frame sync & pacing, checklist mechanics, admin/nettools, announcement. Change-restricted (AGENTS.md rule 5). For ordinary simulation-code work you normally need sync-and-determinism instead.

## Simulation

- [sync-and-determinism](sync-and-determinism.md) — MANDATORY read when touching ANY code that changes simulation state (step()/sync_step() work, tools, RNG, threading of simulation work, rdwr of synced state): the rules that keep server/clients in sync. Sync-sensitive (AGENTS.md rule 5).
- [threading](threading.md) — MANDATORY read when touching the threading MACHINERY itself: adding/removing/reconfiguring threads, thread lifecycle and the start_*/await_* helpers, barriers/mutexes/semaphores, thread_local state, per-thread buffers, display/save/map-loop threads, diagnosing races/deadlocks/hangs, MULTI_THREAD build configuration. Routine simulation changes that merely run within the existing threaded patterns do NOT need this doc — the rules for threaded simulation work live in [sync-and-determinism](sync-and-determinism.md), which is sufficient there.
- [simulation-core](simulation-core.md) — read when touching root-level sim*.cc/h: world, main loop, stepping, tools, interaction, events, messages, units, memory, debug.
- [world-and-ground](world-and-ground.md) — read when touching the map/plan, ground tiles, ways, climates, terraforming, or place-finding.
- [objects](objects.md) — read when touching obj/: things placed on tiles (buildings, signals, signs, trees, pedestrians, city cars, labels, piers, etc.).
- [vehicles-and-convoys](vehicles-and-convoys.md) — read when touching vehicles, consists, convoys, depots, lines, or vehicle descriptors. **ex-15 hotspot.**
- [economy-and-passengers](economy-and-passengers.md) — read when touching factories/industries, cities, goods, players/finance, or passenger generation.
- [routing-and-scheduling](routing-and-scheduling.md) — read when touching path exploration, routing, stops (halts), connections, or schedules. **ex-15 hotspot.**
- [signals-and-blocks](signals-and-blocks.md) — read when touching signals, signalboxes, reservations, block working, or train movement authority. Sync-sensitive (AGENTS.md rule 5).

## Presentation & platform

- [gui](gui.md) — read when touching windows, frames, GUI components, themes, or any user-interface work.
- [rendering](rendering.md) — read when touching drawing, graphics backends, images, colours, or the viewport.
- [scripting-and-tests](scripting-and-tests.md) — read when touching the Squirrel API, scenarios, or the automated tests in tests/.
- [performance](performance.md) — read when doing ANY performance work, profiling or benchmarking, or when a change touches code that runs under step()/sync_step(): the canonical profiling suite (gargantuan-save fixture, scripts/run-perf-suite.ps1) and the full hotspot inventory. Per-area hotspot summaries live in the domain docs themselves (see the protocol note above).
- [utilities](utilities.md) — read when touching tpl/ containers, utils/, sys/ platform backends, sound/music, or unicode/ICU. Threading → [threading](threading.md).
