---
status: reviewed
verified: master @ 84b8345a4
---
# Pak-embedded texts (Text nodes)

**Covers:** descriptor/text_desc.h, descriptor/reader/text_reader.*, descriptor/writer/text_writer.*, and their consumers (vehicle_desc.h/cc, obj_writer, good_writer). Basics: [../translations](../translations.md).

Read when touching how strings are stored inside `.pak` files: object internal names, copyright, accommodation-class names, livery-scheme names — or when changing makeobj's writers or the readers' child-node layout.

## Mechanism

- makeobj writes opaque Text nodes: `text_writer_t::write_obj` (descriptor/writer/text_writer.cc) stores the raw string plus terminator as the node's data. There is no type or field tag — **position within the parent's child list is the only identity**.
- Game side: `text_reader_t::read_node` (descriptor/reader/text_reader.cc) allocates a `text_desc_t` (flexible `char text[]`, accessor `get_text()`); the reader/writer instances are registered in sim_reader.cc / sim_writer.cc.
- These are NOT translations: one language-independent string per pak.

## What gets embedded

- `obj_writer_t::write_head` (descriptor/writer/obj_writer.cc): every pak object's head = `name` (the internal name from the .dat `name=` field) + `copyright`.
- `good_writer_t` (descriptor/writer/good_writer.cc): the `metric` field.
- `vehicle_writer_t` (descriptor/writer/vehicle_writer.cc): `accommodation_name[n]` — one per capacity class, empty values stored as `"\0"` placeholders; `liverytype[n]` — one per livery image variant; plus a terminal `"default"` pseudo-livery whenever liveries exist.

## Consumers

- `vehicle_desc_t::get_accommodation_name` (descriptor/vehicle_desc.cc): index arithmetic over the child list (`get_add_to_node() + trailer_count + leader_count + upgrades + …`) — child ORDER is load-bearing.
- `vehicle_desc_t::get_image_id` / `check_livery` (descriptor/vehicle_desc.h): livery image variants are selected by `strcmp` against the embedded livery names; `"default"` selects variant 0.
- Display names are NOT here: the pakset `text/*.tab` overlay maps internal name → display name (`translator::translate(desc->get_name())` at the call sites).

## Hazard

- Because Text nodes are positional, ANY change to a pak's child-node layout (new fields before the text blocks, writer reordering) silently shifts the indices used by `get_accommodation_name`/`get_image_id`. This couples to pak/save versioning → [../savegame-versioning](../savegame-versioning.md), [../data-and-pak](../data-and-pak.md). Any writer change must be checked against the matching reader's index arithmetic in the same change.

## Open questions

- Which code consumes the goods `metric` Text node (the writer exists; a reader-side consumer was not located)?
