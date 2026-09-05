---
status: stub
verified: none
---
# Repository map

**Covers:** top-level layout; tracked vs. untracked/ignored; asset dirs; dead-code candidates.

## Seed facts

- Tracked top-level dirs (ex-15): source — `bauer/ boden/ dataobj/ descriptor/ display/ finder/ gui/ ifc/ io/ makeobj/ nettools/ network/ obj/ player/ script/ squirrel/ (vendored) sys/ tests/ tpl/ utils/ vehicle/ world/`; assets/runtime — `music/ simutrans/ themes.src/` (`text/` is largely gitignored); build/packaging — `cmake/ configs/ nsis/ OSX/ .github/`; legacy human docs — `documentation/` [CODE ex-15 @ b06e8fa14].
- The root holds ~280 entries, mostly untracked/ignored: `*.obj`, `*.diagsession`, `*.psess/*.vsp`, `Debug*/ Release*/ x64/ intermediates/`, `.vs/`, `*.VC.db`, `base_texts*.dat` (ignored translation fragments), `wishlist.txt` (ignored) [CODE ex-15 @ b06e8fa14 + .gitignore].
- Tracked root oddities: `old_blockmanager.cc/h` (dead-code candidate), `clipboard_s2.cc`, `todo.txt` (legacy wishlist, mostly Standard-era) [CODE ex-15 @ b06e8fa14]. `simline-corrupt.cc` exists locally but is gitignored [CODE].
- Root also holds many local-only historical artefacts (desync logs, performance reports, `.diff`/`.patch` files) — potentially valuable as leads, but NOT tracked; cite as [local-only] [CODE].

## Planned sections

- Full directory table: one line each + link to the domain doc.
- Tracked vs. ignored vs. dead inventory (verify `old_blockmanager` is unreferenced before calling it dead).
- Asset locations: `simutrans/` (runtime text/themes), `themes.src/`, `music/`, `text/` status.
- Branch-specific files (e.g. SDL3 backend on master only → [build-and-toolchain](build-and-toolchain.md)).

## Open questions

- Is `old_blockmanager.*` referenced anywhere in the build?
- Which root `.txt`/`.diff` artefacts are worth summarising into docs as historical leads?
