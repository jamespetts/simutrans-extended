---
status: draft
verified: master @ 0e5d0be48
---

# First-join desync then clean rejoin

**Covers:** network join path (`network/network_cmd_ingame.cc` `nwc_sync_t`/`nwc_ready_t`, `network/network_file_transfer.cc`), join-time checklist comparison (`simworld.cc` `do_network_world_command`), convoy sync-step checklist feed (`simconvoi.cc`). Read when triaging immediate post-join loss of synchronisation where the first join fails and the immediate rejoin holds. Parent registry: [known-bugs](known-bugs.md) (P1 desync family, incl. forum topic 22203).

## Symptom

A client joining a long-running server (observed on Bridgewater-Brunel; reporter notes it is not server-specific) loses synchronisation within seconds of loading. Rejoining immediately in the same process then stays in sync indefinitely [RECOLLECTION:2026-09-30; FORUM:https://forum.simutrans.com/index.php/topic,22203.0.html]. Fresh command-line joins (`-load net:<host>`, no prior local map) succeed first time; in-game joins after a local map (usually `demo.sve`) fail first time [EXECUTION-VERIFIED:2026-09-30, Profile TEST binary direct join held sync; FORUM topic 22203 reports the demo.sve dependence].

## Isolated evidence (2026-09-30 client log)

Unpause at `sync_steps=1058`; checks at 1059 and 1060 match exactly in every group; at 1061 only convoy speed sums differ [EXECUTION-VERIFIED:2026-09-30 via `simu.log` checklist pairs]:

- `sums[0]` 332881 vs 332653 (diff 228), `sums[1]` 3191543156 vs 3189824720 (diff 1718436), `sums[2]` 224937 vs 224935 (diff 2); `sums[3]` identical.
- `1718436 / 228 = 7537` exactly, identifying a single convoy (handle id 7537) per the feed rule (`simconvoi.cc:1400-1409`: `sums[0]` mantissa sum, `sums[1]` mantissa weighted by id, `sums[2]` actual speed, `sums[3]` target speed) [CODE master @ 0e5d0be48].
- Identical at 1061: `ss`/`st`/`nfc`, `random_seed`, halt/line/convoy counters, all `rands` groups, `sums[3..9]` (threads `sums[4]=6` both, passenger generation, transferring cargoes, road random choices), private-car route hash progression.

The load state is therefore identical at unpause; one convoy's sync-step movement execution diverges ~3 frames later with target speed identical and step-phase RNG untouched.

## Ruled out for this sequence

- Thread-count asymmetry in halt `transferring_cargoes` sizing (`simhalt.cc:412` before `parallel_operations` adoption at `simworld.cc:9983`): with threads 6/6 the allocation is 7 slots in the fresh, post-demo and rejoin cases alike (stored value `-1` in the first two, adopted 5 in the third) [CODE master @ 0e5d0be48]. Real defect only for server-threads-above-client-threads; tracked as the known separate problem.
- MSVC-client vs GCC-server toolchain divergence: the failing and succeeding joins use the identical client/server pair, so a systematic toolchain difference would recur on rejoin [CODE audit 2026-09-30: no live-sync FP/libm, no `qsort`, no `long double` in synced paths; save stream is explicit little-endian primitives].
- Electricity (senke/pumpe ordering): reports of occurrence in the pre-electricity era count against it as the cause here; retained as an independent defect, not pursued in this sequence.
- Quickstone checklist counters: `init_tiles()` re-inits tables (`simworld.cc:760-763`; `tpl/quickstone_tpl.h:116-127`) and load-time handle `rdwr` advances `next` from file order (`tpl/quickstone_tpl.h:258-267`) — file-determined, not prior-world determined [CODE master @ 0e5d0be48].
- Rail reservations and convoy signal memory: rail reservations serialize by handle id (`boden/wege/schiene.cc:277-298`); convoy signal memory serializes (`simconvoi.cc:5137-5169`; the `has_reserved` byte there is vestigial, always 0 both directions) — same file, same values both peers [CODE master @ 0e5d0be48].

## Remaining working mechanism

Contamination from the previously loaded single-player world through state that is mutated by demo presence, not serialized, not checklist-sampled at rest, and not reset by `destroy()`/`load()` — while the failed network load itself neutralizes it (rejoin clean) and a fresh process has nothing to neutralize (command-line join clean). State derived purely from the transferred bytes cannot behave this way; candidates are append-only, one-time-initialized, or conditionally-initialized state. Not yet isolated to a single variable [UNVERIFIED].

Structural note, verified: the join-time `nwc_ready_t` comparison is vacuous — `clear_all_checklists()` on network load (`simworld.cc:9612-9616`) leaves the exchanged checklists default-constructed (`utils/checklist.cc:15-31`; sent `network/network_cmd_ingame.cc:737,802`; compared `:500`), so the first effective comparison is the next periodic `nwc_check_t` [CODE master @ 0e5d0be48].

## Diagnostics present (uncommitted)

Temporary TEST-only mismatch logging (reverts before any fix): `utils/checklist.h` read-only `get_debug_sum()` plus a read-only block in `karte_t::do_network_world_command` (`simworld.cc:11304`) that triangulates the suspected convoy id from the `sums[1]/sums[0]` ratio and logs id, name, position, state, actual/target speed, next stop, line, owner, vehicle count. Draws no RNG, writes no game state. Next step: run the TEST client through the failing flow (start, demo, in-game dialogue join) and read the `(TEST)` line.

## Open questions

- Which unsampled variable read by `convoi_t::sync_step` movement differs for the affected convoy between the server's in-process reload and the client's fresh load?
- Does the same convoy id recur across failures (state-dependent) or vary (timing-dependent)?
- Is the demo.sve dependence fully explained by the contamination mechanism, or is a second factor involved?
