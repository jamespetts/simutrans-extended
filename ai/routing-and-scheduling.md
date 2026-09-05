---
status: stub
verified: none
---
# Routing, halts & scheduling

**Covers:** path_explorer.*, simhalt.* + halthandle_t.h, dataobj/schedule.*, dataobj/schedule_entry.h, gui/schedule_gui.*, gui/schedule_list.* (→ [gui](gui.md)), route-finding classes (location to verify), finder/ (→ [world-and-ground](world-and-ground.md)).

## Seed facts

- ex-15 hotspots (ex-15-only commits, whole branch history vs master): `gui/schedule_gui.cc` (102 commits), `dataobj/schedule.cc` (52), `dataobj/schedule.h` (22), `dataobj/schedule_entry.h` (16), `path_explorer.cc` (20), `simhalt.cc` (15) [CODE ex-15 @ b06e8fa14].
- `simhalt.h` has 35 includers [CODE ex-15 @ b06e8fa14].
- The schedule system is under heavy active revision on ex-15 → [ex-15](ex-15.md) registry.

## Planned sections

- Routing pipeline: passenger & goods routing; where the route-search implementation lives (path_explorer vs. a route class — verify, do not assume).
- path_explorer: role, threading model, and interaction with the main loop (→ [utilities-and-threading](utilities-and-threading.md)) [PRIOR → verify].
- Connection scoring: Extended criteria (journey time, congestion, comfort, classes — verify which exist and where) [PRIOR → verify].
- Halts: catchments, coverage, platform/stop mechanics (Extended-specific elements to identify).
- Schedule system & the ex-15 overhaul (intent + implemented state, code-first).
- Load/save coupling → [savegame-versioning](savegame-versioning.md).

## Open questions

- Is there a `route` class distinct from path_explorer, and who calls which?
- What exactly changed in schedule_entry on ex-15?
