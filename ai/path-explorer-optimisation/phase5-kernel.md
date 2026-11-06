---
status: draft
verified: ex-15 @ bf447619a
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
  `[via][idx]` is finite and `idx != via`, `idx` is appended to `inbound_connections` under
  `transport_matrix[idx][via].last_transport` and to `outbound_connections` under
  `transport_matrix[via][idx].first_transport`. Clusters are keyed by transport; members are
  appended in ascending index order.
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
- Access pattern: for each (origin cluster, target cluster) pair the loop walks every origin row
  of the cluster at the target cluster's columns. A transfer halt with k outbound clusters
  therefore fetches each origin row up to k times, at typically one cache line per cell when the
  rows do not stay in cache. This scattered access is what makes the present code unsuitable for
  SIMD.

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

Interaction with the closure bug (→ [known-bugs](../known-bugs.md)): the present output includes
the wrapped sums written when `[o][via]` is `UINT32_MAX`. A kernel that excludes such origins (the
dense kernel below does) is not byte-identical to the present code. Fix the bug first; the fixed
scalar code is then the reference for every variant.

## Lever 1 — split arrays

- Times in one contiguous `uint32` H×H block; next transfers in a `uint16` block; first and last
  transports in two `uint16` blocks (working matrix only); one allocation per array instead of one
  per row.
- Finished matrix 6 B per cell (from 8), working 10 B per cell (from 12). `get_path_between` loses
  one dependent load (the row pointer).
- `rdwr` can write the same byte sequence as now (time then handle per cell), so the savegame
  format does not change.

## Lever 2 — loop reorder

- Per transfer halt: for each origin cluster, for each origin member, for each target cluster
  (skipping the same-transport cluster), for each target member. Each origin row is fetched once
  per transfer halt instead of once per target cluster.
- Expected traffic reduction ~2–8×, depending on cluster sizes; lever 1 halves the read side
  again.
- Budget: count iterations by the same per-origin rule in the new order. All peers run the same
  code, so stop points are identical.
- Savegame: the saved resume counters change meaning. Either give them new semantics with savegame
  version handling, or restart the current transfer halt from its first origin on load
  (idempotent by the invariant). Restarting is consistent in network play because every peer saves
  and reloads on a client join (`nwc_sync_t`). Loading saves written by the present code while a
  transfer halt was part-processed needs the restart rule in either case. Both options are
  AGENTS.md rule-5 load/save changes.

## Lever 3 — SSE2 dense masked row kernel

- Per (transfer halt, origin cluster), build a dense `uint32` array `b_eff[t]`:
  `[via][t] + delta` when t is an outbound target outside the skipped same-transport cluster,
  otherwise `UINT32_MAX`. `delta` is the Y/H substitution for that cluster pair (0 otherwise).
  Cost O(H) per origin cluster.
- Per origin o, with `a = [o][via]` (finite once the bug is fixed), in blocks of 4 cells:
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
- Expected ~2–4× on the compute side, after which the loop is limited by DRAM bandwidth (≈ 4 B
  read per cell). AVX2 (8 lanes, `vpminud`) behind runtime dispatch (lever 10) adds little once
  the loop is bandwidth-bound.
- This formulation meets the gate in [simd-applicability](../simd-applicability.md) (dense vector
  loads, no gathers); the present loop shape does not. The scalar path stays for Win32, non-x86,
  CI and as the oracle; the SSE2 variant is gated to x64.

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

## Lever 9 — GPU

- Per transfer halt: one dense min-plus update of the working block, one GPU thread per cell. The
  inputs are fixed for the duration (invariant), so the result is bit-exact with the CPU kernel.
- VRAM per compartment: 10 B × H² (0.25 / 1 / 4 GB at H = 5k / 10k / 20k). Finished matrices stay
  in host memory; the download is 6 B × H² per pass.
- Estimated 0.1–0.2 ms per transfer halt at 0.5–1 TB/s, ≈ 0.5–1 s per compartment at H = 5k.
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
and verify correctness, determinism and iteration parity. (3) Add the SIMD variant immediately
after verification, gated to x64. CPU share is not a demand indicator, because the controller
holds each phase near `path_explorer_time_midpoint`; a faster kernel shows up as shorter passes
(fresher connexions) or as budget headroom. Fallback rule: if the kernel prototype shows that the
dense formulation does not pay (or the post-Y/H loop cannot be made dense), drop the SIMD stage;
the lever-1 layout is still worth keeping.

## Verification

- Oracle: byte-identical finished matrices and identical total iteration counts against the
  bug-fixed scalar reference, on the gargantuan fixture and on synthetic maps.
- Loopback server plus client with a checklist comparison every sync step and zero divergence.
- Performance suite: pass duration and total iterations (→ [performance](../performance.md)).
