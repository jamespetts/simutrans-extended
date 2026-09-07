---
status: reviewed
verified: master @ d91fc8fce
---
# Utilities & containers

**Covers:** tpl/ (containers), utils/ (cbuffer_t, simstring, simrandom, log, sha1, float32e8_t,
dr_rdpng, searchfolder, csv, fetchopt, plainstring, for.h, checklist, dead files, vendored
utils/openttd), sys/ (simsys + backends, clipboard), sound/ + music/ (backends), unicode.cc/h
(root). Elsewhere: checklist → [network](network.md); all threading (incl. utils/simthread) →
[threading](threading.md); simsound.cc (root) → [simulation-core](simulation-core.md).

**Use these, not std.** Simutrans containers/utilities (tpl/, utils/) must be preferred over
std (etc.) equivalents: they are profiled to be faster for this game's workloads
[RECOLLECTION:2026-09-07] (canonical note: [project-notes](project-notes.md)). Headers are the
interface authority; this doc records role + gotchas only.

## Containers (tpl/) — general

- Bounds-checked access calls `dbg->fatal` → hard abort (no exceptions); entries below note
  where checks are absent or failure is silent.
- Most are non-copyable (private copy ctor/assignment); exceptions are noted per entry.
- Elements are stored by value (`new T[]` / malloc); T must generally be cheaply copy-assignable.
- Iteration: `FOR`/`FORT` macros (utils/for.h, below); on master also legacy `ITERATE`/`ITERATE_PTR`.
- Node containers pool nodes via `freelist_t` (dataobj/freelist.h): allocation is mutex-protected
  under MULTI_THREAD; container operations themselves are NOT thread-safe.

## Container entries

**vector_tpl<T>** — the workhorse dynamic array; copyable/assignable. `append` (no push_back)
grows only when full, doubling capacity (first growth to 1); `resize` grows only, no shrink API.
`[]` checked (fatal); `index_of` returns 0xFFFFFFFF when absent (no fatal). `insert_ordered`/
`insert_unique_ordered` binary-insert (vector must already be sorted; unique variant returns a
pointer to the conflicting element on failure).
`set_count`/`store_at` create default filler elements. `remove_at(pos, false)`/`swap_erase`
swap-remove (O(1), order changes). Iterators are raw pointers, invalidated by any append/resize.
`clear_ptr_vector(v)` deletes pointer elements and clears.

**minivec_tpl<T>** — ≤255 elements (uint8 count/size, GCC_PACKED), for small hot collections
(schedules, buildings, ways). Non-copyable. `append(elem, extend)` grows only when full, by
`extend` (default 1): pass a larger extend for bulk appends, else growth is O(n²). Resize >255
fatal. `remove(elem)` removes ALL occurrences.

**array_tpl<T>** — fixed-size bounds-checked 1-D array; non-copyable. Gotcha: one-arg `resize(n)`
can logically shrink; `resize(n, value)` grows only (inconsistent).

**array2d_tpl<T>** — bounds-checked 2-D grid (w×h). Copy ctor/assignment are memcpy-based →
**T must be trivially copyable**. `resize(x,y)` discards contents; `resize(x,y,def)` keeps the
overlap. `at()` checked (fatal); `to_array()` bypasses all checks. `rotate90()` exists.

**sparse_tpl<T>** — sparse 2-D array (Compressed Row Storage); used for city influence/weight
maps (simcity, minimap, city_info). Out-of-bounds get→0 and set→ignored **silently**; zero is
never stored, setting 0 removes the entry. uint16 internals: ≤65535 nonzero entries, uint16
dimensions. Non-copyable.

**slist_tpl<T>** — singly-linked list; pooled nodes (see general). insert/append/append_list O(1);
remove/is_contained/index_of/at O(n); `sort()` is bubble sort. `append_list(other)` splices O(1),
emptying other. `erase(it)` returns the successor iterator; the iterator passed to erase/insert is
invalid afterwards. Non-copyable. Header warning: do NOT use for types with non-trivial copy
constructors (e.g. std::string) — `remove_first()` returns T by value.

**list_tpl<item_t>** — despite the name: a growable array of pointers to polymorphic objects with
optional ownership (ctor flag `owns_items`; virtual `create_item`/`delete_item`/`compare_items` —
designed for subclassing). Default `compare_items` orders by pointer value; override for
meaningful sort. `is_sorted` is maintained automatically; `set_count` clears it. `extract()`
removes without deleting (even when owned); `remove()` deletes if owned; `set()` returns the old
item (NULL if owned — it was deleted). `get()`/`[]` are UNCHECKED (mutators are checked). Growth:
16, then ×1.5. Widely used (bauer builders, GUI lists, powernet, network socket lists).

**fixed_list_tpl<T,N>** — fixed-capacity ring buffer of N inline slots (designed for small N;
header says ≤32). `add_to_head`/`add_to_tail` **silently overwrite the oldest element when full**;
`add_to_head_no_overwite` (sic) / `add_to_tail_no_overwrite` refuse instead (overwrite-when-full
is by design [RECOLLECTION:2026-09-07]). `[]` checked (fatal);
`trim_from_head`/`trim_from_tail`. Used for rolling data in routing/convoys/halts.

**ordered_vector_tpl<T,inttype>** — sorted-unique vector; needs `<=` and `==`; `inttype` is the
count/capacity width for memory tuning (uint8/16/32). Set ops `set_union`/`set_diff`/`set_minus`
(set_union takes its argument **by value** — copies). `[]` UNCHECKED; `index_of` asserts when
absent. Typedefs: catg_index_vec, halthandle_vec, linehandle_vec, convoihandle_vec.

**weighted_vector_tpl<T>** — vector with cumulative prefix weights for weighted random selection:
`at_weight(simrand(get_sum_weight()))` — see `pick_any_weighted` in simrandom. `append(elem,
weight)`; **IGNORE_ZERO_WEIGHT is always defined (simtypes.h): zero-weight append/insert_at
silently drops the element** — probably intentional: weight-0 entries are never eligible for
selection [RECOLLECTION:2026-09-07]. `update`/`update_at`/`update_weights` adjust weights. `index_of`
FATALS when absent (unlike vector_tpl). Non-copyable.

**binary_heap_tpl<T>** — min-heap for pointer elements: ordering uses `*a <= *b`, so T must be a
pointer to a type defining `operator<=` (routing: route.cc, wegbauer, simhalt, pathfinding API).
nodes[0] unused; `front()` = minimum. Initial capacity 4096 (MALLOCN), doubles. `clear()` does NOT
delete the elements; `delete_all_node_objects()` does. Non-copyable.

**hashtable_tpl<key,value,hash_t,n_bags>** — base hashtable; the hash policy (`hash(key)`,
`comp(a,b)` → <0/0/>0) comes from the variant headers (the header comment references a nonexistent
ifc/hash_tpl.h). **n_bags is compile-time fixed, no rehashing, must be ≤255** (uint8 bag counter).
Bags are slist_tpl kept sorted by comp with early-exit lookup → **never mutate a key after
insertion** (values are fine). `get()` returns a reference to a shared **static default value**
when the key is absent — never modify a `get()` result; use `access()` (pointer or NULL) for
writes. `put` refuses duplicates (returns false); `set` inserts-or-replaces, returning the old
value; `remove` returns the removed value or a default. Non-copyable.

Variants (all non-copyable):
- **inthashtable_tpl** — integer/enum keys (hash = value truncated; bags ordered by key value).
- **ptrhashtable_tpl** — pointer keys (hash = pointer bits; bags ordered by address).
- **stringhashtable_tpl<value,n_bags>** — `const char*` keys: **not copied** — the caller must
  keep the string alive; hash sums only the first ≤16 chars; comp = strcmp.
- **plainstringhashtable_tpl** — same, but keys are plainstring (copied/owned).
- **koordhashtable_tpl** — koord keys (hash packs y<<16|x; ordered by y then x).
- **quickstone_hashtable_tpl** — quickstone handle keys, ordered by id.
- **koord_pair_hashtable_tpl** — **do not use**: `comp()` returns bool, violating the diff
  contract (early-exit produces wrong "absent" results), and the companion iterator class does not
  compile (nonexistent base, syntax error). Zero users → [known-bugs](known-bugs.md).

**quickstone_tpl<T>** — tombstone handle mechanism behind `convoihandle_t`/`halthandle_t`/
`linehandle_t` (root typedefs). Static per-type-T table of T* keyed by uint16 id (≤65535
simultaneous objects per type; id 0 = null). `is_bound()` detects dangling for ALL handle copies;
`init()` invalidates all existing handles; `detach()` unbinds all handles to one object — required
before/at ~T() if raw pointers were handed out via `get_rep()`. `rdwr`/`set_id` are savegame and
network-sync surface → [savegame-versioning](savegame-versioning.md),
[sync-and-determinism](sync-and-determinism.md). Gotcha: the `(T*, bool)` ctor's scan loop
increments instead of decrements (broken; unused — do not use → [known-bugs](known-bugs.md)).

**piecewise_linear_tpl<key,value,internal_t>** — piecewise-linear function table (neroden) for
pak-configured economic functions: build once with `insert(key,value)` (duplicate keys replace),
evaluate with `compute_linear_interpolation(key)` or functor `operator()`; constant extrapolation
outside the key range; `internal_t` widens intermediate arithmetic. Copyable (vector_tpl-based).
Standalone test tpl/test_piecewise_linear_tpl.cc is in no build file (pulls in utils/dumb-log.cc).

**freelist_tpl<T>, freelist_iter_tpl<T>** — unused pool experiments; the iter variant cannot
compile (invalid `if (sync_result result = ...)` syntax, missing include) and both guard their
mutexes with a never-defined `MULTI_THREADx`. Real pooling is `freelist_t` (dataobj/freelist.h;
see general) → [known-bugs](known-bugs.md).

## Iteration idioms

- utils/for.h: `FOR(type, elem, container)` / `FORX(type, elem, container, step)`; `FORT`/`FORTX`
  when type is a dependent (template) name. Element binding chosen by decoration: `x` (copy),
  `const x`, `& x` (reference), `const& x`. The standard idiom codebase-wide; prefer FOR/range-for
  for new code on BOTH branches [RECOLLECTION:2026-09-07].
- master only: `ITERATE(collection,i)` / `ITERATE_PTR` index-loop macros, defined redundantly in
  vector_tpl.h, weighted_vector_tpl.h, ordered_vector_tpl.h and fixed_list_tpl.h
  [CODE master @ d91fc8fce]. ex-15 removed them entirely; loops there use range-for/FOR
  [CODE ex-15 @ fff9c203c].

## utils/

**cbuffer_t** — auto-growing char buffer; the standard GUI string builder (prefer over
std::string/stringstream). `printf`/`vprintf`; `append` overloads (C-string, length-limited, long,
double+precision); `append_money`; `trim`; `extend`; `len()` excludes the NUL; implicit
`const char*` conversion; copyable. `check_and_repair_format_strings` = translation format QA.
ex-15 adds `append_u`, `append_fixed(uint8/16/32)`, `append_bool` plus static `decode_uint8/16/32`,
`decode_bool` — a fixed-width decimal text codec used by the pak readers and consist/replace data
[CODE ex-15 @ fff9c203c].

**simstring.h** — locale-aware number/money formatting (`money_to_string`, `number_to_string(_fit)`;
separators/exponent/large-amount abbreviation set via `set_thousand_sep` etc.), `tstrncpy`,
`rtrim`/`ltrim`/`trim(std::string)`, `strstart`, `strempty`, `STRICMP`/`STRNICMP` macros. Gotcha:
the money/number formatters write into caller buffers with **no size checks**. ex-15 adds
`enum future_state` [CODE ex-15 @ fff9c203c].

**plainstring.h** — minimal owning C-string wrapper (copies on construct/assign; implicit
conversion to `const char*`; `==` is strcmp). A `free()` overload is deliberately deleted to
prevent misuse. Key type of plainstringhashtable_tpl.

**simrandom.h** — RNG; full rules in [sync-and-determinism](sync-and-determinism.md).
`simrand(max[,caller])` is the sync-critical RNG — its seed and sampled values are part of the
network checklist (`random_seed` + `rand[32]`, utils/checklist.h → [network](network.md));
`sim_async_rand` is non-sync (UI etc.); `simrand_normal`, `simrand_plain`; `simrand_rdwr`
(savegame surface); perlin noise (`perlin_noise_2D`, `init_perlin_map`/`exit_perlin_map`);
`pick_any`/`pick_any_weighted` (weighted containers); integer `log10`/`log2`/`sqrt_i32`/`sqrt_i64`.
Random-mode bitmasks (INTERACTIVE_/STEP_/SYNC_STEP_/LOAD_/MAP_CREATE_/MODAL_RANDOM) tag where
simrand is called, for desync debugging. ex-15 adds `sigmoid` [CODE ex-15 @ fff9c203c].

**float32e8_t** — deterministic decimal type: 32-bit mantissa + 10-bit exponent + sign; all
arithmetic is integer-only → bit-identical results on all platforms (the network-sync-safe
decimal; used in physics). Full arithmetic/comparison operators; constants (`zero`…`ten_thousand`,
`half`, `third`, …); `abs`/`sgn`/`log2`/`exp2`/`pow`/`sqrt`; `fl_min`/`fl_max`; `rdwr` (savegame
surface); `get_mantissa()` for checksums; `to_double`/`to_sint32` (explicit cast). Much slower
than float/int — plain integers remain preferred ([project-notes](project-notes.md)). Double
interop is behind `USE_DOUBLE`, defined nowhere in-tree → disabled.

**log.h** — `log_t` (debug/message/warning/error/`fatal`[NORETURN]/vmessage; log file + stderr
tee; optional syslog; duplicate-object message tracking `doubled`). Global instance `dbg`
(simdebug.h); container bounds checks call `dbg->fatal` → hard abort.

**sha1** — vendored FIPS 180-1 SHA-1 (network auth/checksums: network/checksum, pwd_hash,
nettool). `Result()` needs a ≥20-byte caller buffer, no bounds check; single-message object; not
thread-safe.

**dr_rdpng** — libpng PNG I/O, **makeobj-only** (`load_block` → caller-owned REALLOC'd ARGB
block; alpha inverted after load: 0 = opaque; fatal unless dimensions are multiples of the tile
size; case-insensitive filename retry on POSIX). The free `write_png` function has no callers.

**searchfolder** — `searchfolder_t::search(path, ext, …)`: enumerates directory entries matching
an extension (case-insensitive), non-recursive. Owns its entries: frees them at destruction and
at the next `search()` — never free or retain entries yourself. Windows: paths longer than
MAX_PATH fail silently (empty result).

**csv** — `CSV_t` encoder/parser on cbuffer_t (server-list CSV). Not thread-safe (per header).
`get_next_field`/`decode` use dense negative return codes (−1 end-of-line, −2 end-of-data,
−9 corrupt, −4/−5 misuse).

**fetchopt** — getopt()-style single-character CLI option parser; **nettool-only** (not built
into the game). All options must precede positional arguments; no `--`, no long options.

**checklist.h/cc** — network sync checklist (includes the RNG state) → documented in
[network](network.md).

Dead / not built (do not use; slated for deletion → [known-bugs](known-bugs.md)):
- `notification.h` — enum only, zero includes; player_t defines its own duplicate.
- `snprintf.h` — PHP-derived declarations with no implementation; would not compile; never include.
- `dbg_weightmap.*` — debug city weight-map dumper behind a never-defined `DEBUG_WEIGHTMAPS`; the
  .cc is in no build file.
- `dumb-log.cc` — stderr-only `log_t` implementation for unit tests (pulled in by
  tpl/test_piecewise_linear_tpl.cc); never link alongside utils/log.cc (duplicate symbols); built
  nowhere.
- `omzet2.c` — legacy standalone font.pbm→font.dat converter; built nowhere.

**utils/openttd/** — legacy vendored OpenTTD dependency snapshot (ICU, freetype, zlib/png,
lzma/lzo, pthread-win32 headers + prebuilt .lib files). No game source includes anything from it;
the ICU parts and all .lib files are unreferenced by sources and build files; the only live build
role is the Makeobj Debug|Win32 include path for `<png.h>`. The game's UTF-8 is root
unicode.cc/h, not ICU → [build-and-toolchain](build-and-toolchain.md).

**utils/simthread** — all threading → [threading](threading.md).

## sys/ platform layer

**simsys.h/cc** — the `dr_*` OS abstraction, the game's only OS interface. Groups: video/window
(`dr_os_*`, `dr_textur_*`, `dr_prepare_flush`/`dr_flush`, scale/fullscreen queries), input
(`sys_event_t` + extern `sys_event`, `GetEvents`, pointer/IME helpers), UTF-8 filesystem
(`dr_mkdir`, `dr_remove`, `dr_rename`, `dr_chdir`, `dr_fopen`, `dr_gzopen`, `dr_stat`,
`dr_query_homedir`, `dr_query_fontpath`), clipboard (`dr_copy`/`dr_paste`), time (`dr_time`,
`dr_sleep`), locale, `sysmain`. Portable implementations live in simsys.cc. Gotchas:
`dr_movetotrash` returns **false on success** (inverted); `sys_event.mx/my` may be negative;
exactly one video backend and one clipboard backend per binary (else the `dr_*` symbols collide).

Backends: `simsys_s2.cc` SDL2 · `simsys_s3.cc` SDL3 (`USE_SDL3`; never compiled with s2) ·
`simsys_w.cc` Win32 GDI · `simsys_posix.cc` headless stub (no-ops; provides main()). Clipboard:
`clipboard_s2`/`clipboard_s3` (SDL) · `clipboard_w32` (Win32 API, UTF-16 conversion) ·
`clipboard_internal` (static 4096-byte buffer — longer content truncated). Backend selection →
[build-and-toolchain](build-and-toolchain.md); rendering details → [rendering](rendering.md).

## sound/ & music/

**sound.h** — three-function API: `dr_init_sound`, `dr_load_sample(file)` (returns handle or −1),
`dr_play_sample(key, volume)`; exactly one backend per binary; the game core passes WAV paths and
the backend parses/loads them. Backends: sdl2_sound (own software mixing) · sdl2_mixer_sound ·
sdl3_sound · win32_sound (winmm) · win32_sound_xa (XAudio2) · core-audio_sound.mm /
AVF_core-audio_sound.mm (macOS) · no_sound. Gotcha: per-backend sample-count caps differ; loading
beyond the cap fails.

**music.h** — MIDI API (`MAX_MIDI` = 128): `dr_init_midi` (false = leave MIDI alone),
`dr_set_midi_volume(0..255)`, `dr_load_midi`/`dr_play_midi`/`dr_stop_midi`, `dr_midi_pos()`
(−1 = finished), `dr_destroy_midi`. `dr_load_sf` (soundfont) exists **only under
`USE_FLUIDSYNTH_MIDI`** — guard calls with the ifdef. Backends: fluidsynth · sdl2_mixer_midi ·
w32_midi (MCI) · core-audio/AVF (macOS; AVF currently disabled in CMake — no_midi used instead) ·
no_midi. High-level caller: simsound.cc (root → [simulation-core](simulation-core.md)).

## Unicode (root unicode.h/cc)

Hand-rolled UTF-8 support; no ICU (see utils/openttd above): `utf8_decoder_t` (decode/iterate),
`utf8_get_next_char`/`utf8_get_prev_char`, `utf16_to_utf8`, `unicode_to_latin2`/
`latin2_to_unicode`, `utf8caseutf8` (case-insensitive UTF-8 strstr). Gotchas: `decode()` does NOT
stop at NUL — callers must check `UNICODE_NUL` themselves (else buffer overrun); invalid sequences
fall back to ISO-8859-1 consuming 1 byte (never fails); `utf16_to_utf8` is BMP-only (never emits
4-byte sequences), writes ≤3 bytes **without a NUL terminator** — output buffer ≥3 bytes;
`unicode_to_latin2` returns 0 on error (lookup covers only 0xA0–0xFF); case folding is
locale-dependent (towlower).

## Open questions

None.
