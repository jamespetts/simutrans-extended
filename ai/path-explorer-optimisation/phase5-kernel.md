---
status: draft
verified: path-explorer-optimisation @ 186c92e72
---
# Path explorer optimisation: the phase-5 kernel

**Covers:** `path_explorer_t::compartment_t::step` phase `phase_explore_paths` (path_explorer.cc),
the flat matrix arrays and the `target_times` dense row (path_explorer.h/.cc), and the phase-5
resume state in `compartment_t::rdwr`. Parent: [path-explorer-optimisation](../path-explorer-optimisation.md)
(cost model, lever list, constraints). Untagged prose is [CODE] at the frontmatter value;
performance figures are [UNVERIFIED] derivations unless tagged.

## Current loop structure

- Transfer halts are processed in `transfer_list` order (working-index order of halts whose
  serving count exceeds 1).
- For each transfer halt `via`, a registration loop visits every working halt `idx`. If
  `working_times[idx][via]` is finite and `idx != via`, `idx` is appended to
  `inbound_connections` under `working_last_transports[idx][via]`; if `working_times[via][idx]`
  is finite and `idx != via`, it is appended to `outbound_connections` under
  `working_first_transports[via][idx]`. The two directions are registered separately because
  either cell can be unreachable. Clusters are keyed by transport; members are appended in
  ascending index order.
- For each origin cluster, `target_times[t]` is built once: `working_times[via][t]` where `t` is a
  legal target, or `UINT32_MAX` for unreachable cells, for `via` itself, and for the
  same-transport outbound cluster (skipped when both transports are equal and non-zero; 0 is
  walking).
- For each origin member, `relax_target_row()` sweeps every `t` in blocks of four (SSE2 or NEON)
  with a scalar tail. `combined = working_times[o][via] + target_times[t]`; on a strict
  improvement it writes the time, `next_transfer = [o][via].next_transfer`,
  `first_transport = working_first_transports[o][via]` and
  `last_transport = working_last_transports[via][t]`. The four arrays are contiguous over `t`.
- Budget: the registration adds `working_halt_count + 2 × inbound members` iterations; each
  origin member adds the non-skipped outbound member count. The quantum stops after an origin
  member once the count reaches `limit_explore_paths`.
- Savegame: a revision-7 save carries `via_index`, `origin_cluster_index`, `origin_member_index`,
  `target_cluster_index` (always 0), both cluster structures, `process_next_transfer` and the
  controller statistics. A revision-6-or-older mid-pass save restarts the current inbound cluster
  (the origin-member and target-cluster counters are cleared), which is idempotent by the
  invariant below.

## Order-independence invariant

For one transfer halt `via`:

1. the loop reads only `[o][via]` and `[via][t]` (times, next transfer, transports), and none of
   them changes during the loop;
2. it writes only `[o][t]` with `o != via` and `t != via`, so it never writes a cell it reads;
3. every halt is registered in exactly one inbound and one outbound cluster, so each (o, t) pair
   is visited at most once; `[o][o]` is 0 and never improves.

Consequences: within one transfer halt, the order of the updates and any division of them across
threads, SIMD lanes or GPU threads give identical cells, independent of the thread count.
Re-running a partly processed transfer halt gives the same final cells, because writes happen
only on strict improvement and the inputs are unchanged. The order across transfer halts does
matter and must be kept.

Directional registration: an origin is registered only when `[o][via]` is finite, and the
relaxation skips `[o][via] == UINT32_MAX`, so the dense kernel needs no separate exclusion rule.
The scalar cell function is the reference for every variant.

## Matrix layout

- Times in one contiguous `uint32` H×H block; next transfers in a `uint16` block; first and last
  transports in two `uint16` blocks (working matrix only); one allocation per array instead of one
  per row.
- Finished matrix 6 B per cell; working matrix 10 B per cell. `get_path_between` needs one load,
  not a row pointer followed by a cell load.
- `rdwr` writes time then handle per cell, so the savegame byte format is unchanged by the layout.
- The arrays are `finished_times` / `finished_next_transfers` and `working_times` /
  `working_next_transfers` / `working_first_transports` / `working_last_transports`; a cell is
  `row * count + column`, with the product computed in `size_t`.
- The dead `enumerate_all_paths` debug printer is removed; its signature exposed the old
  row-pointer layout.

## Loop shape and savegame

- The production loop is the dense per-origin sweep: one `target_times[]` row per origin cluster,
  then every `t` for each origin member. It visits every halt per origin member, not only
  target-cluster members. The dense scalar form is 2.2-3.9x slower in phase 5 than the sparse
  loop; the SIMD kernel is what makes the dense form pay (below).
- The original sparse loop remains the design for the optional dense/sparse selector: per origin
  member, walk only the members of each non-skipped target cluster. It is faster when the
  outbound set is a small fraction of H (early in a pass, small freight compartments). Choosing
  dense or sparse per transfer halt does not change results. The selector is not implemented.
- Iteration counting is per origin member over the non-skipped outbound member count, so a full
  pass produces the same `total_iterations` under either loop shape. All peers run the same code,
  so stop points are identical.
- `EX_SAVE_MINOR` is 7. A revision-7 save resumes exactly with (transfer, inbound cluster,
  completed origins in that cluster); `target_cluster_index` is always 0. A revision-6-or-older
  mid-pass save restarts the current inbound cluster, which is idempotent and is not a full
  refresh. A network join triggers a save/reload on every peer (`nwc_sync_t`), so exact resume
  avoids discarding path-explorer progress at every join.

## SIMD kernel

- `relax_target_row()` has one definition per build, selected by `USE_SSE2`, `USE_NEON` or the
  scalar fallback. The source auto-detects the backend from the compiler's architecture macros;
  the build systems expose `USE_SSE2` / `USE_NEON` as overrides (→ [build-and-toolchain](../build-and-toolchain.md)).
- Per origin, in blocks of four cells: load `target_times` and the `[o][t]` time row, compute
  `sum = [o][via] + target_times`, test `sum < [o][t]` for valid lanes, blend the time, and store
  the block only if a lane improved. The 16-bit next-transfer, first-transport and
  last-transport arrays are written only for improved lanes, which is rare late in a pass.
- SSE2 has no unsigned 32-bit compare: both sides are biased by 0x80000000 before `pcmpgtd`.
  NEON compares unsigned directly (`vcgtq_u32`) and selects with `vbslq_u32`; it needs no sign
  bias. Both treat `target_times[t] == UINT32_MAX` as invalid.
- Valid sums cannot wrap: real aggregate times are far below 2^31 tenths of a minute
  [UNVERIFIED bound].
- Determinism: integer operations with the same strict less-than as the scalar cell function, so
  the output is byte-identical to the scalar reference.
- Targets: x64 SSE2 and AArch64 NEON [RECOLLECTION:2026-11-07], because Apple users are a
  supported audience and AArch64 mandates NEON. SSE2 is execution-verified against the scalar
  oracle. NEON is compile-verified by the native macOS CI; no NEON execution hardware is
  available, so execution verification is outstanding.
- AVX2 (8 lanes, `vpminud`) behind runtime dispatch (lever 10) is a possible later addition.
- Measured run totals including load, with the current dense SIMD kernel compared with the sparse
  loop and the dense scalar oracle:

| Fixture | Sparse | Dense scalar | Dense SSE2 | SSE2 vs sparse |
|---|---:|---:|---:|---:|
| month-boundary | 17.47 s | 33.36 s | 18.88 s | +8% |
| bb6-apr | 29.82 s | 57.58 s | 30.64 s | +3% |
| bb6-may | 33.34 s | 61.14 s | 31.83 s | -5% |
| bb-10-sep-2023 | 264.86 s | 555.28 s | 214.77 s | -19% |

  The dense SIMD is 1.77-2.59x faster than the dense scalar. It is a clear win on the gargantuan
  fixture, neutral on the intermediates and 8% slower on the smallest. The optional dense/sparse
  selector (threshold on `H / outbound_member_count`, measured crossover about 2.6) would remove
  the small-map regression. [EXECUTION-VERIFIED:2026-11-07, `path-explorer-optimisation` @
  3b18f76aa, Profile (server) x64; the single-function refactor reproduces the month-boundary
  oracle at 186c92e72.]

## Sparse-loop cost (optional fallback)

These are the measured costs of the sparse loop shape, which is the planned selector fallback,
not the current production loop. [EXECUTION-VERIFIED:2026-11-06, ex-15 @ ad1815c, Profile (server),
full unbudgeted pass, `month-boundary.sve`, `bb6-apr-2010.sve`, `bb6-may-2010-standardised.sve`
and `bb-10-sep-2023.sve`.]

| Fixture | H | T | Phase-5 time per class | True target visits per class | ns/visit |
|---|---|---:|---:|---:|---:|
| month-boundary.sve | 1,828 | 1,610 | 1.5–1.8 s | 0.73–0.85 B | ~2.1 |
| bb6-apr-2010.sve | 2,467 | 2,280 | 5.17–5.42 s | 2.95 B | 1.84 |
| bb6-may-2010-standardised.sve | 2,467 | 2,280 | 6.27–7.08 s | 3.55–3.67 B | 1.9–1.97 |
| bb-10-sep-2023.sve | 5,375 | 4,665 | 77.6–79.4 s | 42.6–44.0 B | 1.77–1.85 |

The per-visit cost is stable at 1.8–2.1 ns; phase-5 time is proportional to the true target
visits. The true visit count grows from 0.85 B to 44 B over H 1.8k → 5.4k (×52, effective
exponent ≈ 3.65), so H³ understates the growth by about 2×. Topology matters independently of
H/T/E: `bb6-apr` and `bb6-may` have identical H, T and E but 2.95 B vs 3.64 B visits and 5.2 vs
6.5 s per class. Cluster counters on `bb6-apr`: 2.20 inbound clusters per transfer, average
cluster 790 members, 5.42 M row fetches versus 2.95 B target visits, 26.9 M improvements
(0.91%). On `bb`: 2.44 clusters, average 1,668 members, 31.7 M row fetches versus 44.0 B visits,
182 M improvements (0.41%). The target loop dominates; the row re-fetch is under 0.1% of the
work.

At the 64 ms budget a pass is spread over wall-clock steps, so a faster kernel buys fresher
routes or a lower `path_explorer_time_midpoint`, not a proportional wall-time reduction by itself.

## Open question: mid-pass cross-class result

A mid-pass save whose earlier compartment is complete, then loaded and finished, gives different
matrices for the later classes than a full refresh of the same save. A control run refreshing
only classes 1-4 (skipping class 0) reproduces the resumed result exactly, so the difference comes
from class 0's recomputation versus its loaded state, not from the loop order. Cause not
identified. This does not affect network sync: every peer loads the same save and runs the same
deterministic code, so peers compute the same matrices; the difference is between a full refresh
and a resumed pass on the same save, not between peers [RECOLLECTION:2026-11-07, user
instruction].

## Lever 4 — extra threads

- Divide the origins of one transfer halt among N explorer threads and join before the next
  transfer halt (order across transfer halts matters). One join per transfer halt costs
  microseconds, T times per pass.
- Stop points must come from the canonical iteration count, independent of N. Peers must run the
  same `threads` setting anyway (checklist `debug_sums[4]`).
- The explorer runs between steps, alongside `sync_step`, the display and the convoy
  route-finding threads. Extra threads compete for cores and for DRAM bandwidth (the primary
  constraint, [project-notes](../project-notes.md)); the gain is bounded at ~2–4×. CPU per step
  rises: the controller's time target becomes N × 64 ms of CPU. [threading](../threading.md) is
  mandatory.
- User decision 2026-11-06 [RECOLLECTION:2026-11-06]: adding threads and CPU load to shorten
  the rebuild cycle is worthwhile, because CPU cores are underused during gameplay and route
  staleness is a real in-game problem. Settle the deterministic iteration/stop-point model first;
  it remains sync-critical.

## Lever 9 — GPU

- Per transfer halt: one dense min-plus update of the working block, one GPU thread per cell. The
  inputs are fixed for the duration (invariant), so the result is bit-exact with the CPU kernel.
- VRAM per compartment: 10 B × H² (0.25 / 1 / 4 GB at H = 5k / 10k / 20k). Finished matrices stay
  in host memory; the download is 6 B × H² per pass.
- Estimated 0.05–0.2 ms per transfer halt for the sparse cluster-pair work, ≈ 0.5–1 s per
  compartment at 0.5–1 TB/s. A dense H² update per transfer halt is ~3× the true sparse visit
  count and takes ~1.5–3 s per compartment.
- Needs a compute backend, build integration in three build systems and headless server drivers;
  the CPU path remains authoritative. Network play: limited by the slowest peer (parent doc).
- Decision [RECOLLECTION:2026-11-06]: open to GPU optimisation, not requiring it; implementation
  is decided by a later cost/benefit analysis. Requirements on the CPU design so that a backend can
  be added without redesign:
  - contiguous, pointer-free arrays, including the per-origin-cluster `target_times` row;
  - per-transfer-halt updates expressed as dense operations whose result does not depend on the
    order of updates (the invariant above), with any Y/H adjustment as a per-cluster-pair value;
  - iteration counting and stop points defined from the data, not from the executing engine;
  - one selection point for the kernel implementation, with the scalar CPU path complete,
    default and the verification oracle.

## Y/H requirements on the kernel

- The through-journey substitution (→ [ex-15 yh-path-explorer](../ex-15/yh-path-explorer.md),
  "Through-journey substitution") depends only on the inbound and outbound cluster transports at
  the transfer halt. It is therefore one constant per (origin cluster, target cluster) pair:
  compute it once per pair outside the inner loop (sparse loop) or fold it into `target_times`
  (dense loop). The invariant is preserved. This is cheaper than a lookup and a branch per cell.
  The hook is not implemented.
- The substituted sum cannot become negative: for a through pair the outbound transport is a
  linkage, so the first connexion of `[via][t]` includes via's own transfer time.
- Through edges recorded in phase 2 span two linkages, but phase 4 sets
  `first_transport = last_transport`. Through edges need the source linkage as first and the
  target linkage as last.
- The added edges change neither the matrix size nor the kernel shape.

## Current position and remaining work

The flat layout, the dense loop and the SSE2/NEON kernels are in place. Remaining kernel work: the
optional dense/sparse selector, lever 4 (threading and deterministic stop points), the Y/H hooks
above, and the memory/GPU levers.

## Verification

- Oracle: byte-identical finished matrices and identical total iteration counts against the
  scalar cell function, on the gargantuan fixture and on synthetic maps. The scalar oracle is
  identical on every platform, which is what makes the SSE2 and NEON variants independently
  checkable.
- SSE2: execution-verified on x64. NEON: compile-verified by the native macOS CI build; execution
  verification uses the same oracle on Apple Silicon where available.
- Loopback server plus client with a checklist comparison every sync step and zero divergence.
- Performance suite: pass duration and total iterations (→ [performance](../performance.md)).
