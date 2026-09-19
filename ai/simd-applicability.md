---
status: draft
verified: master @ 075d540f3
---
# SIMD (SSE/AVX) applicability

**Covers:** SIMD/intrinsics applicability across the simulation hot paths (graphics excluded):
baseline-ISA and determinism constraints, per-hotspot verdicts against the measured inventory in
[performance](performance.md), and the staged SIMD position for the path explorer / ex-15 Y/H
traversal work.

## Why this doc

[performance](performance.md) records what is hot and how it is measured; this doc records where
intrinsics could pay off, so that SIMD is not speculatively added to paths whose cost is memory
latency or pointer chase, and so that the one kernel where SIMD is structurally feasible (the
path explorer relaxation) is designed for it from the start.

## Current state and constraints

No SIMD intrinsics exist anywhere in simulation code today; the only assembler/SIMD-era code is
graphics (`USE_ASSEMBLER`, display/simgraph16.cc, GNUC i686 only) [CODE master @ 075d540f3].

- **Baseline ISA:** builds target Win32 and x64 (MSVC) plus GNU make/CMake; `-march=native` is
  opt-in (`TUNE_NATIVE`, Makefile). SSE2 is a safe floor on x64 only (x86-64 ABI guarantee);
  Win32 needs compile-flag gating; AVX2+ needs runtime dispatch that does not exist. MMX is
  obsolete — never prefer it to SSE2.
- **Determinism:** only integer SIMD is bit-exact across platforms; FP SIMD in synced code
  violates the no-float rule ([project-notes](project-notes.md)). All candidates below are
  integer-shaped.
- **Hot-path shape:** the measured costs ([performance](performance.md) inventory) are dominated
  by memory latency, pointer chase and sequential dependence, which SIMD does not address. The
  pooling/SoA directions in [data-layout-and-design-style](data-layout-and-design-style.md)
  target the same hotspots with better expected payoff.

## Per-hotspot verdicts

| Hotspot | Verdict |
|---|---|
| `karte_t::sync_list_t::sync_step` | Not amenable — pointer walk + virtual dispatch; fix is pooling/partitioning |
| Convoy physics + `float32e8_t` operators | Not amenable as code stands — scalar dependent chains; `operator*` is a 64-bit multiply + branchy normalisation (`pmuludq`-shaped), `operator/` has no SIMD 64-bit-divide equivalent. Only pays off inside the cross-convoy SoA restructuring (data-layout doc) |
| `convoi_t::unreserve_route_range` | Not amenable as-is — pointer chase over scattered `weg_t*`. SIMD-adjacent: a contiguous mirror array (waytype + reserved-convoi id) beside `weg_t::get_alle_wege()` removes the cache misses (the real win) and the scan then vectorises; a sparse reserved-ways registry likely beats both |
| Path explorer `phase_explore_paths` | The only genuine dense-arithmetic kernel — staged position below |
| `grund_t::get_weg`, `karte_t::lookup`, private cars, pedestrians | Memory-latency/pointer-chase bound — not amenable |
| Passenger generation, `karte_t::find_destination`, `simrand` | RNG call order is sync/output-defining — untouchable |
| `route_t` A* | Sequential heap-driven expansion — not amenable |
| Mapgen `stadt_t::bewerte_loc` | Branchy per-entry predicate switch over memoised flag bytes; low ceiling; the algorithmic targets in the mapgen section of [performance](performance.md) dominate |
| Savegame load | zstd decompression (debug-lib caveat in [performance](performance.md)) — the lever is a release-built zstd (already SIMD-optimised upstream), not game-code intrinsics |

## Path explorer — staged SIMD position

[RECOLLECTION:2026-09-19 user decision]. The relaxation kernel (`compartment_t::step` phase
`phase_explore_paths`: min-plus over a halts² matrix of `path_element_t`) is structurally
identical on master and ex-15 [CODE ex-15 @ d4893f372]. Its measured CPU share is NOT a demand
indicator: a feedback controller recalibrates the per-step iteration limits
(`limit_explore_paths`/`local_explore_paths`) to spend ≈ `path_explorer_time_midpoint` (simuconf,
default 64 ms) per phase, and in network mode `nwc_routesearch_t` synchronises the limits so all
peers process identical iteration counts. A kernel speedup therefore converts into *throughput*
(shorter full-pass latency = fresher connexions, or budget headroom), not reduced CPU share. The
fixture's near-zero share is dormancy-or-capping, unresolved (open question below); the fixture
lacks live player activity and likely understates busy-server demand.

Staged as one programme with the ex-15 Y/H traversal work
(→ [ex-15 schedule-and-consists](ex-15/schedule-and-consists.md)):
1. **Design** Y/H SIMD-compatible: dense contiguous uint32-only aggregate-time array (SoA split
   from `path_element_t`/`transport_element_t`); per-cell conditions (schedule flags, invalid
   zones, transfer extras) as dense mask/bonus arrays, not per-cell branches; iteration counting
   defined for budget/network parity. **Gate:** confirm the relaxation shape admits dense vector
   loads/stores — today's cluster loop iterates scattered member indices (gather-bound; SSE2 has
   no gather). If the post-Y/H shape stays gather-bound, drop the SIMD stage; the layout is still
   worth keeping.
2. **Implement scalar first**, verified on its own (correctness, determinism, iteration parity).
3. **SIMD immediately after verification**, as a gated variant (the scalar path stays for
   Win32/non-x86/CI; x64 SSE2 floor). Oracle: byte-identical finished matrices + iteration counts
   vs the scalar reference; measure pass duration/total iterations, not CPU share.

## Open questions

- Was the path explorer's near-zero share in the measured fixture window dormancy or
  budget-capping? Measure an active window (induced change activity on the fixture, or a long
  capture spanning a burst): pass duration, total iterations. Reconcile with the 2026-09-09
  recollection (recorded in [performance](performance.md)) that on busy servers changes outpace
  it. Prerequisite for stage 1 above.
