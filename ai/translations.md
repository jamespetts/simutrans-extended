---
status: reviewed
verified: master @ 84b8345a4
---
# Translations & texts

**Covers:** dataobj/translator.*, simutrans/text/ (base translation files + help pages), per-pakset text/ overlays and compat.tab, root base_texts*.dat fragments (untracked), gui/sprachen.cc, gui/help_frame.cc, get_lang_files.sh, descriptor/writer/text_writer.* + descriptor/reader/text_reader.* + descriptor/text_desc.h (→ subdoc).

Basic doc for the common task: adding, changing or debugging user-facing text. Full architecture of the text system itself (needed only to debug or change the system): [translator internals](translations/translator-internals.md). Occasionally needed subtopics: [city & street names](translations/city-and-street-names.md), [pak-embedded texts](translations/pak-embedded-texts.md).

## The three text systems

1. **Translator .tab texts** — nearly all UI text. Per-language key→string hashtables (class `translator`, dataobj/translator.h) loaded from `.tab` files. This is what "translation texts" normally means.
2. **Pak-embedded Text nodes** — strings compiled into `.pak` files by makeobj (object internal names, copyright, accommodation-class and livery-scheme names). → [pak-embedded texts](translations/pak-embedded-texts.md)
3. **In-game help pages** — `simutrans/text/<lang>/<page>.txt` files rendered by `help_frame_t` (gui/help_frame.cc); plain files, no keys.

## Where the .tab text files live

- **Base (program) texts: `simutrans/text/` in THIS repository** — one `<iso>.tab` per language, committed and edited like source code (English directly in `en.tab`; other languages sometimes fixed directly, normally regenerated from the Simutranslator portal — workflow under open questions).
- **Pakset texts: `<pakset>/text/*.tab`** in the pakset distribution — a per-pakset overlay (object display names, pakset-specific strings), maintained with the pakset sources in separate pakset repositories, not here. Loaded after the base texts; a same-key entry overrides the base text.
- **Addons: `addons/<pakset>/text/`** under the user directory, loaded last (only with private paks enabled).
- **NOT the root `text/` directory** — that is untracked clutter (→ [repo-map](repo-map.md)). The tracked base texts are at `simutrans/text/`.
- **Root `base_texts*.dat` / `base-texts-*.dat`** — untracked authoring fragments in Simutranslator import format (`obj=program_text`, `name=`/`note=` fields) [local-only]. Traditionally imported manually into the Simutranslator portal; never consumed by the game or the build [RECOLLECTION:2026-09-08].

## .tab format essentials

- Line 1: the language's display name. Legacy non-UTF-8 files then contain `PROP_FONT_FILE` and a font-file line.
- Then key/value pairs: a key line followed by a value line. `#`-prefixed lines are comments (the Simutranslator banners are comments).
- A backslash followed by ANY character becomes a single newline in the loaded string — the only escape; literal backslashes cannot be written.
- A file is UTF-8 iff it starts with `§` (bytes C2 A7) or a BOM followed by `§`; otherwise it is read as Latin-1 (Latin-2 when the font line says so) and values are recoded to UTF-8 at load.
- Pairs whose two lines are identical are skipped at load.
- Loader: `translator::load` / `load_language_file` / `load_language_file_body` (dataobj/translator.cc).

## Adding or changing a user-facing text

1. In code call `translator::translate("The English text ...")` — the key IS the literal English string.
2. With no table entry, `translate()` returns the key unchanged: English works with no `.tab` entry whenever the key is the desired English text. Add an `en.tab` entry only when the display text must differ from the key (multi-line, escapes, disambiguation) or to feed other languages.
3. Other languages: the key→translation pair goes into that language's `<iso>.tab` (normally via the Simutranslator export; direct in-repo edits are used for quick fixes/proofreading).
4. The C++ key must match the `.tab` key byte-exactly — case, punctuation and trailing `\n` sequences included.
5. Historic authoring pattern for new texts: a root `base_texts*.dat` fragment in Simutranslator format (header comments, `obj=program_text`, `name=<key>`, `note=<explanation for translators>`), traditionally imported manually into the Simutranslator portal [local-only; RECOLLECTION:2026-09-08].

## Gotchas

- A missing translation in a language shows the English KEY to the user (there is no fallback language).
- printf-style mismatch: for UTF-8 languages the loader validates format strings; a translation whose `%s`/`%d` etc. do not match the key's is auto-repaired where possible, otherwise **silently dropped** (user sees English). "Text X is still English in language Y" reports are usually this. Exempt: town-name syllable keys and `.center`/`.suburb`/`.extern`.
- Town-name generator keys (`%X_CITY_SYLL`, `&X_CITY_SYLL`, `&X_CITY_PREFIX`, `&X_CITY_SUFFIX`) look like debris but are data consumed by the name generator — do not clean them up. → [city & street names](translations/city-and-street-names.md)
- Keys must be plain ASCII: internally keys are Latin-1 bytes while values are UTF-8 — hence transliterated keys like "Bruecke".
- Special keys that are settings, not prose: `PROP_FONT_FILE` (font per language), `SEP_THOUSAND`, `SEP_THOUSAND_EXPONENT`, `SEP_FRACTION`, `LARGE_NUMBER_STRING`/`LARGE_NUMBER_VALUE` (number display), `YEAR_SYMBOL`, `DAY_SYMBOL`, `MON_SYMBOL` (dates).
- Savegames referencing removed/renamed pakset objects are repaired through the pakset's `compat.tab` (`translator::compatibility_name`). → [translator internals](translations/translator-internals.md)
- Hard cap of 40 languages (`langs[40]`, dataobj/translator.cc); extra `.tab` files are skipped with a warning.
- `simutrans/text/` content differs between branches like any source file; check the checked-out branch's state per task.

## Open questions

- Is the Simutranslator portal still live/maintained for Extended's texts (base and paksets), and who produces and commits the non-English `.tab` exports? Pakset `.tab` files are all-language portal exports (untranslated languages become stub files) [RECOLLECTION:2026-09-08].
- `simutrans/text/en.txt` — a Simutranslator "Scenario: Base texts" export (smaller than `en.tab`): purpose, and still maintained?
- `get_lang_files.sh` (repo root, tracked) downloads the base-text language pack from the Simutranslator site into `simutrans/text/` — still working/used?
