---
status: draft
verified: ex-15 @ c1e914343 plus working-tree comfort performance changes (2026-11-09)
---
# Path explorer optimisation

**Covers:** the routing database and its current engine: `path_explorer.{cc,h}` and its
refresh pipeline (halt-list preparation, connexion rebuild, eligible-halt selection, graph
build, routing search, waiting-packet reroute), the full connexion-rebuild candidates, the
expanded `(halt, arriving transport)` state graph, the per-origin Dijkstra, the
routing-search worker pool, matrix storage, the revision-9 savegame, and the consumers
`haltestelle_t::find_route` / `compartment_t::get_path_between`. Route semantics →
[engine-semantics](path-explorer-optimisation/engine-semantics.md); kernel, state graph and
verification → [routing-search-kernel](path-explorer-optimisation/routing-search-kernel.md);
the assessed and rejected hub-label oracle → [hub-labels](path-explorer-optimisation/hub-labels.md);
SIMD constraints for the simulation → [simd-applicability](simd-applicability.md); profiling
method and measured hotspots → [performance](performance.md).

## Current engine

The path explorer precomputes, for every pair of working halts `(origin, target)`, an
estimated journey time and the next transfer halt. The routing search is a **per-origin Dijkstra on an
expanded state graph**:

- The halt-list preparation and connexion rebuild generate every walking candidate, every
  direct ride, and every through candidate across a coupling (a journey that stays aboard
  when portions join or split), before the round's deduplication. A candidate is
  `(origin halt, target halt, transport, waiting + journey + transfer)`. The halts'
  `connexions_map` is deduplicated to one best full weight per halt pair for runtime
  boarding; the full list is kept separately for the routing-search graph.
- The eligible-halt selection picks the working halts and swaps the deduplicated runtime
  tables into the halts.
- The graph build creates a compact per-origin edge list and the expanded state space. A state is
  `(halt, arriving transport)`; transport 0 is walking and is exempt from the no-reboarding
  rule. The first state of every halt is the origin state (no arriving transport). The state
  count on the gargantuan fixture is 17,987, 3.35 per halt.
- The routing search runs one Dijkstra per origin over the states. At a state, an outgoing edge is
  skipped when its transport equals the arriving transport and is not walking. The search
  fills one matrix row (`time`, `next_transfer`). Rows are independent; the lead and the helpers
  claim the next origin from a shared counter, so the row contents do not depend on the claim
  order or the thread count.
- The waiting-packet reroute marks halts; `haltestelle_t::step` re-routes waiting packets on
  the main thread.

The finished matrix is 6 B per cell (`uint32` time + `uint16` next transfer). The working
matrix is also 6 B per cell. `get_path_between` is unchanged.

Comfort routing changes the cost stored on each stop-to-stop connection and the rule that
decides which duplicate connection to keep. The time-based comfort model leaves the search
kernel unchanged, but the comfort-improvement rule, which prevents changing transport only to
get a fresh comfortable-time allowance for the next leg, adds a check on each candidate edge
and changes which routes the search accepts. The arriving transport's ride
time to each possible destination is cached per search state, so the check does not re-scan the
connection list; on the gargantuan fixture the rule adds about 20% to a complete rebuild of the
route tables (see [comfort-routing](path-explorer-optimisation/comfort-routing.md)).

### Measurements

`bb-10-sep-2023.sve`, passenger class 0: H = 5,375 working halts, 4,665 transfer halts,
177,672 full connexion-rebuild candidates against 108,542 deduplicated connexions, 28,885,250 pairs.
Profile (server) x64, `threads = 4` (three pool threads), full instant refresh. [EXECUTION-
VERIFIED:2026-11-07, ex-15 @ 375ebf369]

- Full run including load, two runs: 80.6 s and 85.1 s (median 82.9 s); load alone ≈14.9 s,
  so the refresh is ≈68 s.
- Peak working set 3,880 MB; peak private bytes 3,911 MB.
- Routing search per large passenger class: 10.47, 11.01, 11.12, 11.68 and 11.20 s. The
  single-threaded per-origin search is 18.0 s per class; the three-thread pool gives
  1.55-1.71x.
- Graph build: 37-43 ms per large passenger class; the routing search dominates.
- Accuracy: the in-game engine's times matched a standalone Dijkstra over the same full
  candidate list on all 27,989,638 finite pairs (zero differences); next transfers differed
  only through tie order on 37,736 pairs (0.13%).
- Network: loopback server smoke passed with 1,049 identical per-step route hashes.

The five large passenger classes dominate the refresh. Load and the other refresh steps
bound the full-run gain; the routing search dominates the compute. The per-origin search is
memory-latency bound, which limits the pool scaling.

The current search uses dynamic origin claiming, a value heap and a grouped edge stream.
With the same fixture and an instrumented build (TEST phase timers and `-debug 2`, full
refresh after load), the full run measures 80.2 s, the large-class wall time about 12.5 s,
and the single-thread full run 105.2 s. Edge scans and heap pushes are 2.86-2.94 B and
120.9-130.3 M per large class. The instrumented figures include the timers and debug logging;
compare them with each other, not with the canonical figures above.
[EXECUTION-VERIFIED:2026-11-07, ex-15 @ 6447a1d2e]

## Constraints

- The finished matrices are synced, saved state. Changes to route results are an AGENTS.md
  rule-5 review.
- Each origin's routing-search result must be a deterministic function of the synced graph,
  independent of the thread count and of the origin split. The per-origin Dijkstra has that
  property; the lead alone advances `origin_index`, `iterations_processed` and
  `total_iterations`.
- Integer arithmetic only. State keys, distances and edge weights are integers.
- The routing-search budget is in **origins per step**. `default_explore_paths` is 16; the
  controller adjusts it to `path_explorer_time_midpoint` (64 ms code default; the shipped
  simuconf sets 128 ms). In network mode `nwc_routesearch_t` broadcasts the minimum limit
  across peers, so the slowest peer sets the pace.
- Through connexions across couplings are direct connexion-rebuild candidates, used as
  ordinary search edges. The expanded state uses the arriving transport; the through edge's
  transport is the boarding linkage.

## Savegame and network

`EX_SAVE_MINOR` is 9. A mid-refresh save carries only the finished matrix; a load restarts
the current compartment from the start of the refresh and keeps the previous finished matrix.
This is safe when every peer reloads, but it loses the current compartment's in-progress
refresh work at a join. At the 128 ms budget and 5.6 steps/s a large passenger
class takes ≈17-18 s of session time, so the expected join loss is ≈8-9 s and the worst case
≈17-18 s (≈6 s and ≈12-13 s of explorer work). Small compartments lose under a second.

Old saves carry the old routing-search working state, which the load discards; a
mid-refresh save restarts. Saves written for a target savegame revision below 9 carry the
finished matrix and a restart request when no finished matrix exists.

The loopback join test (2026-11-07) exercised a mid-refresh revision-9 join: server and
client both reloaded the save and 287 common per-step route hashes matched over 120 s, with
no desync markers (→ [routing-search-kernel](path-explorer-optimisation/routing-search-kernel.md)).

## Future work

- Exact mid-pass resume: save the full candidate list, the working matrix and
  `origin_index`, and rebuild the graph on load. Cost: roughly 175 MB per mid-pass
  compartment in the savegame and join transfer. Currently the restart is preferred at the
  observed join rate.
- Pool efficiency: the origin claim is dynamic and the heap stores values. The current
  large-class wall time is about 12.5 s and the instrumented full run 80.2 s. The remaining
  limit is memory latency on the edge scan; further gains need an algorithmic change or a GPU
  backend.
- Memory: the matrices remain O(H²). Hub labels were assessed and rejected as a compact
  distance oracle: the label set exceeds the matrix well before full coverage and the build
  is far slower than the current search (→
  [hub-labels](path-explorer-optimisation/hub-labels.md)). The 32-bit time field is retained
  deliberately for future overseas destinations [RECOLLECTION:2026-11-07].
- GPU: a many-source graph search over the expanded states is unassessed; assess if a GPU use
  case exists.
- Comfort-based route choice and through journeys are implemented: the connection costs, the
  rule that prevents changing transport only to get a fresh comfortable-time allowance for the
  next leg, and the through-journey candidates are part of the search. The ride-time lookup used
  by that rule is cached per search state; on the gargantuan fixture the rule adds about 20% to
  a complete rebuild. The remaining comfort-routing tasks (the saved marker recording which
  comfort weights built the route tables, Squirrel scripting tests, pakset calibration, the save
  guard for the older 15.10 format and translations) are listed in
  [comfort-routing](path-explorer-optimisation/comfort-routing.md).

## Open questions

- Is exact mid-pass resume worth the save/transfer cost, given the observed join rate?
- Does every threaded subsystem tolerate mixed local thread counts? `debug_sums[4]` is zeroed;
  the path-explorer per-origin rows are verified independent of the thread count.
- Is there a compact routing oracle whose build cost is close to the current per-origin
  search? Hub labels do not qualify (→ [hub-labels](path-explorer-optimisation/hub-labels.md)).
- Comfort routing: the time-based cost model, the rule that prevents changing transport only
  to get a fresh comfortable-time allowance for the next leg, and the boarding comparison are
  implemented (see [comfort-routing](path-explorer-optimisation/comfort-routing.md)). The exact
  comparison used by that rule, the remaining ~20% rebuild cost, pakset calibration and the
  companion changes remain open.
- GPU: is there a use case?

## Provenance

The expanded-state engine, full candidate list, state graph, pool and revision-9 savegame
were designed and measured on `ex-15` at `a203862c1`,
Profile (server) x64, `bb-10-sep-2023.sve`. The verification used a standalone
expanded-state Dijkstra over the same candidate list. [CODE; EXECUTION-VERIFIED where
tagged]
