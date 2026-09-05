---
status: stub
verified: none
---
# Utilities, containers & threading

**Covers:** tpl/, utils/ (cbuffer_t, simstring, simrandom, simthread, log, sha1, float32e8_t, dr_rdpng, searchfolder, csv, fetchopt, dumb-log; vendored utils/openttd), sys/ (simsys + backends, clipboard), sound/, music/ (assets), unicode.cc/h (root) + ICU dependency. checklist → [network](network.md).

## Initial facts

- Widely-included headers: containers `tpl/vector_tpl.h`, `tpl/stringhashtable_tpl.h`; utils `utils/cbuffer_t.h`, `utils/simstring.h`, `utils/simrandom.h`; platform `sys/simsys.h` [CODE].
- ICU: the repo vendors ICU/OpenTTD headers under `utils/openttd/` (tracked), but no game source includes them; the game's own `unicode.h`/`unicode.cc` (widely included via relative paths) handle UTF-8 → [build-and-toolchain](build-and-toolchain.md) [CODE].
- SDL3 backend files exist on master only → [rendering](rendering.md) [CODE].

## Planned sections

- Container idioms (vector_tpl, hashtables, templates patterns; for.h).
- Threading model: simthread; what runs off the main thread (path exploration → [routing-and-scheduling](routing-and-scheduling.md); others to verify); lock usage rules; sync implications (→ [network](network.md)).
- float32e8_t: custom fixed-point float type; role (determinism across network? verify — do not assume) [UNVERIFIED].
- simrandom: RNG and its sync-criticality (verify) [UNVERIFIED].
- Platform layer: sys/simsys backends inventory (per branch), clipboard, filesystem.
- Sound/music: simsound (root → [simulation-core](simulation-core.md)), sound/ backends, music/ assets.
- Unicode/ICU integration (unicode.cc/h wrapper).
- Logging & debug utilities (log.cc, dumb-log.cc local artefact).

## Open questions

- Is simrandom state part of the checklist/sync surface?
- Vendored utils/openttd: what is actually used from it?
