---
status: stub
verified: none
---
# World & ground

**Covers:** boden/ (incl. boden/wege/ — way tiles), world/, simplan.*, finder/, get_climate (source location to verify), dataobj heightfield/relief (verify location → [data-and-pak](data-and-pak.md)).

## Seed facts

- `boden/grund.h` is the tile/ground hub [CODE].
- `boden/wege/` (way tiles) is a notable divergence area between the branches [CODE].
- A contributor terraforming refactor exists as a remote branch (`Ceeac/extended-ex15-terraforming-refactor`), merge status unknown [CODE master].

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
