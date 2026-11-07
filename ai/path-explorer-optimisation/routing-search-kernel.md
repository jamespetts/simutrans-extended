---
status: draft
verified: ex-15 @ a203862c1
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

- a per-origin edge list (`out_offset`/`out_edge` over `full_edges`), with
  `full_edges[i] = { target halt, transport, weight, target state }`;
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

The Dijkstra uses a `binary_heap_tpl<heap_item_t*>` with a per-thread item pool sized to the
edge count and per-thread `dist`/`first_hop` arrays. The heap is reset per origin.

## Pool

The routing-search pool has `get_parallel_operations()` threads in total: one lead and the rest
helpers, parked on `pool_start_cond` and released per batch. The batch is a contiguous range
of origins. Each thread runs the per-origin Dijkstra for its slice and writes only its own
rows. The lead alone advances `origin_index`, `iterations_processed` and
`total_iterations`, then joins the helpers before the next batch. At one thread the batch
runs serially. The scratch arrays are allocated for the pool thread count and reallocated if
the pool is initialised after a save load.

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

[EXECUTION-VERIFIED:2026-11-07, working tree]

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
- Client join and revision-9 save/load roundtrip test.
- Pool efficiency: the per-origin Dijkstra is memory-latency bound; current scaling
  1.55-1.71x on three threads.
- Re-assess GPU against the current graph-search shape.
- Compact distance oracle (hub labels) if the O(H²) memory limit becomes binding.

## Provenance

The expanded-state engine, candidate list, state graph, pool and revision-9 savegame were
implemented and measured on `ex-15` at `a203862c1`, Profile
(server) x64. The verification used a standalone expanded-state Dijkstra over the same
candidate list. [CODE; EXECUTION-VERIFIED where tagged]
