---
status: draft
verified: path-explorer-optimisation @ 186c92e72
---
# Path explorer optimisation: the phase-5 kernel

**Covers:** `path_explorer_t::compartment_t::step` phase `phase_explore_paths` (path_explorer.cc),
`path_element_t` / `transport_element_t` (path_explorer.h), and the phase-5 resume state in
`compartment_t::rdwr`. Parent: [path-explorer-optimisation](../path-explorer-optimisation.md)
(cost model, lever list, constraints). Untagged prose is [CODE] at the frontmatter value;
performance figures are [UNVERIFIED] derivations.

## Present loop structure

- Transfer halts are processed in `transfer_list` order (working-index order of halts whose
  serving count exceeds 1).
- For each transfer halt `via`, a registration loop visits every working halt `idx`. If
  `[idx][via]` is finite and `idx != via`, `idx` is appended to `inbound_connections` under
  `transport_matrix[idx][via].last_transport`; if `[via][idx]` is finite and `idx != via`, it is
  appended to `outbound_connections` under `transport_matrix[via][idx].first_transport`. The
  two directions are registered separately because either cell can be unreachable. Clusters are
  keyed by transport; members are appended in ascending index order.
- Nested loops: origin cluster → target cluster (skipped when both transports are equal and
  non-zero; 0 is walking) → origin member → target member. `combined = [o][via] + [via][t]`; if it
  is less than `[o][t]`, the code writes the time, `next_transfer = [o][via].next_transfer`,
  `first_transport = transport[o][via].first_transport` and
  `last_transport = transport[via][t].last_transport`.
- Budget: the registration adds `working_halt_count + 2 × inbound members` iterations; each origin
  member adds the target cluster's size; the quantum stops after an origin member once the count
  reaches `limit_explore_paths`. The savegame carries the resume state: `via_index`, both cluster
  indices, `origin_member_index`, both cluster structures, `process_next_transfer` and the
  controller statistics.
- Access pattern: for each (origin cluster, target cluster) pair the loop walks origin members
  and target members. Target members are appended in ascending index order, so for a fixed
  origin and via the `[via][t]` and `[o][t]` accesses are contiguous over the target cluster;
  this is the SIMD-vectorisable part. Origin rows are re-fetched per target cluster, but the
  measured row fetches are under 0.1% of the target visits (one row fetch per 544–1,389
  target visits), so re-fetching is not the dominant cost.

### Measured cost (2026-11-06)

[EXECUTION-VERIFIED:2026-11-06, ex-15 @ ad1815c, Profile (server), full unbudgeted pass,
`month-boundary.sve`, `bb6-apr-2010.sve`, `bb6-may-2010-standardised.sve` and
`bb-10-sep-2023.sve`.]

| Fixture | H | T | Phase-5 time per class | True target visits per class | ns/visit |
|---|---:|---:|---:|---:|---:|
| month-boundary.sve | 1,828 | 1,610 | 1.5–1.8 s | 0.73–0.85 B | ~2.1 |
| bb6-apr-2010.sve | 2,467 | 2,280 | 5.17–5.42 s | 2.95 B | 1.84 |
| bb6-may-2010-standardised.sve | 2,467 | 2,280 | 6.27–7.08 s | 3.55–3.67 B | 1.9–1.97 |
| bb-10-sep-2023.sve | 5,375 | 4,665 | 77.6–79.4 s | 42.6–44.0 B | 1.77–1.85 |

The per-visit cost is stable at 1.8–2.1 ns; phase-5 time is proportional to the true target
visits. The true visit count grows from 0.85 B to 44 B over H 1.8k → 5.4k (×52, effective
exponent ≈ 3.65), so H³ understates the growth by about 2×.

Topology matters independently of H/T/E: `bb6-apr` and `bb6-may` have identical H, T and E
but 2.95 B vs 3.64 B visits and 5.2 vs 6.5 s per class. Cluster counters on `bb6-apr`:
2.20 inbound clusters per transfer, average cluster 790 members, 5.42 M row fetches versus
2.95 B target visits, 26.9 M improvements (0.91%). On `bb`: 2.44 clusters, average 1,668
members, 31.7 M row fetches versus 44.0 B visits, 182 M improvements (0.41%). The target
loop dominates; the row re-fetch is under 0.1% of the work.

Under normal server pacing the same 394.7 s of CPU is spread over 18–20 min wall at ~60 ms
of kernel work per step, so a faster kernel buys fresher routes or a lower
`path_explorer_time_midpoint`, not a proportional wall-time reduction by itself.

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
relaxation skips `[o][via] == UINT32_MAX`, so the dense kernel below needs no separate exclusion
rule. The scalar code is the reference for every variant.

## Lever 1 — split arrays

- Times in one contiguous `uint32` H×H block; next transfers in a `uint16` block; first and last
  transports in two `uint16` blocks (working matrix only); one allocation per array instead of one
  per row.
- Finished matrix 6 B per cell (from 8), working 10 B per cell (from 12). `get_path_between` loses
  one dependent load (the row pointer).
- `rdwr` writes the same byte sequence as before (time then handle per cell), so the savegame
  format does not change.

**Implemented 2026-11-07** on `path-explorer-optimisation`, commit `e5fdd5f37`. The arrays are
`finished_times` / `finished_next_transfers` and `working_times` / `working_next_transfers` /
`working_first_transports` / `working_last_transports`; a cell is `row * count + column`, with the
product computed in `size_t`. The dead `enumerate_all_paths` debug printer, whose signature exposed
the row-pointer layout, was removed. A TEST oracle hashing every finished cell after an unbudgeted
refresh produced identical matrices and `total_iterations` on `month-boundary`, `bb6-apr`,
`bb6-may` and `bb-10-sep-2023`, and an identical save/load round trip; phase-5 time fell by 40-56%
(1.6-1.7x) on those fixtures [EXECUTION-VERIFIED:2026-11-07, `path-explorer-optimisation` @
e5fdd5f37, Profile (server) x64].

## Lever 2 — loop reorder

- Per transfer halt: for each origin cluster, for each origin member, for each target cluster
  (skipping the same-transport cluster), for each target member. Each origin row is fetched once
  per transfer halt instead of once per target cluster.
- Expected gain is bounded by the row re-fetch share: measured one origin-row fetch per
  544–1,389 target visits (under 0.1% of the loop work), so the direct reduction is small.
  Its value is locality and simpler loop structure, not a 2–8× reduction. Lever 1 halves the
  read side of the remaining target loop.
- Budget: count iterations by the same per-origin rule in the new order. All peers run the same
  code, so stop points are identical.
- Savegame: the saved resume counters change meaning. Decision 2026-11-07 [RECOLLECTION:2026-11-07]:
  bump `EX_SAVE_MINOR` to 7 and resume exactly with (transfer, inbound cluster, completed origins in
  that cluster); `target_cluster_index` is always 0 in a revision-7 save. A save from revision 6 or
  older, if it is mid-pass, restarts the current inbound cluster (set the origin-member and
  target-cluster counters to 0 and keep the cluster and transfer). That is idempotent by the
  invariant above and is not a full refresh. A network join triggers a save/reload on every peer
  (`nwc_sync_t`), so exact resume avoids discarding path-explorer progress at every join.

**Implemented 2026-11-07** in commit `63e5d2ac1` as the scalar dense staging form, with the
revision-7 gate. Full unbudgeted refreshes on the four fixtures produce identical matrices and
`total_iterations`; the interrupted compartment of a mid-pass save resumes to the same matrix.
The dense sweep is 2.2-3.9x slower in phase 5 than the sparse loop (1.8-2.1x on the run total
including load), as expected: it visits every halt per origin rather than only target-cluster
members. The SIMD stage must recover more than that to pay; otherwise revert this reorder and keep
lever 1, which is a 40-56% gain on its own [EXECUTION-VERIFIED:2026-11-07,
`path-explorer-optimisation` @ 63e5d2ac1, Profile (server) x64].

**Open question (2026-11-07)**: a mid-pass save whose earlier compartment is complete, then loaded
and finished, gives different matrices for the later classes than a full refresh of the same save.
A control run refreshing only classes 1-4 (skipping class 0) reproduces the resumed result exactly,
so the difference comes from class 0's recomputation versus its loaded state, not from the loop
order. Cause not identified. This does not affect network sync: every peer loads the same save and
runs the same deterministic code, so peers compute the same matrices; the difference is between a
full refresh and a resumed pass on the same save, not between peers [RECOLLECTION:2026-11-07, user
instruction].

## Lever 3 — SSE2 and NEON dense masked row kernel

- Per (transfer halt, origin cluster), build a dense `uint32` array `b_eff[t]`:
  `[via][t] + delta` when t is an outbound target outside the skipped same-transport cluster,
  otherwise `UINT32_MAX`. `delta` is the Y/H substitution for that cluster pair (0 otherwise).
  Cost O(H) per origin cluster.
- Per origin o, with `a = [o][via]` (finite), in blocks of 4 cells:
  `sum = a + b_eff`; `valid = (b_eff != MAX)`; `improve = valid && sum < c`; `c = blend(improve,
  sum, c)`. SSE2 has no unsigned 32-bit compare or min: use a sign-bias XOR before `pcmpgtd`.
  Store a block only if a lane improved (`movemask`); late in a pass improvements are rare, so
  most blocks are only read. Blend the 16-bit arrays with packed masks: next transfer and first
  transport are constant per origin; last transport comes from the `[via][*]` row.
- Valid sums cannot wrap: real aggregate times are far below 2^31 tenths of a minute
  [UNVERIFIED bound].
- When the outbound set is a small fraction of H (early in a pass, small freight compartments),
  the sparse scalar loop is cheaper. Choosing dense or sparse per transfer halt does not change
  results.
- Determinism: integer operations with the same strict less-than as the scalar loop, so the
  output is byte-identical to the scalar reference.
- Targets: x64 SSE2 and AArch64 NEON are implemented together [RECOLLECTION:2026-11-07], because
  Apple users are a supported audience and AArch64 mandates NEON. NEON uses four `uint32` lanes
  like SSE2 but compares unsigned directly (`vcgtq_u32`) and selects with `vbslq_u32`, so the
  sign-bias XOR is unnecessary. Both backends store the 32-bit times as a four-lane blend and
  write the 16-bit arrays per improved lane, which is rare. macOS CI builds natively on Apple
  Silicon, so the NEON path is compile-tested there; no NEON execution hardware is available.
- Measured 1.77-2.59x against the dense scalar: roughly break-even with the sparse loop on the
  small fixtures and a 19% win at H ≈ 5.4k. SIMD is the primary CPU lever for large maps; AVX2
  (8 lanes, `vpminud`) behind runtime dispatch (lever 10) may add further once SSE2 is in place.
- This formulation meets the gate in [simd-applicability](../simd-applicability.md) (dense vector
  loads, no gathers); the present loop shape does not. The scalar path stays for Win32 x86 and
  any target without SSE2/NEON, and is the oracle; the SSE2 variant is gated to x64 and the NEON
  variant to AArch64. The source auto-detects from the compiler's architecture macros; `USE_SSE2`
  and `USE_NEON` are the generic override macros, exposed as GNU make `USE_SSE2=1`/`USE_NEON=1`
  and CMake `SIMUTRANS_USE_SSE2`/`SIMUTRANS_USE_NEON`. They are mutually exclusive
  (→ [build-and-toolchain](../build-and-toolchain.md)).

**Implemented 2026-11-07** in commit `3b18f76aa`, refactored to a single `relax_target_row` with
one definition per backend in `186c92e72`: SSE2 (x64) and NEON (AArch64) dense row kernels, with
the scalar cell function as oracle and fallback. SSE2 execution-verified on the four fixtures and
both mid-pass resume fixtures (identical hashes and `total_iterations`); the single-function
refactor reproduces the month-boundary oracle. Run totals including load:

| Fixture | Sparse | Dense scalar | Dense SSE2 | SSE2 vs sparse |
|---|---:|---:|---:|---:|
| month-boundary | 17.47 s | 33.36 s | 18.88 s | +8% |
| bb6-apr | 29.82 s | 57.58 s | 30.64 s | +3% |
| bb6-may | 33.34 s | 61.14 s | 31.83 s | -5% |
| bb-10-sep-2023 | 264.86 s | 555.28 s | 214.77 s | -19% |

The dense SIMD is a clear win on the gargantuan fixture, neutral on the intermediates and 8%
slower on the smallest. The optional dense/sparse selector (threshold on
`H / outbound_member_count`, measured crossover about 2.6) would remove the small-map regression;
it is not implemented. NEON is compile-verified by the native macOS CI (all macOS jobs passed on
`186c92e72`), not execution-verified.
[EXECUTION-VERIFIED:2026-11-07, `path-explorer-optimisation` @ 3b18f76aa, Profile (server) x64]

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
  staleness is a real in-game problem. Implement this after the layout/SIMD kernel and settle
  the deterministic iteration/stop-point model first; it remains sync-critical.

## Lever 9 — GPU

- Per transfer halt: one dense min-plus update of the working block, one GPU thread per cell. The
  inputs are fixed for the duration (invariant), so the result is bit-exact with the CPU kernel.
- VRAM per compartment: 10 B × H² (0.25 / 1 / 4 GB at H = 5k / 10k / 20k). Finished matrices stay
  in host memory; the download is 6 B × H² per pass.
- Estimated 0.05–0.2 ms per transfer halt for the sparse cluster-pair work (44 B target
  visits per class at H = 5k, 10 B/cell), ≈ 0.5–1 s per compartment at 0.5–1 TB/s. A dense
  H² update per transfer halt is ~3× the true visit count and takes ~1.5–3 s per
  compartment.
- Needs a compute backend, build integration in three build systems and headless server drivers;
  the CPU path remains authoritative. Network play: limited by the slowest peer (parent doc).
- Decision [RECOLLECTION:2026-11-06]: open to GPU optimisation, not requiring it; implementation
  is decided by a later cost/benefit analysis. Requirements on the CPU design so that a backend can
  be added without redesign:
  - contiguous, pointer-free arrays (lever 1), including the per-origin-cluster `b_eff` arrays;
  - per-transfer-halt updates expressed as dense operations whose result does not depend on the
    order of updates (the invariant above), with any Y/H adjustment as a per-cluster-pair value;
  - iteration counting and stop points defined from the data, not from the executing engine;
  - one selection point for the kernel implementation, with the scalar CPU path complete,
    default and the verification oracle.

## Y/H requirements on the kernel

- The through-journey substitution (→ [ex-15 yh-path-explorer](../ex-15/yh-path-explorer.md),
  "Through-journey substitution") depends only on the inbound and outbound cluster transports at
  the transfer halt. It is therefore one constant per (origin cluster, target cluster) pair:
  compute it once per pair outside the inner loop (sparse kernel) or fold it into `b_eff` (dense
  kernel). The invariant is preserved. This is cheaper than a lookup and a branch per cell.
- The substituted sum cannot become negative: for a through pair the outbound transport is a
  linkage, so the first connexion of `[via][t]` includes via's own transfer time.
- Through edges recorded in phase 2 span two linkages, but phase 4 sets
  `first_transport = last_transport`. Through edges need the source linkage as first and the
  target linkage as last.
- The added edges change neither the matrix size nor the kernel shape.

## Staged position (user decision)

[RECOLLECTION:2026-09-19]: one programme with the Y/H work. (1) Design SIMD-compatible: dense
contiguous `uint32` time array, conditions as dense mask/adjustment arrays rather than per-cell
branches, iteration counting defined for budget and network parity. (2) Implement scalar first
and verify correctness, determinism and iteration parity. (3) Add the SIMD variants immediately
after verification: SSE2 gated to x64 and NEON gated to AArch64. CPU share is not a demand
indicator, because the controller holds each phase near `path_explorer_time_midpoint`; a faster
kernel shows up as shorter passes (fresher connexions) or as budget headroom. Fallback rule: if
the kernel prototype shows that the dense formulation does not pay (or the post-Y/H loop cannot be
made dense), drop the SIMD stage; the lever-1 layout is already implemented and is worth keeping
on its own (40-56% phase-5 reduction, above).

## Verification

- Oracle: byte-identical finished matrices and identical total iteration counts against the
  scalar reference, on the gargantuan fixture and on synthetic maps. The scalar oracle is identical
  on every platform, which is what makes the SSE2 and NEON variants independently checkable.
- SSE2: execution-verified on x64. NEON: compile-verified by the native macOS CI build; execution
  verification uses the same oracle on Apple Silicon where available.
- Loopback server plus client with a checklist comparison every sync step and zero divergence.
- Performance suite: pass duration and total iterations (→ [performance](../performance.md)).
