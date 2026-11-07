---
status: draft
verified: ex-15 @ ad753f5c6 (path-explorer SIMD, merged from path-explorer-optimisation @ 186c92e72)
---
# SIMD (SSE/AVX) applicability

**Covers:** SIMD/intrinsics applicability across the simulation hot paths (graphics excluded):
baseline-ISA and determinism constraints, and per-hotspot verdicts against the measured inventory
in [performance](performance.md). The path explorer kernel's SIMD design and staged position →
[path-explorer-optimisation](path-explorer-optimisation.md).

## Why this doc

[performance](performance.md) records what is hot and how it is measured; this doc records where
intrinsics could pay off, so that SIMD is not speculatively added to paths whose cost is memory
latency or pointer chase, and so that the one kernel where SIMD is structurally feasible (the
path explorer relaxation) is designed for it from the start.

## Current state and constraints

The path explorer dense-row kernel is the first simulation SIMD code: SSE2 on x64 and NEON on
AArch64. The only earlier assembler/SIMD-era code is graphics (`USE_ASSEMBLER`,
display/simgraph16.cc, GNUC i686 only) [CODE master @ 075d540f3].

- **Baseline ISA:** builds target Win32 and x64 (MSVC) plus GNU make/CMake, including native
  macOS/Apple Silicon builds. `-march=native` is opt-in (`TUNE_NATIVE`, Makefile). SSE2 is a safe
  floor on x64 only (x86-64 ABI guarantee); Win32 x86 needs compile-flag gating. AArch64 mandates
  NEON, so Apple Silicon and ARM64 Linux have a first-class SIMD target; NEON intrinsics use
  `<arm_neon.h>`. AVX2+ needs runtime dispatch that does not exist. MMX is obsolete — never
  prefer it to SSE2.
- **Platform decision** [RECOLLECTION:2026-11-07]: the path explorer kernel implements SSE2 (x64)
  and NEON (AArch64) together, not SSE2 alone, because Apple users are a supported audience. The
  scalar path remains the portable base and the oracle on every target, including Win32 x86 and
  any platform without either backend.
- **Backend selection:** compile-time and mutually exclusive. The source auto-detects x86-64 SSE2
  and AArch64 NEON; `USE_SSE2`/`USE_NEON` are the generic override macros, exposed as GNU make
  `USE_SSE2=1`/`USE_NEON=1` and CMake `SIMUTRANS_USE_SSE2`/`SIMUTRANS_USE_NEON`
  (→ [build-and-toolchain](build-and-toolchain.md)).
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
| Path explorer `phase_explore_paths` | The only genuine dense-arithmetic kernel. Gather-bound as written; a dense masked row formulation runs on the SSE2 and NEON baselines without gathers and leaves results unchanged; implemented → [path-explorer-optimisation](path-explorer-optimisation.md) |
| `grund_t::get_weg`, `karte_t::lookup`, private cars, pedestrians | Memory-latency/pointer-chase bound — not amenable |
| Passenger generation, `karte_t::find_destination`, `simrand` | RNG call order is sync/output-defining — untouchable |
| `route_t` A* | Sequential heap-driven expansion — not amenable |
| Mapgen `stadt_t::bewerte_loc` | Branchy per-entry predicate switch over memoised flag bytes; low ceiling; the algorithmic targets in the mapgen section of [performance](performance.md) dominate |
| Savegame load | zstd decompression (debug-lib caveat in [performance](performance.md)) — the lever is a release-built zstd (already SIMD-optimised upstream), not game-code intrinsics |

## Path explorer

The staged position [RECOLLECTION:2026-09-19 user decision] is: design SIMD-compatible, then
implement and verify scalar, then add the gated SIMD variants immediately. The dense-vectorisation
gate is met by a dense masked row formulation of the closure. The variants are implemented:
SSE2 on x64 and NEON on AArch64 [RECOLLECTION:2026-11-07]. SSE2 is execution-verified against the
scalar oracle on the four fixtures and the mid-pass resume fixtures; NEON is compile-verified by
the native macOS CI. The scalar path remains the oracle on every platform and the fallback where
neither backend applies. Kernel design, invariants and verification:
[path-explorer-optimisation](path-explorer-optimisation.md).

## Open questions

- Path-explorer questions → [path-explorer-optimisation](path-explorer-optimisation.md).
