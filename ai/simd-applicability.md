---
status: draft
verified: ex-15 @ a203862c1 (2026-11-07)
---
# SIMD (SSE/AVX) applicability

**Covers:** SIMD/intrinsics applicability across the simulation hot paths (graphics excluded):
baseline-ISA and determinism constraints, and per-hotspot verdicts against the measured inventory
in [performance](performance.md). The path-explorer routing-search engine →
[routing-search-kernel](path-explorer-optimisation/routing-search-kernel.md).

## Why this doc

[performance](performance.md) records what is hot and how it is measured; this doc records where
intrinsics could pay off, so that SIMD is not speculatively added to paths whose cost is memory
latency or pointer chase.

## Current state and constraints

The simulation has **no SIMD kernel**. The routing-search path-explorer search is a scalar graph
search. The only assembler/SIMD-era code is graphics (`USE_ASSEMBLER`,
display/simgraph16.cc, GNUC i686 only) [CODE master @ 075d540f3]. The build defines no SIMD
backend options.

- **Baseline ISA:** builds target Win32 and x64 (MSVC) plus GNU make/CMake, including native
  macOS/Apple Silicon builds. `-march=native` is opt-in (`TUNE_NATIVE`, Makefile). SSE2 is a safe
  floor on x64 only (x86-64 ABI guarantee); Win32 x86 needs compile-flag gating. AArch64 mandates
  NEON, so Apple Silicon and ARM64 Linux can use NEON if a future kernel needs it. AVX2+ needs
  runtime dispatch that does not exist. MMX is obsolete — never prefer it to SSE2.
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
| Path explorer `phase_explore_paths` | Not applicable: the routing search is a scalar expanded-state graph search (per-origin Dijkstra), not a dense arithmetic kernel → [routing-search-kernel](path-explorer-optimisation/routing-search-kernel.md) |
| `grund_t::get_weg`, `karte_t::lookup`, private cars, pedestrians | Memory-latency/pointer-chase bound — not amenable |
| Passenger generation, `karte_t::find_destination`, `simrand` | RNG call order is sync/output-defining — untouchable |
| `route_t` A* | Sequential heap-driven expansion — not amenable |
| Mapgen `stadt_t::bewerte_loc` | Branchy per-entry predicate switch over memoised flag bytes; low ceiling; the algorithmic targets in the mapgen section of [performance](performance.md) dominate |
| Savegame load | zstd decompression (debug-lib caveat in [performance](performance.md)) - the fix is a release-built zstd (already SIMD-optimised upstream), not game-code intrinsics |

## Path explorer

The routing search is scalar: it follows irregular adjacency lists, uses a binary heap and is
memory-latency bound, so there is no dense arithmetic to vectorise. The engine and its
verification are in [routing-search-kernel](path-explorer-optimisation/routing-search-kernel.md).

## Open questions

- Path-explorer questions → [path-explorer-optimisation](path-explorer-optimisation.md).
- A future many-source graph search might be a GPU problem rather than a SIMD one.
