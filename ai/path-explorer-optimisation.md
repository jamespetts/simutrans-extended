---
status: draft
verified: path-explorer-optimisation @ 3b18f76aa; master @ 6194cd341 (shared closure code)
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

Status: assessment and implementation plan. Levers 1–3 are implemented on
`path-explorer-optimisation` (2026-11-07): flat arrays, the dense loop reorder, and the SSE2/NEON
dense row kernels. SSE2 is execution-verified; NEON is compile-verified by the native macOS CI.
The dense path is a clear win on the gargantuan fixture (-19% against the sparse loop), roughly
neutral on the intermediates and 8% slower on the smallest; an optional dense/sparse selector
would remove that regression. Figures marked [UNVERIFIED] are derivations from code structure,
not measurements. Measured cost figures: [EXECUTION-VERIFIED:2026-11-06, ex-15 @ ad1815c, Profile
(server), month-boundary.sve and bb-10-sep-2023.sve]; lever-1 result:
[EXECUTION-VERIFIED:2026-11-07, `path-explorer-optimisation` @ e5fdd5f37]; lever-2 scalar result:
[EXECUTION-VERIFIED:2026-11-07, `path-explorer-optimisation` @ 63e5d2ac1]; lever-3 SSE2 result:
[EXECUTION-VERIFIED:2026-11-07, `path-explorer-optimisation` @ 3b18f76aa].

## Priorities and sequencing

User decisions, 2026-11-06 [RECOLLECTION:2026-11-06]:

- Memory savings justify changes on their own, because memory use can be a problem.
- Small performance improvements are worthwhile when the coding cost is modest.
- CPU cores are underused during normal gameplay even on large maps, and route staleness is a
  real in-game problem. Adding threads and CPU load to shorten rebuild cycles is worthwhile;
  this is sync-critical (→ [threading](threading.md), [sync-and-determinism](sync-and-determinism.md)).

Recommended internal sequencing for the single 15.x release: finish the shared foundation
before integrating Y/H or comfort into the route tables — final cell layout (contiguous arrays,
one combined comfort cost), comparison-loop rewrite/SSE2/NEON, and the threading/iteration model.
The runtime, data and balance parts of Y/H and comfort are independent of that foundation and
can proceed in parallel. This is sequencing within the
single 15.x release (no staged releases); GPU remains optional.

Sub-documents (read-when):

- [phase5-kernel](path-explorer-optimisation/phase5-kernel.md) — read when implementing or
  reviewing any change to `phase_explore_paths` (data layout, loop order, SIMD, extra threads,
  GPU): the order-independence invariant, the kernel designs, budget/parity, savegame and Y/H
  rules, and the verification oracle.
- [engine-semantics](path-explorer-optimisation/engine-semantics.md) — read before any change
  that alters route results (per-source Dijkstra rows, hub labels, connexion deduplication): the
  present route semantics and worked examples of how each alternative changes passenger
  behaviour.
- [per-packet-routing](path-explorer-optimisation/per-packet-routing.md) — read before proposing
  route search at decision time instead of a precomputed database: the rejected assessment and
  its quantitative reasons.
- [comfort-routing](path-explorer-optimisation/comfort-routing.md) — read when designing or
  implementing comfort-aware routing (vehicle, stop and walking comfort in route choice and
  tolerance), the removal of the speed bonus and of comfort weighting from fares, or private-car
  comfort: decisions, edge-weight model, rules, storage choice and open questions.

## Cost and memory structure

Code facts [CODE ex-15 @ 33958208a]:

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
  work is |inbound| × |outbound|, and rows fill as the pass proceeds. Phase 5 is the dominant
  phase: 86% of a full pass at H ≈ 1.8k and 99.6% at H ≈ 5.4k. The true target-member visits at
  H ≈ 5.4k are ~43–44 B per passenger class. The diagnostic is a `uint64` (`total_iterations`).
  The cost is ~1.8–2.1 ns per true target visit across the measured fixtures.
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
| Phase 5 | **dominant** (86% / 99.6% of a full pass at H ≈ 1.8k / 5.4k). True target visits grow ~H^3.65 over that range (0.85 B → 44 B per passenger class); time is ~1.8–2.1 ns per visit. H³ understates the growth by ~2×. | working + transport, one compartment: 0.3 / 1.2 / 4.8 GB at H = 5k / 10k / 20k |
| Finished matrices | — | 1.0 / 4.0 / 16 GB at H = 5k / 10k / 20k |
| Savegame | per-cell read calls | 6 B per finished cell (+10 B per working cell) |

Calibration: the gargantuan fixture has H ≈ 5,375 working halts in the passenger
compartments; a full unbudgeted refresh takes 394.7 s CPU on the measurement machine, of
which 393.0 s is phase 5. The recollection of tens of minutes per refresh
[RECOLLECTION:2026-11-04] is consistent with that fixture on a server 2–4× slower, or with
the budgeted wall-clock pass (64 ms of explorer work per simulation step). It is not the
unbudgeted CPU pass on this machine. The true target visits grow ~H^3.65 from H ≈ 1.8k to
5.4k, so H³ is an underestimate over this range, not a safe upper bound.

### Measured cost (2026-11-06)

[EXECUTION-VERIFIED:2026-11-06, ex-15 @ ad1815c, Profile (server), `-debug 1`; full
unbudgeted refresh with per-compartment timers and 64-bit target-visit counters. Fixtures:
`month-boundary.sve` (2/2047), `bb6-apr-2010.sve`, `bb6-may-2010-standardised.sve` and
`bb-10-sep-2023.sve`.]

| Fixture | Passenger H | Passenger T | T/H | Passenger E | Phase-5 per class | True target visits per class | ns/visit |
|---|---:|---:|---:|---:|---:|---:|---:|
| month-boundary.sve | 1,828 | 1,610 | 88.1% | ~48k | 1.5–1.8 s | 0.73–0.85 B | ~2.1 |
| bb6-apr-2010.sve | 2,467 | 2,280 | 92.4% | 36.3k | 5.17–5.42 s | 2.95 B | 1.84 |
| bb6-may-2010-standardised.sve | 2,467 | 2,280 | 92.4% | 36.3k | 6.27–7.08 s | 3.55–3.67 B | 1.9–1.97 |
| bb-10-sep-2023.sve | 5,375 | 4,665 | 86.8% | ~109k | 77.6–79.4 s | 42.6–44.0 B | 1.77–1.85 |

- **Per-visit cost is stable:** 1.84–2.13 ns across all four fixtures; the 231 MB matrix at
  H ≈ 5.4k does not change the per-visit cost materially.
- **Visit growth:** 0.85 B → 2.95 B → 44 B per passenger class over H 1.8k → 2.5k → 5.4k.
  From 1.8k to 5.4k this is ×52, an effective exponent of ≈3.65; H³ (×25.5) understates it
  by about 2×. The fraction of T·H² actually visited rises from 15.7% to 21.2% to 32.6%.
- **Topology matters independently of H/T/E.** `bb6-apr` and `bb6-may` have identical H, T
  and E but 2.95 B vs 3.64 B visits and 5.2 vs 6.5 s per class. In-game halt/line/convoy
  counts are not sufficient predictors; cluster and transport structure matter.
- Cluster counters (details in [phase5-kernel](path-explorer-optimisation/phase5-kernel.md)):
  2.20–2.44 clusters per transfer, average 790–1,668 members; one row fetch per 544–1,389
  target visits, so row re-fetching is under 0.1% of the work.
- Phase 5 is 86.0% of the small-fixture pass and 99.6% of the gargantuan pass; phase 2 adds
  0.47 s and phase 4 0.61 s on the gargantuan fixture, and the remaining phases are small.
- Savegame: at H ≈ 1.8k the finished matrices are 36.85 MiB of a 132 MiB save (27.9%).
  The on-disk format is already 6 B per finished cell, so lever 1 does not change save bytes;
  only a different representation (lever 8) or omitting the data does.
- Load time at H ≈ 1.8k: 19 s with path data vs 26 s without (the latter doing a full
  refresh; `-debug 3` logging included). The matrix read is roughly 2.5 s, so retaining the
  data is worth its size at this H. At H ≈ 5.4k a no-path load must pay the 394.7 s refresh.

Budgeted capture on the gargantuan fixture [EXECUTION-VERIFIED:2026-11-06, 2,400 s
server-paced window, `reroute_check_interval_steps = 512` to induce requests]: 96.1% of
steps busy, 748.2 s of explorer CPU (31.2% of wall), 29 completions and two full cycles;
218–238 s per passenger compartment, 18–20 min per five-class cycle. Without the induced
requests it was idle for the first 296 steps after load, so it is dormant between requested
passes and budget-capped during one; the default 8192-step interval at ~5.6 steps/s is about
24 min, close to one pass, so the duty cycle depends on server speed.

Consequence for the levers: the true target-member loop is the cost (44 B visits per class
at H ≈ 5.4k, ~1.8 ns each), so **lever 3 (SIMD over the contiguous target lists) is the
primary CPU lever**. Lever 2 (reorder) only removes row re-fetches, which
are under 0.1% of the visits; its 2–8× estimate is not supported and it should be treated as a
locality refinement, not the main gain. Lever 1 remains useful for memory and for feeding SIMD
with contiguous arrays, but not for save size.

## Constraints on every option

- The finished matrices are synced, saved state. Any change to route results is a gameplay and
  sync change (AGENTS.md rule 5 review); any change to the saved form is a savegame change
  (→ [savegame-versioning](savegame-versioning.md)).
- Iteration counts per quantum must be deterministic functions of the data, independent of
  timing and thread count, so that every peer stops each quantum at the same point and finishes
  each phase on the same step (→ [sync-and-determinism](sync-and-determinism.md)).
- Integer arithmetic only. x64 SSE2 and AArch64 NEON are the two SIMD targets, implemented
  together (user decision 2026-11-07); no runtime ISA dispatch exists. Win32 x86 and other targets
  use the scalar path (→ [simd-applicability](simd-applicability.md)).
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
| 1 | Split arrays: contiguous `uint32` times apart from 16-bit handles, one allocation | phase-5 read traffic halved; finished 8 → 6 B per cell, working 12 → 10 B | yes; save byte format can stay identical | through edges need first ≠ last transport |
| 2 | Reorder phase-5 loops: per origin, all target clusters | in scalar, 2.2-3.9x slower in phase 5 than the sparse loop (visits every halt per origin); the staging form for lever 3, not a gain on its own [EXECUTION-VERIFIED:2026-11-07] | yes | compatible |
| 3 | SSE2/NEON dense masked row kernel | measured 1.77-2.59x against the dense scalar; roughly break-even with the sparse loop on small maps and -19% on the run total at H ≈ 5.4k [EXECUTION-VERIFIED:2026-11-07] | yes | delta folded into the kernel |
| 4 | Split origin rows per transfer halt over 2–4 threads | ~2–4× shorter passes until bandwidth saturates; **raises CPU per step**. Approved in principle: cores are underused and route staleness is real [RECOLLECTION:2026-11-06] | yes | compatible |
| 5 | Compact connexion storage (sorted arrays per halt) | ≈ −50% connexion memory; faster `get()` and phase 4 | yes, if iteration order equals bag order | compatible |
| 6 | Phase-2 micro-optimisations (`get_average_waiting_time` copies 32 samples by value) | small | yes | none |
| 7 | Per-source Dijkstra rows instead of the closure | H·E instead of H³: ~5× at H = 5k, ~10× at ≥ 10k; scales with threads | **no** (→ engine-semantics) | needs per-transport state at coupling halts |
| 8 | Compact distance oracle (hub labels) | memory O(H·L) instead of O(H²); ~1 µs queries; smaller saves | **no**; as 7 on the same graph | needs adaptation |
| 9 | GPU per-transfer-halt update | ~0.5–1 s per compartment at H = 5k with sparse cluster-pair work; ~1.5–3 s with a dense H² update per transfer [UNVERIFIED] | yes, if it reproduces the CPU kernel | compatible |
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

## Combined projection (passenger compartment, H ≈ 5k)

Present is measured (2026-11-06); later stages are [UNVERIFIED] projections from the lever
estimates. Lever 1 is now implemented: it reduced phase-5 time by 40-56% on the four fixtures
(1.6-1.7× on the pass total including load), so the "Present" row is the pre-lever-1 figure; the
per-fixture numbers are in [phase5-kernel](path-explorer-optimisation/phase5-kernel.md), Lever 1.

| Stage | CPU per compartment pass | Full refresh cycle at the 64 ms budget |
|---|---|---|
| Present before lever 1 (measured, 2026-11-06) | 78 s at H ≈ 5.4k | 18–20 min wall (394.7 s CPU) |
| Lever 1 implemented (measured, 2026-11-07) | 40-56% less phase-5 time; 1.6-1.7× on the run total | proportionally shorter, or a lower budget |
| Levers 1–3, SSE2 implemented (measured, 2026-11-07) | ~37 s at H ≈ 5.4k (214.77 s run total) | proportionally shorter, or a lower budget |
| Levers 1–3, 6 | ~20–40 s if the assumed 2–4× materialises | ~5–10 min, or the present latency at a smaller budget |
| + lever 4 | ~8–25 s wall (more CPU) | ~2–5 min |
| Lever 7, threaded | ~5–8 s | ~1–2 min |
| Lever 9 | ~0.5–1 s (sparse cluster-pair kernel) | seconds (single-player) |

The gains from levers 1–3 can be taken as fresher routes or as a lower
`path_explorer_time_midpoint`, which returns CPU and memory bandwidth to the simulation.
Parallelism (lever 4) shortens passes without reducing work.

Memory does not scale with these gains: levers 1 and 5 save about 20–25% in total; SIMD and GPU
save nothing. Matrix memory and savegame size remain O(H²) per compartment (16 GB of finished
matrices at H = 20k, C = 5). Above H ≈ 10–15k only a different representation (lever 8) removes
that limit. Storing only transfer-halt rows does not help passenger compartments, because T ≈ H.

## Recommended order

1. Measurement: done 2026-11-06 (above).
2. Levers 1–3 are implemented: flat arrays, the dense loop reorder and the SSE2/NEON kernels.
   The dense SIMD is a clear win at H ≈ 5.4k (-19% against sparse) and roughly neutral on the
   smaller fixtures (8% slower at H ≈ 1.8k). Decide whether to add the dense/sparse selector
   before lever 4; otherwise proceed to lever 4 (threads) or lever 5 (memory).
3. Lever 4 after the layout/SIMD kernel: extra threads shorten the rebuild cycle and are
   approved in principle; settle the deterministic iteration/stop-point model first
   (→ [threading](threading.md), [sync-and-determinism](sync-and-determinism.md)).
4. Lever 5 for memory; it can run alongside the kernel work.
5. Lever 7 as the compute step for maps beyond the gargantuan fixture; it needs the behaviour
   specification in [engine-semantics](path-explorer-optimisation/engine-semantics.md).
6. Lever 8 as a research prototype for memory. GPU (lever 9): keep the design open to it, do not
   require it, and decide on implementation after a cost/benefit analysis.

Y/H stays on the present engine family (not levers 7/8), but its route-table integration
should follow the layout/loop foundation; its runtime co-requirements can proceed in
parallel. The kernel-level requirements are in
[phase5-kernel](path-explorer-optimisation/phase5-kernel.md).

## Measurement plan

Done 2026-11-06 (→ [performance](performance.md) for the suite): per-compartment CPU per
phase, H, T and E, full-pass duration, and the savegame/load share on both fixtures. Still
open: the distribution of cluster sizes per transfer halt (it sets lever 2's gain), bytes
moved per relaxation or origin-row fetch, `find_route` decisions per step by call site, and
waiting packets re-routed per phase 6. Report pass duration and total iterations, not CPU
share: the controller holds CPU share near its target.

## Open questions

- Is the CPU-first direction (levers 1–6, then 7) adopted?
- Dense/sparse selector: whether to add it, and at what threshold. The measured crossover is about
  `H / outbound_member_count = 2.6`; without a selector the dense SIMD is roughly break-even on the
  intermediates, 8% slower at H ≈ 1.8k and 19% faster at H ≈ 5.4k (2026-11-07).
- NEON execution verification: the NEON kernel compiles on the native macOS CI but no NEON
  hardware is available to run the oracle. Whether to add a CI execution run (or accept
  compile-only) is open.
- Was the path explorer's near-zero share in the measured fixture window dormancy or budget
  capping? Both: it is dormant between periodic requests and budget-capped during a pass. The
  gargantuan capture was idle for at least the first 296 steps after load, then busy 96.1% of
  steps with ~60 ms of work per step and an 18–20 min cycle. Code requests
  `refresh_all_categories(false)` every `reroute_check_interval_steps` (default 8192) and
  monthly, so it is not change-driven only.
- Savegame and load share: measured. At H ≈ 1.8k the finished matrices are 36.85 MiB of a
  132 MiB save (27.9%); load is 19 s with data vs 26 s without. Lever 1 does not change the
  save byte format; the join benefit of a smaller representation belongs to lever 8.
- T/H and E: measured. T/H is 88.1% at H ≈ 1.8k, 92.4% on the intermediate `bb6` fixtures
  and 86.8% at H ≈ 5.4k; E is ~48k, ~36k and ~109k per passenger compartment respectively.
- How large do hub labels become on dense town walking networks?
- GPU backend: the cost/benefit analysis that decides whether to implement it (inputs: measured
  CPU pass times after levers 1–3, the share of players and servers with suitable GPUs, the
  slowest-peer limit in network play, and the build and maintenance cost of a compute backend).
  The GPU backend must process the same ~44 B target visits per passenger class at H ≈ 5.4k,
  so it must keep the working matrix resident and use the contiguous target clusters; a
  sparse cluster-pair kernel does far less work than a dense H² update per transfer halt.
- If lever 7 or 8 is adopted, which companion boarding rule (stay aboard, or a stored next
  linkage) is chosen (→ [engine-semantics](path-explorer-optimisation/engine-semantics.md))?
  Comfort routing makes a boarding rule mandatory regardless of lever 7.
- Comfort-routing design questions → [comfort-routing](path-explorer-optimisation/comfort-routing.md).

## Provenance

Cost structure, invariants and original lever analysis: code inspection and derivation,
2026-11-04; measured 2026-11-06 (above); lever 1 implemented and measured 2026-11-07 on
`path-explorer-optimisation` (e5fdd5f37), code current at `path-explorer-optimisation` @
011f9a23a and master @ 6194cd341 [CODE]. The staged SIMD sequence (design SIMD-compatible →
scalar verified → gated SIMD) is a user decision [RECOLLECTION:2026-09-19]; SSE2 and NEON are
implemented together [RECOLLECTION:2026-11-07]. Measured shares come from the 2026-09-10 capture
recorded in [performance](performance.md).
