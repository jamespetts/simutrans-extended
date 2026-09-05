---
status: reviewed
verified: ex-15 @ be23f4203
---
# Savegame & network versioning

The *system*: how versions are declared, written into files, parsed, applied as conditions,
and refused. Per
[documentation-architecture](documentation-architecture.md) rules 1 and 7, this doc
deliberately does NOT record what particular subsystems store at particular versions,
nor current version values — that detail is in each class's `rdwr()` code and in
`simversion.h`, and changes constantly. Mechanism verified identical on both branches.

## Version identity (simversion.h)

Three series, all declared in `simversion.h` (the sole authority for current values):
- Base (inherited from Standard): `SIM_VERSION_MAJOR/MINOR/PATCH`, `SIM_SAVE_MINOR`, `SIM_SERVER_MINOR` [CODE].
- Extended release: `EX_VERSION_MAJOR/MINOR` — the fork's own game version (14.x on master, 15.x on ex-15) [CODE].
- Extended savegame revision: `EX_SAVE_MINOR` — ex-15 started a new series [CODE].

Header strings (simversion.h): `SAVEGAME_VER_NR` = `"0.<major>.<SIM_SAVE_MINOR>"`,
`EXTENDED_VER_NR` = `".<EX_MAJOR>"`, `EXTENDED_REVISION_NR` = `".<EX_SAVE_MINOR>"`;
`EXTENDED_SAVEGAME_VERSION` concatenates prefix + base + extended [CODE].
In-file warning (simversion.h): when changing versions, also update gui/settings_stats.cc [CODE].

## Parsing & file classification

- `loadsave_t::int_version()` (dataobj/loadsave.cc) parses `"0.<maj>.<save_minor>[.<ex_ver>[.<ex_rev>]]"` into `extended_version_t {version, extended_version, extended_revision}` (io/classify_file.h); base encodes as `v0*1000000 + v1*1000 + v2` [CODE].
- `classify_file()` (io/classify_file.cc) fills `file_info_t`: format (binary/text/xml × zip/bzip2/zstd), version, pak extension [CODE].

## Loading & refusal

- `loadsave_t::rd_open` (dataobj/loadsave.cc): `FILE_STATUS_ERR_NO_VERSION` if unversioned; `FILE_STATUS_ERR_FUTURE_VERSION` if base version > `SIM_VERSION_MAJOR*1000 + SIM_SERVER_MINOR` [CODE].
- `karte_t::load` (simworld.cc): rejects version 0 or base version newer than `env_t::savegame_version_str` [CODE].
- Settings xml (simmain.cc): file deleted if base, Extended version OR Extended revision newer than the build [CODE].
- Network transfer handles the future-version status in network/network_file_transfer.cc [CODE].

## Saving & target-version selection

- `loadsave_t::wr_open(filename, mode, level, pak_extension, savegame_version, savegame_version_ex, savegame_revision_ex)` (dataobj/loadsave.h) [CODE].
- The strings come from `env_t::savegame_version_str / savegame_ex_version_str / savegame_ex_revision_str` (dataobj/environment.h — "version for which the savegames should be created"), defaulted to build constants (dataobj/environment.cc) and **user-selectable in the settings dialog** (gui/settings_stats.cc comboboxes from static `version[]`, `version_ex[]`, `revision_ex[]` arrays; gui/loadsave_frame.cc saves via the env_t strings) [CODE].
- Consequence: when saving, `finfo.ext_version` is set from the *target* strings — version conditions control what is written as well as what is read [CODE].
- Server-side saves use `SERVER_SAVEGAME_VER_NR` (simworld.cc, network/network_cmd_ingame.cc) [CODE].

## Rules for rdwr serialization code (what AI-written save code must follow)

- Every persisted class serialises itself in a `rdwr(loadsave_t*)` method using the `rdwr_*` primitives; XML hierarchy via `xml_tag_t` [CODE].
- Version conditions in use [CODE]:
  - Extended: `is_version_ex_atleast/less/equal(ex_ver, ex_revision)` or explicit `get_extended_version()/get_extended_revision()` combos — ex-15 style e.g. "atleast 15, or 14 with a minimum revision" (boden/wege/weg.cc, simworld.cc).
  - Legacy base: `is_version_atleast(major, save_minor)` (e.g. dataobj/koord3d.cc).
- Rules when adding persisted data [RECOLLECTION:2026-09-05 user statement]:
  1. Each new datum's save and load must be conditional upon the exact same version number (Extended version/revision). The version condition determines both writing and reading, because the user may select a save target version older than the build.
  2. Data not loaded because the file's version is too old must always be assigned a sensible default value (assign defaults to member variables before the conditional block).
  3. Do not alter or remove existing serialization entries — saves written by existing versions must continue to load.
  4. The position of a new entry within a block does not strictly matter provided its version condition is correct; by convention new data goes at the end of the block that writes the relevant type, because this makes the code more readable.
  5. Do not change data affecting network sync without following AGENTS.md rule 4 → [network](network.md).
- Version bumps are rare, and changing them is restricted by AGENTS.md rule 4: `EX_SAVE_MINOR` (+ `EX_VERSION_*` for majors) in simversion.h AND the settings_stats.cc arrays must be extended together (simversion.h warning); present the change to the user before proceeding.

## Network/desync coupling

- Hash-dump files ("hashes" extension) written with `SAVEGAME_VER_NR` (simworld.cc; network/network_cmd_ingame.cc); `stream_loadsave_t` ("produce hash of savegame_version") and `compare_loadsave_t` (dataobj/loadsave.h) support desync comparison → [network](network.md) [CODE].

## Open questions

- **Extended-series future refusal for savegames**: `rd_open` and `karte_t::load` check only the BASE series; Extended-series refusal is verified only for settings.xml (simmain.cc). Intended behaviour: a 14.x client should refuse a 15.x savegame cleanly; if it instead fails during parsing, that is a bug [RECOLLECTION:2026-09-05]. Whether the code actually refuses cleanly is [UNVERIFIED — verify and fix if needed before any 15.x save is exposed to 14.x clients].
- Does the load/save UI (gui/loadsave_frame.cc, dataobj/sve_cache.cc) filter or mark version-incompatible files? (Verify from code in a later pass.)
- Is ex-15's new `EX_SAVE_MINOR` series intended to stay at its initial value until the 15.x release, or to increment during development? (Undecided as at 2026-09-05 — ask again later.)
