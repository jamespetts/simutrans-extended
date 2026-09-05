---
status: stub
verified: none
---
# Vehicles, consists & convoys

**Covers:** vehicle/ (17 files), bauer/vehikelbauer.*, convoy.cc/h + convoihandle_t.h, simconvoi.*, simdepot.*, simline.* + linehandle_t.h, simlinemgmt.*, dataobj/consist_order_t.* (ex-15 only), gui/consist_order_gui.*, gui/components/gui_convoy_assembler.* (→ [gui](gui.md)), descriptor/vehicle_desc.*, descriptor/writer/vehicle_writer.cc, descriptor/reader/vehicle_reader.cc.

## Seed facts

- The most-churned area of ex-15 (ex-15-only commits, whole branch history vs master): `simconvoi.cc` (112 commits), `gui/consist_order_gui.cc` (92), `vehicle/vehicle.cc` (35), `descriptor/vehicle_desc.cc` (33), `gui/convoi_detail_t.cc` (33), `simdepot.cc` (20), `simline.cc` (17) [CODE ex-15 @ b06e8fa14].
- `dataobj/consist_order_t.{cc,h}` exists only on ex-15 (added vs. master) — consist ordering is the signature in-progress ex-15 feature here [CODE].
- `simconvoi.h` has 30 includers [CODE ex-15 @ b06e8fa14].

## Planned sections

- Convoy lifecycle & state machine (simconvoi/convoy split — why two files? verify).
- Consist ordering: data model, GUI, save/load (ex-15; → [ex-15](ex-15.md) registry).
- Lines vs. convoys (simline/simlinemgmt); line replacement work (ex-15 branches exist) [CODE master @ 3b70dd4b3].
- Depots (simdepot; depot GUI → [gui](gui.md)).
- Vehicle physics hooks: braking/acceleration (Extended-specific — verify scope) [PRIOR → verify].
- Descriptors & makeobj side → [data-and-pak](data-and-pak.md).
- Load/save coupling → [savegame-versioning](savegame-versioning.md).

## Open questions

- How complete is consist ordering on ex-15 (registry will answer, code-first)?
