---
status: reviewed
verified: master @ c3f98d5a0
---
# City & street names

**Covers:** translator name-list machinery (`init_custom_names`, `name_list_hash`, `load_custom_list`, `get_city_name_list`, `get_street_name_list`), town founding (simcity.cc), stop naming (simhalt.cc), the world init/load rebuild calls and `calc_name_list_seed` (simworld.cc), citylist_/streetlist_ files, the syllable keys. Basics: [../translations](../translations.md).

Read when touching town or street naming, citylist/streetlist files, per-region name lists, or the `%X_CITY_SYLL`-style translation keys.

## Two name sources

1. **Explicit lists**: `citylist_<iso>.txt` / `streetlist_<iso>.txt` — one name per line, `#` comments, recoded to UTF-8 at load.
2. **Deterministic generation from translation keys** (fallback when no list is found; cities only — street lists come only from files): the syllable keys in the base `<iso>.tab`.

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

## Generation from syllable keys (translator::init_custom_names)

World-scoped: called ONLY from `karte_t::init` (before map-creation town founding) and `karte_t::load` (single-player AND network), with the game's name language (`settings.get_name_language_id()` — a savegame setting, independent of the UI language). NOT called on UI language changes: town names are game state and must not depend on per-peer UI settings. Each call clears and rebuilds all 16 region lists. The server's post-load name-language override ("language of map becomes server language") deliberately does NOT rebuild: all peers' lists must follow the loaded save's setting.

- Per region: every `%X_CITY_SYLL` key is paired with every `&X_CITY_SYLL` key (hex indices; `[n]` region variants fall back to `[0]`) to form full names.
- `&X_CITY_PREFIX`/`&X_CITY_SUFFIX` entries are attached to a small subset of names (~5-6% each, capped per name), selected by a deterministic integer hash (`name_list_hash`) instead of RNG, seeded from `calc_name_list_seed` (simworld.cc): a hash of the saved `map_number`, `size_x`, `size_y`, `city_count` and `mean_citizen_count` — values that are rdwr'd and cannot be overridden from local simuconf.tab files after load, so every peer computes the same seed. Different map settings give different name sets even for a re-used map number.
- De-duplication heuristics avoid names like "Tarwoodwood" or "Bumblewick Wick".

## Consumers

- Town founding (`stadt_t::stadt_t`, simcity.cc): exactly ONE `simrand` draw for the starting index (code comment: "to avoid desyncs in network games"), then a prime-offset walk for uniqueness; falls back to "simcity".
- Stop naming (`haltestelle_t::create_name`, simhalt.cc): same one-draw pattern against the STREET list of the stop's region; called from the station-building tools (simtool.cc), from stop tile-addition/rename-on-collision (`haltestelle_t::add_grund`) and from the passenger AI. The AI runs in lockstep on every peer with no command that could carry a name — which is why identical lists on all peers (not transmitted names) are the only sync mechanism covering every naming path.
- List generation happens at world init/load — not per founding.

## Sync sensitivity

- The index draws use the synced `simrand`, so list CONTENTS must be identical on all peers. This is guaranteed by deterministic generation seeded from saved settings plus the world-scoped rebuild (above). RNG must not be used in generation: unsynced `sim_async_rand` gives every process different lists, and drawing from the synced stream at init/load time would diverge peers (a mid-game joiner re-baselines its RNG from the save; generation draws would then shift its stream). Rule: [../sync-and-determinism](../sync-and-determinism.md) — RNG draws must not depend on data that can differ between peers.
- Failure mode if lists ever do differ (e.g. peers with different text files): NOT a checklist desync — `simrand(max)` consumes one draw regardless of `max` (utils/simrandom.cc) — but silently different town/stop names on each peer: divergent saved state that only heavy-mode hashing catches. Sole stream-divergence exception: `max<=1` returns without drawing.
- Residual requirement: peers must hold identical language text data (`<iso>.tab` syllable keys, citylist/streetlist files); identical pakset text is a de-facto network-play requirement.

## Design intent & observed data

- Base `en.tab` contains the full syllable/prefix/suffix key set.
- Pak128.Britain-Ex is intended to use the fragment (syllable) system; complete-name citylists are somewhat deprecated there. The installed pakset's `citylist_en_gb.txt`-style files were alternatives to be manually switched in and out in place of plain `citylist_en.txt` for different countries' English names — superseded by the fragment system [RECOLLECTION:2026-09-08]. The pakset sources keep the lists under `text/citylists/` [local-only, separate repository].

## Known problems

- Enlarge-map and rotation change the map dimensions mid-session, but lists are rebuilt only at init/load, so a later reload derives a different decorated name subset. Single-player cosmetic only: no mid-session rebuild, founded names are saved, and network play cannot enlarge maps.
- Cosmetic off-by-ones in the affix selection gates: registered in [../known-bugs](../known-bugs.md) (P4).

## Open questions

- Should re-using a map number with otherwise identical settings (size, city count, mean citizen count — which also reproduces the same terrain) produce different town names? Guaranteeing that needs a dedicated per-game name seed stored in settings: a savegame format change (AGENTS.md rule 5).
