---
status: draft
verified: ex-15 @ 1b236a4f1
---
# Savegame & network versioning

The *system*: how versions are declared, stamped, parsed, gated and refused. Per
[documentation-architecture](documentation-architecture.md) rule 1, this doc deliberately
does NOT record what particular subsystems store at particular versions — that detail lives
in each class's `rdwr()` code and changes constantly. Mechanism verified identical on
master @ a3cf7d4fa (only version values and a few line numbers differ).

## Version identity (simversion.h)

Three series, all declared in `simversion.h`:
- Base (Standard ancestry): `SIM_VERSION_MAJOR/MINOR/PATCH`, `SIM_SAVE_MINOR`, `SIM_SERVER_MINOR` — identical on both branches (123.2 / 7 / 7) [CODE].
- Extended release: `EX_VERSION_MAJOR/MINOR` — master 14.23, ex-15 15.0 [CODE].
- Extended savegame revision: `EX_SAVE_MINOR` — master 66, ex-15 0 (fresh series for 15.x) [CODE].

Header strings, simversion.h:75-83: `SAVEGAME_VER_NR` = `"0.<major>.<SIM_SAVE_MINOR>"`,
`EXTENDED_VER_NR` = `".<EX_MAJOR>"`, `EXTENDED_REVISION_NR` = `".<EX_SAVE_MINOR>"`;
`EXTENDED_SAVEGAME_VERSION` concatenates prefix + base + extended [CODE].
In-file warning (simversion.h:40): when changing versions, also update gui/settings_stats.cc [CODE].

## Parsing & file classification

- `loadsave_t::int_version()` (dataobj/loadsave.cc, ~1310 ex-15 / ~1314 master) parses `"0.<maj>.<save_minor>[.<ex_ver>[.<ex_rev>]]"` into `extended_version_t {version, extended_version, extended_revision}` (io/classify_file.h); base encodes as `v0*1000000 + v1*1000 + v2` [CODE].
- `classify_file()` (io/classify_file.cc) fills `file_info_t`: format (binary/text/xml × zip/bzip2/zstd), version, pak extension [CODE].

## Loading & refusal

- `loadsave_t::rd_open` (dataobj/loadsave.cc:279-301, both branches): `FILE_STATUS_ERR_NO_VERSION` if unversioned; `FILE_STATUS_ERR_FUTURE_VERSION` if base version > `SIM_VERSION_MAJOR*1000 + SIM_SERVER_MINOR` [CODE].
- `karte_t::load` (simworld.cc, ~8982 ex-15 / ~8928 master): rejects version 0 or base version newer than `env_t::savegame_version_str` [CODE].
- Settings xml (simmain.cc:597-599, identical both branches): file deleted if base, Extended version OR Extended revision newer than the build [CODE].
- Network transfer handles the future-version status at network/network_file_transfer.cc:148 [CODE].

## Saving & target-version selection

- `loadsave_t::wr_open(filename, mode, level, pak_extension, savegame_version, savegame_version_ex, savegame_revision_ex)` (dataobj/loadsave.h:124) [CODE].
- The strings come from `env_t::savegame_version_str / savegame_ex_version_str / savegame_ex_revision_str` (dataobj/environment.h:50-55 — "version for which the savegames should be created"), defaulted to build constants (dataobj/environment.cc) and **user-selectable in the settings dialog** (gui/settings_stats.cc comboboxes from static `version[]`, `version_ex[]`, `revision_ex[]` arrays; gui/loadsave_frame.cc:64 saves via the env_t strings) [CODE].
- Consequence: when saving, `finfo.ext_version` is set from the *target* strings — version gates control what is written as well as what is read [CODE].
- Server-side saves use `SERVER_SAVEGAME_VER_NR` (simworld.cc, network/network_cmd_ingame.cc) [CODE].

## The rdwr gating idiom (what AI-written save code must follow)

- Every persisted class serialises itself in a `rdwr(loadsave_t*)` method using the `rdwr_*` primitives; XML hierarchy via `xml_tag_t` [CODE].
- Gates in use [CODE]:
  - Extended: `is_version_ex_atleast/less/equal(ex_ver, ex_revision)` or explicit `get_extended_version()/get_extended_revision()` combos — ex-15 style e.g. `>= 15 || (== 14 && >= 19)` (boden/wege/weg.cc, simworld.cc).
  - Legacy base: `is_version_atleast(major, save_minor)` (e.g. dataobj/koord3d.cc 99,5 / 112,7).
- Rules when adding persisted data:
  1. **Append only** — never reorder or remove existing rdwr entries; streams are positional.
  2. Wrap new fields in a gate for the Extended version/revision that introduces them; default-initialise members before the gate so old saves load sanely.
  3. Keep gates **symmetric** — the same gate governs writing (target may be an older user-selected version) and reading.
  4. Sync-critical data has network-checksum implications → [network](network.md) (ring-fenced).
- Version bumps are RARE and ring-fenced (AGENTS.md rule 4): `EX_SAVE_MINOR` (+ `EX_VERSION_*` for majors) in simversion.h AND the settings_stats.cc arrays must be extended together (simversion.h:40 warning); surface to the user before proceeding.

## Network/desync coupling

- Hash-dump files ("hashes" extension) written with `SAVEGAME_VER_NR` (simworld.cc ~10460 ex-15; network/network_cmd_ingame.cc:745); `stream_loadsave_t` ("produce hash of savegame_version") and `compare_loadsave_t` (dataobj/loadsave.h:209-226) support desync comparison → [network](network.md) [CODE].

## Open questions

- **Extended-series future refusal for savegames**: `rd_open` and `karte_t::load` check only the BASE series; Extended-series refusal is verified only for settings.xml (simmain.cc). Does a 14.x client gracefully refuse a 15.x savegame, or fail positionally? Risk: corruption rather than clean refusal. [UNVERIFIED — resolve before any 15.x save is exposed to 14.x clients]
- Does the load/save UI (gui/loadsave_frame.cc, dataobj/sve_cache.cc) filter or mark version-incompatible files?
- Is ex-15's `EX_SAVE_MINOR = 0` intended to stay 0 until the 15.0 release? (Ask user.)
