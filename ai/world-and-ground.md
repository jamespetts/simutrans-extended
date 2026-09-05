---
status: stub
verified: none
---
# World & ground

**Covers:** boden/ (incl. boden/wege/ — way tiles), world/ (2 files), simplan.*, finder/, get_climate (source location to verify), dataobj heightfield/relief (verify location → [data-and-pak](data-and-pak.md)).

## Seed facts

- `boden/grund.h` is included by 29 files — the tile/ground hub [CODE ex-15 @ b06e8fa14].
- `boden/wege/weg.cc` touched in 9 ex-15-only commits (whole branch history vs master) [CODE ex-15 @ b06e8fa14].
- `boden/wege/` is a sizable divergence area (2.5% of master→ex-15 changed files) [CODE].
- A contributor terraforming refactor exists as a remote branch (`Ceeac/extended-ex15-terraforming-refactor`), merge status unknown [CODE master @ 3b70dd4b3].

## Planned sections

- Plan/tile model: simplan, coordinates (koord/ribi → [data-and-pak](data-and-pak.md)), climates, heights.
- Grund & way tiles: inventory of boden/ and boden/wege/ classes; way wear (Extended feature; root has a local-only notes file as lead) [UNVERIFIED].
- Terraforming mechanics & ex-15 refactor status → [ex-15](ex-15.md).
- Place-finding (finder/).
- Contents/purpose of the `world/` directory (only 2 files).
- Load/save coupling → [savegame-versioning](savegame-versioning.md).

## Open questions

- Does `world/` duplicate or complement simworld? (Inventory pending.)
- Where does way-wear state live and how is it saved?
