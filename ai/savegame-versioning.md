---
status: stub
verified: none
---
# Savegame & network versioning

**Covers:** simversion.h; dataobj/loadsave.{h,cc}; simworld.cc rdwr paths; gui/settings_stats.cc; descriptor/objversion.h (pak side → [data-and-pak](data-and-pak.md)).

## Seed facts

- master @ 3b70dd4b3: `SIM_VERSION 123.2.0`, `SIM_SAVE_MINOR 7`, `SIM_SERVER_MINOR 7`, `EX_VERSION 14.23`, `EX_SAVE_MINOR 66` [CODE].
- ex-15 @ b06e8fa14: base identical (123.2 / 7 / 7); `EX_VERSION 15.0`, `EX_SAVE_MINOR 0` — a fresh Extended savegame series for the new major version [CODE].
- Save header strings are composed in simversion.h:75-83: `SAVEGAME_VER_NR` = `"0.<maj>.<SIM_SAVE_MINOR>"`, plus `EXTENDED_VER_NR` (`".<EX_MAJOR>"`) and `EXTENDED_REVISION_NR` (`".<EX_SAVE_MINOR>"`) [CODE].
- In-file coupling warning: "Do not forget to increment the save game versions in settings_stats.cc when changing this" (simversion.h:40) [CODE].
- `loadsave.h:124`: `wr_open(...)` takes `savegame_version`, `savegame_version_ex`, `savegame_revision_ex` separately; `loadsave.h:210` mentions a class producing a hash of `savegame_version` [CODE].
- User statement: the system is versioned; ex-15 has its own version; master recognises that it cannot load a future version and refuses [RECOLLECTION:2026-09-05 — verify against load logic].

## Planned sections (PRIORITY: first depth dive, user-directed)

- Header format & parsing on load; refusal/version-negotiation logic.
- How `EX_SAVE_MINOR` gates feature-level rdwr code across subsystems.
- Server/network version coupling (`SIM_SERVER_MINOR`) → [network](network.md).
- Pak/obj version interaction (`descriptor/objversion.h`, MAKEOBJ_VERSION).
- XML vs. binary saves; hash-of-version class purpose.

## Open questions

- Any interaction between checklist (desync system) and versioning?
- Which code paths implement the "cannot load future version" refusal?
