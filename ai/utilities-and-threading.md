---
status: stub
verified: none
---
# Utilities, containers & threading

**Covers:** tpl/ (24 files), utils/ (cbuffer_t, simstring, simrandom, simthread, log, sha1, float32e8_t, dr_rdpng, searchfolder, csv, fetchopt, dumb-log; vendored utils/openttd), sys/ (simsys + backends, clipboard), sound/, music/ (assets), unicode.cc/h (root) + ICU dependency. checklist → [network](network.md).

## Seed facts

- Container hubs: `tpl/vector_tpl.h` (46 includers), `tpl/stringhashtable_tpl.h` (30) [CODE ex-15 @ b06e8fa14].
- Utils hubs: `utils/cbuffer_t.h` (95 includers), `utils/simstring.h` (71), `utils/simrandom.h` (31) [CODE ex-15 @ b06e8fa14].
- ICU: `unicode/utypes.h` is included by 148 files, `unicode/uobject.h` 41, `unicode/unistr.h` 38 — but ICU is not a tracked top-level dir; where the headers come from per platform is unknown → [build-and-toolchain](build-and-toolchain.md) open question [CODE ex-15 @ b06e8fa14].
- `sys/simsys.h` has 41 includers — the platform abstraction hub [CODE ex-15 @ b06e8fa14].
- SDL3 backend files exist on master only → [rendering](rendering.md) [CODE].

## Planned sections

- Container idioms (vector_tpl, hashtables, templates patterns; for.h).
- Threading model: simthread; what runs off the main thread (path exploration → [routing-and-scheduling](routing-and-scheduling.md); others to verify); lock discipline; sync implications (→ [network](network.md)).
- float32e8_t: custom fixed-point float type; role (determinism across network? verify — do not assume) [UNVERIFIED].
- simrandom: RNG and its sync-criticality (verify) [UNVERIFIED].
- Platform layer: sys/simsys backends inventory (per branch), clipboard, filesystem.
- Sound/music: simsound (root → [simulation-core](simulation-core.md)), sound/ backends, music/ assets.
- Unicode/ICU integration (unicode.cc/h wrapper).
- Logging & debug utilities (log.cc, dumb-log.cc local artefact).

## Open questions

- Is simrandom state part of the checklist/sync surface?
- Vendored utils/openttd: what is actually used from it?
