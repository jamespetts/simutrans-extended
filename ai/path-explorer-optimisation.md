---
status: draft
verified: ex-15 @ f803ef8ac (merged from path-explorer-optimisation @ 2988b9597; service-frequency cache fix; sorted connexion arrays); master @ 6194cd341 (shared closure code)
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

Status: assessment and implementation plan. The current implementation has flat matrix arrays,
the dense per-origin loop, SSE2/NEON kernels and the dedicated phase-5 thread pool. SSE2 is
execution-verified; NEON is compile-verified by the native macOS CI. The dense path is a clear
win on the gargantuan fixture (-19% against the sparse loop), roughly neutral on the
intermediates and 8% slower on the smallest before the pool. The pool measured run totals 2.02x
faster on the gargantuan fixture and 1.19x on month-boundary, with identical matrices and
iteration totals. These ratios include savegame load; phase 5 alone is ≈ 2.2–2.4x faster on the
gargantuan fixture (load of 15–28 s subtracted [UNVERIFIED derivation]), and the month-boundary
ratio is mostly load time. Against the original sparse loop (393 s of phase 5 for the five
passenger classes), phase 5 is ≈ 4.3–5.0x faster with three pool threads. The
pool's mid-pass resume and loopback network behaviour are verified. The branch was merged into
`ex-15` on 2026-11-07 (merge `ad753f5c6`), the TEST harness has been removed, and the revision-6
mid-pass restart path is execution-verified: the finished matrices match the revision-7 resume,
with `total_iterations` higher only by the repeated current inbound cluster. The service-frequency
cache key truncation was fixed on `ex-15` (`711656d4b`); with the full 16-bit halt ID, a full
refresh and a resumed mid-pass pass produce identical finished matrices, resolving the mid-pass
cross-class difference. The work stays on `ex-15` until 15.x itself is merged into `master`; it is
not ported to `master` separately (user decision 2026-11-07). Remaining work: the optional
server-path ETW capture, the Y/H hooks and the later levers. Lever 5 (sorted connexion arrays) is
implemented and measured. The dense/sparse selector is held off (2026-11-07), not rejected.
Figures marked [UNVERIFIED] are derivations from code structure, not measurements. Current
measured cost: [EXECUTION-VERIFIED:2026-11-07, `ex-15` @ f803ef8ac, Profile (server) x64; oracle
harness].

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
The layout, the SIMD comparison loop, the phase-5 thread pool and their verification are in
place on `ex-15`; the Y/H hooks are the next path-explorer implementation work. The runtime, data
and balance parts of Y/H and comfort are independent of that foundation and can proceed in
parallel. This is sequencing
within the single 15.x release (no staged releases); GPU remains optional.

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
- **Phase 4:** allocates the flat working arrays (`working_times` 4 B/cell, `working_next_transfers`
  2 B/cell, `working_first_transports` and `working_last_transports` 2 B/cell each) and fills them
  from the connexions. The finished arrays are 4 + 2 B/cell.
- **Phase 5 (`phase_explore_paths`):** a closure over the transfer halts. Conceptually the work per
  transfer halt is |inbound| × |outbound|; the implemented dense row visits every halt per origin
  member (|inbound| × H per transfer) with `UINT32_MAX` masks for non-targets. Phase 5 dominates a
  full pass. The dense SIMD kernel is the production path; the sparse fallback's measured share
  was 86% of a full pass at H ≈ 1.8k and 99.6% at H ≈ 5.4k, at ~1.8–2.1 ns per true target visit.
  The diagnostic is a `uint64` (`total_iterations`).
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
| Phase 5 | **dominant** in both loop shapes. The sparse fallback's measured share was 86% / 99.6% of a full pass at H ≈ 1.8k / 5.4k; its true target visits grow ~H^3.65 over that range (0.85 B → 44 B per passenger class) at ~1.8–2.1 ns per visit. | working + transport, one compartment: 0.25 / 1.0 / 4.0 GB at H = 5k / 10k / 20k |
| Finished matrices | — | 0.75 / 3.0 / 12 GB at H = 5k / 10k / 20k |
| Savegame | per-cell read calls | 6 B per finished cell (+10 B per working cell) |

Calibration: the gargantuan fixture has H ≈ 5,375 working halts in the passenger
compartments. The current dense SIMD run total there is 214.77 s including load; the sparse
fallback's phase-5 cost was 393.0 s of a 394.7 s full pass (→
[phase5-kernel](path-explorer-optimisation/phase5-kernel.md), sparse-loop cost). The recollection
of tens of minutes per refresh [RECOLLECTION:2026-11-04] is consistent with that fixture on a
server 2–4× slower, or with the budgeted wall-clock pass (64 ms of explorer work per simulation
step). The sparse loop's true target visits grow ~H^3.65 from H ≈ 1.8k to 5.4k, so H³ is an
underestimate over that range, not a safe upper bound.

### Sparse-loop cost and topology (optional fallback)

The sparse loop is the planned selector fallback. Its measured cost and the topology that
determines when it is cheaper: [EXECUTION-VERIFIED:2026-11-06, ex-15 @ ad1815c, Profile (server),
`-debug 1`; full unbudgeted refresh with per-compartment timers and 64-bit target-visit counters.
Fixtures: `month-boundary.sve` (2/2047), `bb6-apr-2010.sve`, `bb6-may-2010-standardised.sve` and
`bb-10-sep-2023.sve`.]

| Fixture | Passenger H | Passenger T | T/H | Passenger E | Sparse phase-5 per class | True target visits per class | ns/visit |
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
- On the sparse loop, phase 5 is 86.0% of the small-fixture pass and 99.6% of the gargantuan
  pass; phase 2 adds 0.47 s and phase 4 0.61 s on the gargantuan fixture, and the remaining
  phases are small.
- Savegame: at H ≈ 1.8k the finished matrices are 36.85 MiB of a 132 MiB save (27.9%).
  The on-disk format is already 6 B per finished cell, so lever 1 does not change save bytes;
  only a different representation (lever 8) or omitting the data does.
- Load time at H ≈ 1.8k: 19 s with path data vs 26 s without (the latter doing a full
  refresh; `-debug 3` logging included). The matrix read is roughly 2.5 s, so retaining the
  data is worth its size at this H. At H ≈ 5.4k a no-path load must pay the current pass
  (214.77 s run total).

Budgeted capture on the gargantuan fixture with the sparse loop [EXECUTION-VERIFIED:2026-11-06,
2,400 s server-paced window, `reroute_check_interval_steps = 512` to induce requests]: 96.1% of
steps busy, 748.2 s of explorer CPU (31.2% of wall), 29 completions and two full cycles. Without
the induced requests it was idle for the first 296 steps after load, so it is dormant between
requested passes and budget-capped during one; the default 8192-step interval at ~5.6 steps/s is
about 24 min, so the duty cycle depends on server speed and the pass duration.

The current dense SIMD kernel is the production path: it is a clear win on the gargantuan fixture,
roughly neutral on the intermediates and slightly slower on the smallest, where the sparse loop is
the planned selector fallback. The scalar cell function remains the oracle. SIMD does not change
memory; lever 5 (compact connexion storage) is implemented, and the remaining memory option is
lever 8 (a different representation).

## Constraints on every option

- The finished matrices are synced, saved state. Any change to route results is a gameplay and
  sync change (AGENTS.md rule 5 review); any change to the saved form is a savegame change
  (→ [savegame-versioning](savegame-versioning.md)).
- Iteration counts per quantum must be deterministic functions of the data, independent of
  timing and thread count, so that every peer stops each quantum at the same point and finishes
  each phase on the same step (→ [sync-and-determinism](sync-and-determinism.md)).
- Integer arithmetic only. x64 SSE2 and AArch64 NEON are the two SIMD targets, implemented
  together [RECOLLECTION:2026-11-07]; no runtime ISA dispatch exists. Win32 x86 and other targets
  use the scalar path (→ [simd-applicability](simd-applicability.md)).
- New threads or a changed thread window require [threading](threading.md); the explorer runs
  between steps, concurrently with `sync_step` and the convoy route-finding threads.
- Y/H (→ [ex-15 yh-path-explorer](ex-15/yh-path-explorer.md)) adds through edges in phase 2 and
  a transfer substitution per transport pair in phase 5; every option must accommodate both.
- Planned comfort-aware routing (→ [comfort-routing](path-explorer-optimisation/comfort-routing.md))
  changes phase-2 edge weights and deduplication, and decides whether lever 1's layout carries an
  extra per-cell discomfort array (leaning no; design the arrays so one can be added later).

## Options ("levers")

| # | Lever | Gain (measured where tagged EXECUTION-VERIFIED; otherwise [UNVERIFIED] estimate) | Results unchanged? | Y/H |
|---|---|---|---|---|
| 1 | Split arrays: contiguous `uint32` times apart from 16-bit handles, one allocation | phase-5 read traffic halved; finished 8 → 6 B per cell, working 12 → 10 B | yes; save byte format can stay identical | through edges need first ≠ last transport |
| 2 | Reorder phase-5 loops: per origin, all target clusters | Reordering the sparse loop (which visits only registered target halts) is worth under 0.1%: there are 2.2–2.4 large inbound clusters per transfer halt and one row fetch per 544–1,389 target visits [EXECUTION-VERIFIED:2026-11-06; reordered sparse loop not built]. The implemented per-origin loop is instead the dense row form for lever 3: without SIMD it is 2.2–3.9x slower in phase 5 than the sparse loop, because it visits every halt per origin [EXECUTION-VERIFIED:2026-11-07] | yes | compatible |
| 3 | SSE2/NEON dense masked row kernel | measured 1.77-2.59x against the dense scalar; roughly break-even with the sparse loop on small maps and -19% on the run total at H ≈ 5.4k [EXECUTION-VERIFIED:2026-11-07] | yes | delta folded into the kernel |
| 4 | Split origin rows per transfer halt over the phase-5 thread pool | Implemented 2026-11-07: `parallel_operations` explorer threads (lead + helpers); run totals (including load) measured 2.02x faster on the gargantuan fixture and 1.19x on month-boundary; phase 5 alone ≈ 2.2–2.4x on the gargantuan fixture [UNVERIFIED derivation]; identical results; **raises CPU per step**. Approved in principle: cores are underused and route staleness is real [RECOLLECTION:2026-11-06] | yes | compatible |
| 5 | Compact connexion storage (sorted arrays per halt) | Implemented 2026-11-07: 44 → 20 B per connexion plus a 16-byte array header instead of a 37-bag table; on bb-10-sep-2023 (583,164 connexions, 31,508 non-empty maps) the connexion structure falls from ≈130 MB to ≈14 MB and peak working set from 4.12 to 3.98 GB; phase 2 unchanged, phase 4 34 → 24 ms, phase 5 ≈2–5% slower in controlled runs (kernel unchanged; allocation/code layout) [EXECUTION-VERIFIED:2026-11-07] | yes; matrices and iteration totals identical, save entry order changes | compatible |
| 6 | Phase-2 micro-optimisations (`get_average_waiting_time` copies 32 samples by value) | small | yes | none |
| 7 | Per-source Dijkstra rows instead of the closure | H·E instead of H³: ~5× at H = 5k, ~10× at ≥ 10k; scales with threads | **no** (→ engine-semantics) | needs per-transport state at coupling halts |
| 8 | Compact distance oracle (hub labels) | memory O(H·L) instead of O(H²); ~1 µs queries; smaller saves | **no**; as 7 on the same graph | needs adaptation |
| 9 | GPU per-transfer-halt update | ~0.5–1 s per compartment at H = 5k with sparse cluster-pair work; ~1.5–3 s with a dense H² update per transfer [UNVERIFIED] | yes, if it reproduces the CPU kernel | compatible |
| 10 | AVX2 via runtime dispatch | small once bandwidth-bound | yes | — |

Designs, proofs and rules for levers 1–5, 9 and 10 → [phase5-kernel](path-explorer-optimisation/phase5-kernel.md);
behaviour of 0, 7 and 8 → [engine-semantics](path-explorer-optimisation/engine-semantics.md).

GPU in network play: the slowest peer sets the shared iteration limits, so a GPU raises
throughput only when every connected peer has one; single-player receives the full gain, and a
server GPU also returns server CPU to the simulation.

GPU decision: the system should be open to GPU optimisation but must not require it; whether to
implement a GPU backend is decided by a later cost/benefit analysis [RECOLLECTION:2026-11-06].
Levers 1–3 are to be designed so that such a backend can be added without redesign (requirements
→ [phase5-kernel](path-explorer-optimisation/phase5-kernel.md), Lever 9).

## Combined projection (passenger compartment, H ≈ 5k)

Current is measured (dense SSE2 plus the phase-5 pool); later stages are [UNVERIFIED]
projections from the lever estimates.

| Stage | CPU per compartment pass | Full refresh cycle at the 64 ms budget |
|---|---|---|
| Current (dense SSE2 + pool, measured) | ~16–18 s per passenger class at H ≈ 5.4k with three pool threads (106.47 s run total including load, against 214.77 s single-thread; original sparse loop 77.6–79.4 s per class) | proportionally shorter, or a lower budget |
| + optional dense/sparse selector | removes the small-map regression; no change on large maps | — |
| Lever 7, threaded | ~5–8 s | ~1–2 min |
| Lever 9 | ~0.5–1 s (sparse cluster-pair kernel) | seconds (single-player) |

The current gains can be taken as fresher routes or as a lower
`path_explorer_time_midpoint`, which returns CPU and memory bandwidth to the simulation.
The phase-5 pool shortens passes without reducing work; it is implemented.

Memory does not scale with these gains: levers 1 and 5 reduce the non-matrix structures (lever 5
measured ≈140 MB peak and ≈120 MB of connexion structure on the gargantuan fixture), while SIMD and
GPU save nothing. Matrix memory and savegame size remain O(H²) per compartment (12 GB of finished
matrices at H = 20k, C = 5). Above H ≈ 10–15k only a different representation (lever 8) removes
that limit. Storing only transfer-halt rows does not help passenger compartments, because T ≈ H.

## Recommended order

1. The dense/sparse selector is held off for now (2026-11-07), not rejected; revisit only if
   the small-map regression becomes a real problem.
2. Lever 4: implemented 2026-11-07 with `get_parallel_operations()` explorer threads (lead plus
   helpers). The mid-pass resume and loopback network behaviour are verified; the branch is
   merged into `ex-15`, the TEST harness is removed and the revision-6 restart path is verified.
   The optional server-path ETW capture and the main-thread frame-time measurement remain.
3. Lever 5 for memory: implemented 2026-11-07 (sorted connexion arrays); matrices verified and
   memory/timing measured.
4. Lever 7 as the compute step for maps beyond the gargantuan fixture; it needs the behaviour
   specification in [engine-semantics](path-explorer-optimisation/engine-semantics.md).
5. Lever 8 as a research prototype for memory. GPU (lever 9): keep the design open to it, do not
   require it, and decide on implementation after a cost/benefit analysis.

Y/H stays on the present engine family (not levers 7/8), but its route-table integration
should follow the layout/loop foundation; its runtime co-requirements can proceed in
parallel. The kernel-level requirements are in
[phase5-kernel](path-explorer-optimisation/phase5-kernel.md).

## Measurement plan

Open measurements: the distribution of cluster sizes and the per-batch wake-up cost (they set
the pool's efficiency and any future dense/sparse selector threshold), bytes moved per
relaxation or origin-row fetch, `find_route` decisions per step by call site, and waiting packets
re-routed per phase 6. Report pass duration and total iterations, not CPU share: the controller
holds CPU share near its target.

## Open questions

- Is lever 6 adopted, and is lever 7 the compute step for maps beyond the gargantuan fixture?
- Dense/sparse selector: held off for now (2026-11-07), not rejected. The measured crossover is
  about `H / outbound_member_count = 2.6`; the dense SIMD is roughly break-even on the
  intermediates, 8% slower at H ≈ 1.8k and 19% faster at H ≈ 5.4k. Revisit if the small-map
  regression matters.
- Lever 4 pool: the month-boundary, gargantuan and mid-pass oracle checks pass, and the
  loopback network smoke run passed with 1,049 per-step route hashes identical. The revision-6
  mid-pass restart path is execution-verified: every finished matrix hash matches the revision-7
  resume, and `total_iterations` for passenger class 0 is 2,266,957,180 against 2,266,778,036, a
  difference of 179,144 from repeating the current inbound cluster. The optional server-path ETW
  capture remains. The pool size is `get_parallel_operations()` threads in total
  (one lead plus `get_parallel_operations() - 1` helpers); at `threads = 4`, three threads. The
  A/B measured run totals (including load) 2.02x faster on the gargantuan fixture and 1.19x on
  month-boundary; phase 5 alone ≈ 2.2–2.4x on the gargantuan fixture. On the 24-thread
  test machine, `threads = 6` (5 pool threads: lead + 4 helpers) measured 93.87 s and 15.60 s
  (1.13x and 1.02x against t4), with identical results; the t6 network smoke also passed with
  1,049 per-step route hashes identical. A four-core server at `threads = 6` would oversubscribe,
  so the t4 pool remains the four-core recommendation.
  [EXECUTION-VERIFIED:2026-11-07, `ex-15` @ ad753f5c6 (merged from
  `path-explorer-optimisation` @ 2988b9597); oracle harness and network smoke.]
- NEON execution verification: the NEON kernel compiles on the native macOS CI but no NEON
  hardware is available to run the oracle. Whether to add a CI execution run (or accept
  compile-only) is open.
- How large do hub labels become on dense town walking networks?
- GPU backend: the cost/benefit analysis that decides whether to implement it (inputs: current
  CPU pass times, the share of players and servers with suitable GPUs, the slowest-peer limit in
  network play, and the build and maintenance cost of a compute backend). A GPU backend must keep
  the working matrix resident and process the same cells as the current dense kernel; a sparse
  cluster-pair kernel does far less work than a dense H² update per transfer halt.
- If lever 7 or 8 is adopted, which companion boarding rule (stay aboard, or a stored next
  linkage) is chosen (→ [engine-semantics](path-explorer-optimisation/engine-semantics.md))?
  Comfort routing makes a boarding rule mandatory regardless of lever 7.
- Comfort-routing design questions → [comfort-routing](path-explorer-optimisation/comfort-routing.md).

## Provenance

Cost structure, invariants and lever analysis: code inspection and derivation, 2026-11-04;
sparse-loop measurements 2026-11-06; current layout and SIMD implementation and measurements
2026-11-07; phase-5 pool implementation, oracle equivalence and A/B 2026-11-07; TEST-harness
removal, revision-6 restart verification, service-frequency cache fix and merge into `ex-15`
2026-11-07; sorted connexion arrays implemented and measured 2026-11-07; code current at `ex-15`
@ f803ef8ac (merge `ad753f5c6`; branch merged from `path-explorer-optimisation` @ 2988b9597;
service-frequency cache fix `711656d4b`; sorted connexion arrays `f803ef8ac`), and master @
6194cd341 [CODE].
The staged SIMD sequence (design SIMD-compatible → scalar verified → gated SIMD) is a user
decision [RECOLLECTION:2026-09-19]; SSE2 and NEON are implemented together
[RECOLLECTION:2026-11-07]. Measured shares come from the 2026-09-10 capture recorded
in [performance](performance.md).
