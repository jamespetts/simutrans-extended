---
status: stub
verified: none
---
# Data, descriptors & pak format

**Covers:** descriptor/ (156 files incl. reader/ and writer/), dataobj/ (46 files: environment, settings, translator, loadsave, koord/ribi — full inventory in breadth pass), makeobj/, io/, ifc/, descriptor/objversion.h, simio.* (root → [simulation-core](simulation-core.md)), text/*.dat translation files, root base_texts*.dat fragments (untracked local artefacts → [repo-map](repo-map.md)).

## Seed facts

- Hubs: `dataobj/environment.h` (112 includers), `dataobj/translator.h` (103), `dataobj/loadsave.h` (55), `descriptor/objversion.h` (28) [CODE ex-15 @ b06e8fa14].
- `dataobj/settings.cc` touched in 22 ex-15 commits since 2025-05-26; `gui/settings_stats.cc` in 11 — settings are actively changing on ex-15 [CODE].
- `loadsave.h:210` mentions a class producing a hash of `savegame_version` — couples to [savegame-versioning](savegame-versioning.md) [CODE].
- makeobj/ contains the pak compiler source plus its own legacy project files; root also holds many `*_writer.obj` build artefacts (ignore) [CODE ex-15 @ b06e8fa14].

## Planned sections

- Descriptor system: desc class hierarchy, reader/writer symmetry, xrefs, obj_base_desc.
- Pak format & node structure (obj_node_info.h); makeobj usage & versioning (MAKEOBJ_VERSION "60.21" on master).
- Pak/obj version vs. savegame version distinction → [savegame-versioning](savegame-versioning.md).
- Settings system: settings.cc/environment.h, how settings serialize (sync-critical? verify → [network](network.md)).
- Translation pipeline: translator, text/*.dat, how base_texts fragments get applied (workflow interview candidate).
- io/ and ifc/ purposes (small dirs; verify).
- dataobj/ full inventory: koord, ribi, loadsave, environment, schedule (→ [routing-and-scheduling](routing-and-scheduling.md)), consist_order_t (→ [vehicles-and-convoys](vehicles-and-convoys.md)).

## Open questions

- Which settings are network-sync-relevant (server enforces?) — verify before touching.
