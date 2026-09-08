---
status: reviewed
verified: master @ 84b8345a4
---
# Translator internals

**Covers:** dataobj/translator.cc/h in full (loading, encoding, lookup, switching), gui/sprachen.cc, gui/help_frame.cc page loading, language resolution in simmain.cc, get_lang_files.sh, simutrans/text/ layout. Basics and file locations: [../translations](../translations.md).

Read when debugging or changing the text system itself: file discovery and load order, encodings, format-string validation, language switching, fonts, date/number formatting, compat.tab, the language cap.

## Architecture

- Singleton `translator` plus file statics (dataobj/translator.cc): `langs[40]` (hard cap; `lang_count` live count), `current_langinfo`, the `compatibility` hashtable, city/street name lists (→ [city & street names](city-and-street-names.md)).
- `translator::lang_info` per language: `stringhashtable_tpl texts`; `name`, `iso` (e.g. `en_GB`), `iso_base` (part before `_`, e.g. `en`), `is_latin2_based`, `highest_character`, `ellipsis_width`.
- Lookup (`lang_info::translate`): exact C-string hash lookup; NULL → `"(null)"`; empty → itself; miss → returns the KEY unchanged.
- `translator::translate(str)` = current language; `translate(str, lang)` = explicit language.

## Load pipeline (translator::load, called from simu_main)

1. `dr_chdir(env_t::data_dir)`; scan `text/*.tab` in the program directory (in this repo that is `simutrans/`).
2. Per base file: `load_language_iso` (iso from filename minus `.tab`; iso_base = before `_`), `load_language_file` (language name line; PROP_FONT_FILE; body), `guess_highest_unicode`, `lang_count++`. Files beyond 40 are skipped with warnings.
3. Pakset overlay: `load_files_from_folder("<pakset>/text/")` — every `.tab` whose filename embeds a loaded language code: `LA.*.tab` (e.g. `en.tab`, `en_extra.tab`) or a non-alphanumeric separator before `LA.tab` (e.g. `foo_en.tab`). Bodies are merged with `set`, so a later file (pakset/addon) overrides the base on duplicate keys.
4. Addons (private paks enabled): `addons/<pakset>/text/` under the user dir, same merge.
5. `compat.tab` (pakset, then addons) → the `compatibility` table, loaded raw (no recoding).
6. Current language defaults to English when present.

## .tab parsing (load_language_file_body)

- `fgets_line` strips CR/LF; `#` lines are comments; a pair is stored only when the two lines differ.
- `recode()` (both lines): backslash + ANY character → one `\n` (the only escape); control chars below 13 are dropped; bytes ≥ 127 handled per direction below.
- Encoding detection (`is_unicode_file`): UTF-8 iff the file starts with `§` (C2 A7) or BOM followed by `§`; else Latin-1, or Latin-2 when `is_latin2_based`.
  - KEY line: recoded to Latin-1 form (UTF-8 files are decoded down) — keys are Latin-1 bytes; this is why keys must be ASCII/transliterated.
  - VALUE line: recoded to UTF-8 (Latin-1/2 files are encoded up; UTF-8 files pass through, multi-byte characters copied whole).
- Latin-2 conversion via `latin2_to_unicode`/`unicode_to_latin2` when the language's font is latin2-based.
- `is_latin2_based` is guessed from the PROP_FONT_FILE line containing "latin2"; such a language has `cyr.bdf` registered as its font.
- Format-string validation (UTF-8 languages only; exempt via `is_special_format_string`: `%X_CITY_SYLL`-style keys and `.center`/`.suburb`/`.extern`): `cbuffer_t::check_and_repair_format_strings` — mismatched `%`-sequences are repaired where possible, else the pair is DROPPED (English is shown).
- PROP_FONT_FILE: in non-UTF-8 files it is read (and skipped) after the language-name line and stored as a table entry; the value may be a `;`-separated font list.

## Fonts & display metrics (gui/sprachen.cc)

- `sprachengui_t::init_font_from_lang`: loads the language font from `font/` (`FONT_PATH_X` + the PROP_FONT_FILE value, `;` fallback list, final fallback `cyr.bdf`) when the current font lacks `highest_character`.
- `guess_highest_unicode` probes two specific keys ("Bruecke muss an\neinfachem\nHang beginnen!\n", "Start") for the language's highest codepoint — a font-selection heuristic.
- `ellipsis_width` = display width of `translate("...")`.
- Number formatting comes from translations: `SEP_THOUSAND`, `SEP_THOUSAND_EXPONENT`, `SEP_FRACTION`, `LARGE_NUMBER_STRING`/`LARGE_NUMBER_VALUE` (applied via `set_thousand_sep` etc.).
- Language dialogue: one button per language (flag skin); a language whose font files cannot be found is disabled.

## Language selection

- Startup (simmain.cc): `-lang <iso>` → else saved `env_t::language_iso` → else OS locale (`dr_get_locale`, `dr_get_locale_string`) → else the language dialogue.
- Matching uses only the first two characters (iso_base): `-lang en_GB` selects `en`.
- `set_language(const char*)`: unknown iso falls back to English if present, else language 0.
- `set_language(int)` side effects: `env_t::language_iso`, `default_settings.set_name_language_iso`, `init_custom_names(lang)` (name lists → [city & street names](city-and-street-names.md)), `ellipsis_width`.
- In-game switch (`sprachengui_t::action_triggered`): `set_language` → `init_font_from_lang` → `destroy_all_win(true)` + `SYSTEM_RELOAD_WINDOWS` event: every window is recreated so labels re-translate. Windows must translate at build/draw time, never cache translated strings across a language switch.

## Dates

- `get_month_name`/`get_short_month_name`: fixed key arrays ("January"…; the key "Oktober" — German spelling — is load-bearing).
- `get_date`/`get_short_date`/`get_year_month` build the string per `env_t::show_month` (`DATE_FMT_*`; Extended adds `DATE_FMT_INTERNAL_MINUTE` and `DATE_FMT_JAPANESE_INTERNAL_MINUTE` for minute-resolution calendars).
- `YEAR_SYMBOL`/`DAY_SYMBOL`/`MON_SYMBOL` keys; a missing symbol key yields "" (strcmp-against-the-key trick).

## compat.tab / compatibility_name

- Pakset `compat.tab`: old object name → replacement name pairs, loaded raw into the `compatibility` table.
- `translator::compatibility_name(str)` returns the replacement or the input unchanged.
- Called from the `rdwr` loading paths of nearly every object class (wege, gebaeude, bruecke, tunnel, roadsign, wayobj, baum, groundobj, vehicle/simconvoi, simfab, replace_data, …) when a stored desc name is not found — the pakset-side mechanism for keeping old savegames loadable after object renames/removals.

## Help pages (gui/help_frame.cc)

- `help_frame_t::load_text` resolves `text/<iso>/<page>.txt` → `text/<iso_base>/…` → `text/en/…`, relative to `env_t::data_dir`; legacy Latin-1 contents are recoded to UTF-8 (heuristic on umlaut bytes).
- `all-help-file-names` (simutrans/text) lists the pages; `about.txt`/`simutrans.txt` are the general-help defaults.

## Obtaining the files

- `get_lang_files.sh` (repo root, tracked): downloads the base-text language-pack zip from the Simutranslator site into `simutrans/text/` (curl/wget). simmain.cc's fatal no-language-files error message points users at it.

## Known problems & quirks

- Silent drop of format-mismatched translations — the main "still English" cause (→ [../translations](../translations.md) gotchas).
- Key/value encoding asymmetry (Latin-1 keys vs UTF-8 values): non-ASCII keys cannot round-trip.
- The backslash escape swallows any following character.
- Static return buffers in `get_date`/`get_short_date`/`get_year_month`: not reentrant; UI-thread use only.
- 40-language cap.
- `sprachengui_t`'s "> 256 glyphs" warning condition is vacuous (it tests a font-found boolean against 256).

## Open questions

- Workflow questions (portal, fragments, exports) live in [../translations](../translations.md).
