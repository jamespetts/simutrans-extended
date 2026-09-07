---
status: stub
verified: none
---
# Routing, halts & scheduling

**Covers:** path_explorer.*, simhalt.* + halthandle_t.h, dataobj/schedule.*, dataobj/schedule_entry.h, gui/schedule_gui.*, gui/schedule_list.* (→ [gui](gui.md)), route-finding classes (location to verify), finder/ (→ [world-and-ground](world-and-ground.md)).

## Initial facts

- The schedule system (`dataobj/schedule.*`, `schedule_entry.h`) and its GUI (`gui/schedule_gui.cc`) are heavy ex-15 hotspots, alongside `path_explorer.cc` and `simhalt.cc` [CODE].
- `simhalt.h` is widely included [CODE].
- The schedule system is under heavy active revision on ex-15 → [ex-15](ex-15.md) registry.

## Planned sections

- Routing pipeline: passenger & goods routing; where the route-search implementation is (path_explorer vs. a route class — verify, do not assume).
- path_explorer: role and interaction with the main loop; threading model documented in (→ [threading](threading.md)).
- Connection scoring: Extended criteria (journey time, congestion, comfort, classes — verify which exist and where) [PRIOR → verify].
- Halts: catchments, coverage, platform/stop mechanics (Extended-specific elements to identify).
- Schedule system & the ex-15 overhaul (intent + implemented state, code-first).
- Load/save coupling → [savegame-versioning](savegame-versioning.md).

## Open questions

- Is there a `route` class distinct from path_explorer, and who calls which?
- What exactly changed in schedule_entry on ex-15?
