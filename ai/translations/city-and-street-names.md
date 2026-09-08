---
status: reviewed
verified: master @ 84b8345a4
---
# City & street names

**Covers:** translator name-list machinery (`init_custom_names`, `load_custom_list`, `get_city_name_list`, `get_street_name_list`), town founding (simcity.cc), stop naming (simhalt.cc), world-init call (simworld.cc), citylist_/streetlist_ files, the syllable keys. Basics: [../translations](../translations.md).

Read when touching town or street naming, citylist/streetlist files, per-region name lists, or the `%X_CITY_SYLL`-style translation keys.

## Two name sources

1. **Explicit lists**: `citylist_<iso>.txt` / `streetlist_<iso>.txt` — one name per line, `#` comments, recoded to UTF-8 at load.
2. **Random generation from translation keys** (fallback when no list is found): the syllable keys in the base `<iso>.tab`.

## List loading (translator::load_custom_list)

Search order, first hit wins (`fileprefix` = `citylist_`/`streetlist_`):
1. `<user_dir>addons/<pakset>text/<prefix><iso_base>.txt`
2. `<user_dir><prefix><iso_base>.txt`
3. `<data_dir><pakset>text/<prefix><iso_base>.txt`
4. `<data_dir>text/<prefix><iso_base>.txt`

The name always uses `iso_base` (two letters): `citylist_en.txt`, never `citylist_en_gb.txt`.

## Regions (Extended-specific)

- Maps have up to 16 name regions; a town knows its region via `karte_t::get_region(pos)`.
- Region 0: `citylist_<iso>.txt` is tried, then `citylist[0]_<iso>.txt`. Region n: `citylist[n]_<iso>.txt` (same for streetlist).
- `get_city_name_list(region)`/`get_street_name_list(region)` fall back to region 0's list when a region has none.

## Random generation (translator::init_custom_names)

Runs whenever the language is set, and at world init with `settings.get_name_language_id()` — the town-name language is a savegame setting, independent of the UI language.

- Per region: every `%X_CITY_SYLL` key is paired with every `&X_CITY_SYLL` key (hex indices; `[n]` region variants fall back to `[0]`) to form full names.
- `&X_CITY_PREFIX`/`&X_CITY_SUFFIX` entries are attached to a small random subset of names (~5% each) — selected with `sim_async_rand` (the unsynced RNG: list contents are NOT identical across machines).
- De-duplication heuristics avoid names like "Tarwoodwood" or "Bumblewick Wick".

## Consumers

- Town founding (`stadt_t::stadt_t`, simcity.cc): exactly ONE `simrand` draw for the starting index (code comment: "to avoid desyncs in network games"), then a prime-offset walk for uniqueness; falls back to "simcity".
- Stop naming (simhalt.cc): stops take their names from the STREET list of the stop's region.
- List generation itself happens at language load / world init — not per founding.

## Sync sensitivity

- Index draws use the synced `simrand`; but syllable-generated list contents (and LENGTH) depend on `sim_async_rand`, so peers without a citylist file can hold different lists → a different `count` → the single `simrand(count)` draw diverges the synced RNG stream. With identical citylist files (the normal case) the lists are identical and this is safe. Rule: [../sync-and-determinism](../sync-and-determinism.md) — RNG draws must not depend on data that can differ between peers.

## Design intent & observed data

- Base `en.tab` contains the full syllable/prefix/suffix key set.
- Pak128.Britain-Ex is intended to use the fragment (syllable) system; complete-name citylists are somewhat deprecated there. The installed pakset's `citylist_en_gb.txt`-style files were alternatives to be manually switched in and out in place of plain `citylist_en.txt` for different countries' English names — superseded by the fragment system [RECOLLECTION:2026-09-08]. The pakset sources keep the lists under `text/citylists/` [local-only, separate repository].

## Known problems

- In `init_custom_names`' suffix loop, the per-name gate compares `random_percent_suffix > prefix_probability`; `suffix_probability` is defined but unused there — suspected copy-paste bug (dataobj/translator.cc).

## Open questions

- Does the syllable-path list-length divergence (sync section) actually desync live network games in practice, when no citylist file is present?
