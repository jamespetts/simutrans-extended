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

## Performance hotspots

Measured on the gargantuan fixture (method and full inventory: [performance](performance.md))
[EXECUTION-VERIFIED:2026-09-10 master @ d40847e90]:
- A* route search (`route_t::intern_calc_route`) is minor in steady state (`route_t::find_route`
  ~1.2% self) but there is a mass reroute wave right after loading large saves; heuristic-failure
  diagnostics (`heur` ~10x `cost`) fire continuously at -debug >= 2.
- The path explorer runs concurrently and only when the network changed; it was dormant (0.01%)
  in a quiet 120 s window — it governs how quickly in-game routes update, not framerate.
- Halt cargo handling: `karte_t::check_transferring_cargoes` 2.6% self.

## Open questions

- Is there a `route` class distinct from path_explorer, and who calls which?
- What exactly changed in schedule_entry on ex-15?
