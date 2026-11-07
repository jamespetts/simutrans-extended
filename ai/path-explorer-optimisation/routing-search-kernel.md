---
status: draft
verified: ex-15 @ 6447a1d2e
---
# Path explorer optimisation: the routing-search engine

**Covers:** `path_explorer_t::compartment_t::step` graph build and routing search
(path_explorer.cc), the full connexion-rebuild candidate list, the compact per-origin edge list, the
expanded `(halt, arriving transport)` state space, the per-origin Dijkstra, the routing-search
worker pool and the routing-search savegame state. Parent:
[path-explorer-optimisation](../path-explorer-optimisation.md). Untagged prose is [CODE];
performance figures are [UNVERIFIED] unless tagged.

## Candidate list

The halt-list preparation and connexion rebuild append every walking candidate, every direct
ride and every through candidate across a coupling (a journey that stays aboard when portions
join or split) to `compartment_t::full_connexions` before the round's cross-linkage
deduplication:

```
full_connexion_t { origin_halt_id, target_halt_id, transport_idx, weight }
```

- `transport_idx` is the compact line/convoy index from `transport_index_map`; walking is 0.
- `weight` is `waiting_time + journey_time + transfer_time`, clamped to `uint32`.
- The connexion rebuild deduplicates each halt's `connexions_map` (one best full weight per
  halt pair); that table drives runtime boarding. The full list is the input to the
  routing-search graph.
- The full list is cleared at the start of a new pass and after the routing search.

On `bb-10-sep-2023.sve`, passenger class 0, the full list has 177,672 candidates against
108,542 deduplicated connexions.

## Compact graph and state space

The graph build creates, once per compartment:

- a per-origin outgoing edge stream (`out_offset` over `out_edges`), with
  `out_edges[i] = { target halt, transport, weight, target state }`. The build
  first fills `full_edges`/`out_edge` (edge indices grouped by origin), resolves
  every edge's target state, then copies the resolved edges into `out_edges` and
  releases the build arrays, so the search reads one contiguous stream per halt;
- the distinct incoming transport keys per halt, sorted and deduplicated;
- one state per `(halt, arriving transport)` plus one origin state per halt (arriving
  transport = NONE). `state_key` is a `uint32`; NONE is `0xFFFFFFFF`;
- `state_halt`, `state_offset` and each edge's `target_state`.

The state count is `H + sum over halts of distinct incoming transport keys`. On the
gargantuan fixture: 17,987 states, 3.35 per working halt.

The working matrix is allocated here as `uint32` times and `uint16` next transfers, both
6 B per cell. The diagonal is 0.

The graph build takes 37-43 ms per large passenger class.

## Per-origin Dijkstra

For each origin `o`:

1. Reset the thread's distance and first-hop arrays to infinity for all states.
2. Set `dist[state_offset[o]] = 0` (the origin state) and push it.
3. Pop the lowest `(distance, state)`; skip stale entries. For every outgoing edge of the
   state's halt:
   - skip when `edge.transport == arriving transport` and the transport is not walking;
   - skip when the edge has no valid target state;
   - relax on strict improvement and push the target state.
   The first hop is the edge's target halt when the source is the origin state; otherwise it
   is inherited from the source state.
4. The target halt's distance is the minimum over its states; the next transfer is the first
   hop of the best state, converted to a halt ID. The origin row's diagonal is 0.

The tie-break is `(distance, state index)`, deterministic for the synced graph. Each
origin's result is independent of the thread count and the origin split.

The Dijkstra uses a `binary_heap_tpl<heap_item_t>` with one value heap and per-thread
`dist`/`first_hop` arrays per pool thread. The heap is reset per origin. The value heap
removes the pointer indirection and the separate item pool; it also removes the old
`full_edge_count` push cap, which could silently drop an improvement.

## Pool

The routing-search pool has `get_parallel_operations()` threads in total: one lead and the rest
helpers, parked on `pool_start_cond` and released per batch. The batch is a contiguous range
of origins. The lead and the helpers claim the next origin from a shared counter under
`pool_work_mutex` until the batch is exhausted; each thread runs the per-origin Dijkstra and
writes only that origin's row. Claim order changes only which thread writes a row, not the
row's contents, so results are independent of the claim order and the thread count. The lead
alone advances `origin_index`, `iterations_processed` and `total_iterations`, then joins the
helpers before the next batch. At one thread the batch runs serially. The scratch arrays are
allocated for the pool thread count and reallocated if the pool is initialised after a save
load.

## Budget and network

The budget unit is **origins per step**:

- `default_explore_paths` is 16.
- The controller targets `path_explorer_time_midpoint` per simulation step (64 ms code
  default; the shipped simuconf sets 128 ms) from the measured origins/ms.
- The lead processes `min(remaining budget, working_halt_count - origin_index)` origins, at
  least one when a pass is entered with no budget left.
- In network mode `nwc_routesearch_t` broadcasts the minimum limit set across peers. Every
  peer processes the same number of origins per step, so the slowest peer sets the pace.

A mid-refresh save carries the finished matrix only. On load the compartment restarts from
the start of the refresh, and the previous finished matrix stays available. The expected join
loss on the gargantuan fixture at 128 ms and 5.6 steps/s is ≈8-9 s of session time (≈6 s of
explorer work), worst case ≈17-18 s (≈12-13 s of explorer work). Exact resume would need the
full candidate list, the working matrix and `origin_index`.

## Verification

- **Times:** the in-game engine's finished times matched a standalone Dijkstra over the same
  full candidate list and state rules on all 27,989,638 finite pairs of the gargantuan
  passenger class: zero differences. Next transfers differed only through tie order (37,736
  pairs, 0.13%).
- **Reachability:** the engine resolves 27,989,638 finite pairs; the remaining 895,612 pairs
  are unreachable on the full candidate graph.
- **Network:** loopback server smoke passed with 1,049 per-step route hashes identical.
- **Join and revision 9:** a loopback server loaded `bb6-apr-2010.sve` (14.66), ran with
  `reroute_check_interval_steps = 1` and a 16 ms explorer budget so the join save was
  mid-refresh, wrote a revision-9 join save, and a client loaded it. Server and client both
  reloaded revision 9; 287 common per-step route hashes were identical over 120 s with no
  desync markers. [EXECUTION-VERIFIED:2026-11-07, ex-15 @ 6447a1d2e, Profile (server) x64]
- **Old saves:** the 14.x fixture loads; a mid-refresh save restarts the compartment and
  keeps the finished matrix.

## Measurements

`bb-10-sep-2023.sve`, Profile (server) x64, `threads = 4` (three pool threads), full instant
refresh:

- Full run including load, two runs: 80.6 s and 85.1 s (median 82.9 s); load ≈14.9 s, so the
  refresh is ≈68 s.
- Peak working set 3,880 MB; peak private bytes 3,911 MB.
- Routing search per large passenger class: 10.47, 11.01, 11.12, 11.68 and 11.20 s.
- Single-threaded routing search: 18.0 s per class; the pool gives 1.55-1.71x.
- Graph build: 37-43 ms per class.

2026-11-07 follow-up (same fixture, Profile (server) x64, `threads = 4`, TEST phase timers and
`-debug 2`, full refresh after load):

- Before the follow-up (indexed edges, pointer heap, contiguous slices): full run 98.3 s;
  single-thread full run 129.3 s. Large passenger classes: 41.6-47.4 s total thread time,
  14.9-16.6 s wall; single-thread 20.8-22.3 s.
- After the grouped edge stream, value heap and dynamic origin claim: full run 80.2 s;
  single-thread full run 105.2 s. Large passenger classes: 37.4-38.5 s total thread time,
  ~12.5 s wall; single-thread 16.4-17.7 s. Per-thread times differ by under 0.02 s. Edge
  scans and pushes are identical to the baseline (2.86-2.94 B edges and 120.9-130.3 M pushes
  per large class), confirming that the relaxations are unchanged. The follow-up's parallel
  wall against its single-thread figures is 1.31-1.42x; the pool is bandwidth-limited.
- The instrumented figures include the TEST counters and `-debug 2`; compare them with each
  other, not with the uninstrumented figures above.

[EXECUTION-VERIFIED:2026-11-07, ex-15 @ 6447a1d2e]

## Through journeys and comfort

Through connexions across couplings are direct connexion-rebuild candidates in the full list.
The state graph records the boarding transport; the through edge is a normal search edge with
one transport field.

Comfort routing changes the connexion-rebuild edge weight and deduplication policy; the
routing-search kernel is unchanged. The weight is one non-negative integer per edge per
compartment, and the current state/edge representation keeps one cost field per cell (→
[comfort-routing](comfort-routing.md)).

## Future work

- Exact mid-pass resume (full candidate list + working matrix + origin index) if the join
  rate justifies the extra ≈175 MB per mid-pass compartment.
- Pool efficiency: the origin claim is now dynamic and the heap stores values; the 2026-11-07
  follow-up reduced the large-class wall time by about 16% and the full instrumented run by
  about 18%. The remaining limit is memory latency on the edge scan; further gains need an
  algorithmic change (hub labels) or a GPU backend.
- Re-assess GPU against the current graph-search shape.
- Compact distance oracle (hub labels) if the O(H²) memory limit becomes binding.

## Provenance

The expanded-state engine, candidate list, state graph, pool and revision-9 savegame were
implemented and measured on `ex-15` at `a203862c1`, Profile
(server) x64. The runtime-table deduplication was corrected to compare full weights at
`375ebf369`. The 2026-11-07 follow-up (grouped edge stream, value heap, dynamic origin claim)
was measured at `6447a1d2e`. The verification used a standalone expanded-state Dijkstra
over the same candidate list. [CODE; EXECUTION-VERIFIED where tagged]
