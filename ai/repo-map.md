---
status: stub
verified: none
---
# Repository map

**Covers:** top-level layout; tracked vs. untracked/ignored; asset dirs; dead-code candidates.

## Initial facts

- Tracked top-level dirs (ex-15): source — `bauer/ boden/ dataobj/ descriptor/ display/ finder/ gui/ ifc/ io/ makeobj/ nettools/ network/ obj/ player/ script/ squirrel/ (vendored) sys/ tests/ tpl/ utils/ vehicle/ world/`; assets/runtime — `music/ simutrans/ themes.src/` (the tracked base translation texts + help pages live under `simutrans/text/` → [translations](translations.md); the root `text/` directory is untracked); build/packaging — `cmake/ configs/ nsis/ OSX/ .github/`; legacy human docs — `documentation/` [CODE].
- The root contains many untracked/ignored entries: `*.obj`, `*.diagsession`, `*.psess/*.vsp`, `Debug*/ Release*/ x64/ intermediates/`, `.vs/`, `*.VC.db`, `base_texts*.dat` (ignored Simutranslator authoring fragments → [translations](translations.md)), `wishlist.txt` (ignored) [CODE + .gitignore].
- Tracked root oddities: `old_blockmanager.cc/h` (dead-code candidate), `clipboard_s2.cc`, `todo.txt` (legacy wishlist, mostly Standard-era) [CODE]. `simline-corrupt.cc` exists locally but is gitignored [CODE].
- The root also contains many local-only historical artefacts (desync logs, performance reports, `.diff`/`.patch` files) — potentially valuable as indications, but NOT tracked; cite as [local-only] [CODE].

## Planned sections

- Full directory table: one line each + link to the domain doc.
- Tracked vs. ignored vs. dead inventory (verify `old_blockmanager` is unreferenced before calling it dead).
- Asset locations: `simutrans/` (runtime text/themes), `themes.src/`, `music/`, `text/` status.
- Branch-specific files (e.g. the ex-15-only consist-order files → [ex-15](ex-15.md)).

## Open questions

- Is `old_blockmanager.*` referenced anywhere in the build?
- Which root `.txt`/`.diff` artefacts are worth summarising into docs as historical indications?
