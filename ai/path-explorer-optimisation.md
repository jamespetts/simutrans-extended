---
status: draft
verified: ex-15 @ bf447619a; master @ fa0b18fba (shared closure code)
---
# Path explorer optimisation

**Covers:** the compute and memory cost of the routing database and the options for reducing it
(software, SIMD, GPU): the refresh phases in path_explorer.{cc,h}, matrix storage and
`path_explorer_t::compartment_t::rdwr`, the phase-2 connexion tables (`haltestelle_t::connexion`,
`haltestelle_t::connexions_map`), and the consumers `haltestelle_t::find_route` /
`compartment_t::get_path_between`. Canonical home of all path-explorer optimisation work. Routing
design (Y/H traversal) → [ex-15 yh-path-explorer](ex-15/yh-path-explorer.md); SIMD constraints
for the whole simulation → [simd-applicability](simd-applicability.md); profiling method and
measured hotspots → [performance](performance.md).

Status: assessment only. Nothing in this doc is implemented; the direction is not decided (see
Open questions). Figures marked [UNVERIFIED] are derivations from code structure, not
measurements.

Sub-documents (read-when):

- [phase5-kernel](path-explorer-optimisation/phase5-kernel.md) — read when implementing or
  reviewing any change to `phase_explore_paths` (data layout, loop order, SIMD, extra threads,
  GPU): the order-independence invariant, the kernel designs, budget/parity, savegame and Y/H
  rules, and the verification oracle.
- [engine-semantics](path-explorer-optimisation/engine-semantics.md) — read before any change
  that alters route results (closure bug fix, per-source Dijkstra rows, hub labels, connexion
  deduplication): the present route semantics and worked examples of how each alternative changes
  passenger behaviour.
- [per-packet-routing](path-explorer-optimisation/per-packet-routing.md) — read before proposing
  route search at decision time instead of a precomputed database: the rejected assessment and
  its quantitative reasons.
- [comfort-routing](path-explorer-optimisation/comfort-routing.md) — read when designing or
  implementing comfort-aware routing (vehicle, stop and walking comfort in route choice and
  tolerance), the removal of the speed bonus and of comfort weighting from fares, or private-car
  comfort: decisions, edge-weight model, rules, storage choice and open questions.

## Cost and memory structure

Code facts [CODE ex-15 @ bf447619a]:

- **Compartments:** one per freight category, one per passenger class, one per mail class.
  `path_explorer_t::step` advances one compartment's current phase per game step; each phase
  quantum is iteration-limited, and a feedback controller sets the limits to spend ≈
  `path_explorer_time_midpoint` (simuconf; default 64 ms). In network mode `nwc_routesearch_t`
  broadcasts the minimum of all peers' limits, so refresh throughput is set by the slowest
  connected peer (→ [network](network.md)). A refresh request only sets a flag that is read after
  the current pass completes, so served routes are one to two passes old.
- **Phase 1 (`phase_init_prepare`):** with `allow_routing_on_foot` (pak128.Britain-Ex sets 1),
  every passenger halt with a halt within walking distance receives walking connexions
  (transport 0, waiting time 0) and a serving count of 1.
- **Phase 2 (`phase_rebuild_connexions`):** each line or lineless convoy ("linkage") contributes
  a connexion from every stop of its schedule to every other stop — a clique per linkage. Weight
  = waiting time (per origin and destination halt, `haltestelle_t::waiting_times`, mean of the
  last 32 samples) + journey time (linkage average per halt pair, else an accumulated estimate)
  + the origin halt's transfer time. One connexion per halt pair is kept (smallest journey time).
  Storage per connexion: a pool-allocated 20-byte `connexion` plus a 24-byte hash-list node, in
  37-bag tables per halt per compartment; the compartment being rebuilt holds a second copy until
  phase 3.
- **Phase 3:** swaps the new tables into the halts; a halt is a transfer halt when its serving
  count exceeds 1. With walking connexions, nearly every passenger halt in a town is a transfer
  halt, so T (transfer halts) ≈ H (working halts) for passenger compartments [UNVERIFIED on real
  maps].
- **Phase 4:** allocates the H×H working matrix (`path_element_t`, 8 B per cell: 4 B time, 2 B
  handle, 2 B padding) and transport matrix (4 B per cell) and fills them from the connexions.
- **Phase 5 (`phase_explore_paths`):** a closure over the transfer halts; per transfer halt the
  work is |inbound| × |outbound|, and rows fill as the pass proceeds, so the cost is ≈ T·H²
  relaxations per compartment — ≈ H³ for passenger compartments.
- **Phase 6:** flags halts; `haltestelle_t::step` re-routes their waiting packets on the main
  thread.
- **Savegame:** every finished cell (6 B), plus the working and transport matrices (10 B per cell)
  of a compartment saved mid-pass; one read call per field per cell on load. Every peer saves and
  reloads when a client joins (`nwc_sync_t`).
- **Queries:** `get_path_between` was 0.5% of in-game CPU samples on the gargantuan fixture
  (→ [performance](performance.md) hotspots) ≈ 6 ms CPU per real second [UNVERIFIED derivation].

| Component | Compute | Memory (C = 5 large compartments) [UNVERIFIED] |
|---|---|---|
| Phase 2 | ≈ Σ S² insertions per compartment at ~0.3–1 µs: seconds | connexion tables ≈ 46 B per connexion: ~0.2–0.5 GB in total |
| Phase 5 | ≈ H³ per passenger compartment: **dominant** | working + transport, one compartment: 0.3 / 1.2 / 4.8 GB at H = 5k / 10k / 20k |
| Finished matrices | — | 1.0 / 4.0 / 16 GB at H = 5k / 10k / 20k |
| Savegame | per-cell read calls | 6 B per finished cell (+10 B per working cell) |

Calibration: a full refresh on a very big server game takes tens of minutes
[RECOLLECTION:2026-11-04]; with the H³ model this corresponds to H ≈ 4–6k working halts in the
passenger compartments [UNVERIFIED].

## Constraints on every option

- The finished matrices are synced, saved state. Any change to route results is a gameplay and
  sync change (AGENTS.md rule 5 review); any change to the saved form is a savegame change
  (→ [savegame-versioning](savegame-versioning.md)).
- Iteration counts per quantum must be deterministic functions of the data, independent of
  timing and thread count, so that every peer stops each quantum at the same point and finishes
  each phase on the same step (→ [sync-and-determinism](sync-and-determinism.md)).
- Integer arithmetic only. x64 SSE2 is the SIMD floor; no runtime ISA dispatch exists; Win32 and
  non-x86 builds need the scalar path (→ [simd-applicability](simd-applicability.md)).
- New threads or a changed thread window require [threading](threading.md); the explorer runs
  between steps, concurrently with `sync_step` and the convoy route-finding threads.
- Y/H (→ [ex-15 yh-path-explorer](ex-15/yh-path-explorer.md)) adds through edges in phase 2 and
  a transfer substitution per transport pair in phase 5; every option must accommodate both.
- Planned comfort-aware routing (→ [comfort-routing](path-explorer-optimisation/comfort-routing.md))
  changes phase-2 edge weights and deduplication, and decides whether lever 1's layout carries an
  extra per-cell discomfort array (leaning no; design the arrays so one can be added later).

## Options ("levers")

| # | Lever | Gain [UNVERIFIED] | Results unchanged? | Y/H |
|---|---|---|---|---|
| 0 | Fix the closure bug (→ [known-bugs](known-bugs.md)) | — | no; becomes the reference for all others | none |
| 1 | Split arrays: contiguous `uint32` times apart from 16-bit handles, one allocation | phase-5 read traffic halved; finished 8 → 6 B per cell, working 12 → 10 B | yes; save byte format can stay identical | through edges need first ≠ last transport |
| 2 | Reorder phase-5 loops: per origin, all target clusters | ~2–8× less memory traffic | yes | compatible |
| 3 | SSE2 dense masked row kernel | ~2–4× compute; then bandwidth-bound | yes | delta folded into the kernel |
| 4 | Split origin rows per transfer halt over 2–4 threads | ~2–4× shorter passes until bandwidth saturates; **raises CPU per step** | yes | compatible |
| 5 | Compact connexion storage (sorted arrays per halt) | ≈ −50% connexion memory; faster `get()` and phase 4 | yes, if iteration order equals bag order | compatible |
| 6 | Phase-2 micro-optimisations (`get_average_waiting_time` copies 32 samples by value) | small | yes | none |
| 7 | Per-source Dijkstra rows instead of the closure | H·E instead of H³: ~5× at H = 5k, ~10× at ≥ 10k; scales with threads | **no** (→ engine-semantics) | needs per-transport state at coupling halts |
| 8 | Compact distance oracle (hub labels) | memory O(H·L) instead of O(H²); ~1 µs queries; smaller saves | **no**; as 7 on the same graph | needs adaptation |
| 9 | GPU per-transfer-halt update | ~0.5–1 s per compartment at H = 5k | yes, if it reproduces the CPU kernel | compatible |
| 10 | AVX2 via runtime dispatch | small once bandwidth-bound | yes | — |

Designs, proofs and rules for levers 1–4, 9 and 10 → [phase5-kernel](path-explorer-optimisation/phase5-kernel.md);
behaviour of 0, 7 and 8 → [engine-semantics](path-explorer-optimisation/engine-semantics.md).

GPU in network play: the slowest peer sets the shared iteration limits, so a GPU raises
throughput only when every connected peer has one; single-player receives the full gain, and a
server GPU also returns server CPU to the simulation.

GPU decision: the system should be open to GPU optimisation but must not require it; whether to
implement a GPU backend is decided by a later cost/benefit analysis [RECOLLECTION:2026-11-06].
Levers 1–3 are to be designed so that such a backend can be added without redesign (requirements
→ [phase5-kernel](path-explorer-optimisation/phase5-kernel.md), Lever 9).

## Combined projection (passenger compartment, H ≈ 5k) [UNVERIFIED]

| Stage | CPU per compartment pass | Full refresh cycle at the 64 ms budget |
|---|---|---|
| Present | ~100–250 s | tens of minutes |
| Levers 0–3, 6 | ~20–60 s | ~4–12 min, or the present latency at a smaller budget |
| + lever 4 | ~8–25 s wall (more CPU) | ~2–5 min |
| Lever 7, threaded | ~5–8 s | ~1–2 min |
| Lever 9 | ~0.5–1 s | seconds (single-player) |

The gains from levers 1–3 can be taken as fresher routes or as a lower
`path_explorer_time_midpoint`, which returns CPU and memory bandwidth to the simulation.
Parallelism (lever 4) shortens passes without reducing work.

Memory does not scale with these gains: levers 1 and 5 save about 20–25% in total; SIMD and GPU
save nothing. Matrix memory and savegame size remain O(H²) per compartment (16 GB of finished
matrices at H = 20k, C = 5). Above H ≈ 10–15k only a different representation (lever 8) removes
that limit. Storing only transfer-halt rows does not help passenger compartments, because T ≈ H.

## Recommended order

1. Measure (below).
2. Lever 0, with the rule-5 review; its scalar output becomes the reference.
3. Levers 1, 2, 3 and 6: results unchanged, no network penalty, and every peer gains, which
   raises the shared limit.
4. Lever 5 for memory.
5. Lever 4 only if fresher routes are worth extra CPU per step.
6. Lever 7 as the compute step for maps beyond the gargantuan fixture; it needs the behaviour
   specification in [engine-semantics](path-explorer-optimisation/engine-semantics.md).
7. Lever 8 as a research prototype for memory. GPU (lever 9): keep the design open to it, do not
   require it, and decide on implementation after a cost/benefit analysis.

Y/H proceeds on the present engine; the kernel-level requirements it must meet are in
[phase5-kernel](path-explorer-optimisation/phase5-kernel.md).

## Measurement plan

On the gargantuan fixture (→ [performance](performance.md)), per compartment: CPU per phase per
pass; H, T and E (connexions); the distribution of cluster sizes per transfer halt (it sets
lever 2's gain within ~2–8×); bytes moved per relaxation; the share of savegame size and load
time taken by path-explorer data; `find_route` decisions per step by call site; waiting packets
re-routed per phase 6. Report pass duration and total iterations, not CPU share: the controller
holds CPU share near its target.

## Open questions

- Is the CPU-first direction (levers 0–6, then 7) adopted?
- Was the path explorer's near-zero share in the measured fixture window dormancy or budget
  capping? Measure an active window (induced change activity, or a capture spanning a burst):
  pass duration, total iterations. Reconcile with the recollection that on busy servers changes
  outpace it [RECOLLECTION:2026-09-09]. The fixture has no live player activity and probably
  understates the demand on a busy server.
- What share of savegame size and load time is path-explorer data? It bounds the save and join
  benefit of levers 1 and 8.
- What are T/H and E on real large maps, per compartment?
- How large do hub labels become on dense town walking networks?
- GPU backend: the cost/benefit analysis that decides whether to implement it (inputs: measured
  CPU pass times after levers 1–3, the share of players and servers with suitable GPUs, the
  slowest-peer limit in network play, and the build and maintenance cost of a compute backend).
- If lever 7 or 8 is adopted, which companion boarding rule (stay aboard, or a stored next
  linkage) is chosen (→ [engine-semantics](path-explorer-optimisation/engine-semantics.md))?
  Comfort routing makes a boarding rule mandatory regardless of lever 7.
- Comfort-routing design questions → [comfort-routing](path-explorer-optimisation/comfort-routing.md).

## Provenance

Cost structure, invariants and lever analysis: code inspection and derivation, 2026-11-04,
[CODE ex-15 @ bf447619a]; the closure code is identical on master [CODE master @ fa0b18fba]. The
staged SIMD sequence (design SIMD-compatible → scalar verified → gated SIMD) is a user decision
[RECOLLECTION:2026-09-19]. Measured shares come from the 2026-09-10 capture recorded in
[performance](performance.md).
