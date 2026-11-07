---
status: draft
verified: ex-15 @ 5eda2829e
---
# Hub labels (assessed, rejected)

**Covers:** the assessment of a Pruned Landmark Labeling (PLL) hub-label distance
oracle for the path-explorer routing tables: the prototype, its exactness, label memory,
build time and query cost, and the decision not to adopt it. Parent:
[path-explorer-optimisation](../path-explorer-optimisation.md). The current engine is in
[routing-search-kernel](routing-search-kernel.md).

## Method

- The in-game graph build and the finished matrix were dumped for passenger class 0 on
  `bb-10-sep-2023.sve` (gargantuan) and `bb6-apr-2010.sve` (small), using TEST hooks in
  `path_explorer_t::compartment_t::build_search_graph` and `step`; the hooks were removed
  after the runs.
- A standalone prototype rebuilt the expanded state graph, checked sample Dijkstra rows
  against the dumped matrix, and built PLL labels with forward and backward searches. A
  label stores hub rank, distance and the first-hop halt (10 B/entry), so `next_transfer`
  is reconstructed as well as the aggregate time.
- Vertex order: state degree descending (out-degree plus in-degree).
- Prototype, graph dumps and logs: `ai/temp/perf/hub-label/` (persistent performance
  state).

## Correctness

- On the small fixture the full label set reproduced the matrix time on all 6,086,089
  pairs (0 mismatches, including the 93,024 unreachable pairs).
- Next transfers differed on 16,107 of 5,990,598 finite pairs (0.269%). The differences
  are tie choices and no next transfer was missing.
- The oracle is therefore exact in the same sense as the matrix; the rejection is a cost
  decision, not a correctness one.

## Measurements

Small fixture: H = 2,467, S = 8,272, E = 57,605; 191,016 allowed state edges.

- Finished matrix 34.8 MiB (6 B/cell); finished plus working 69.6 MiB.
- Full PLL build 231.5 s single-thread; 5,897,430 entries, 56.2 MiB at 10 B/entry
  (1.61x the finished matrix; 0.81x both matrices).
- Query: 4,690 ns/pair against 9 ns/pair for a matrix lookup in the same benchmark. The
  matrix figure is a cached-read best case; a cache miss is tens of nanoseconds, so the
  label query is still roughly one to two orders of magnitude slower.

Gargantuan fixture: H = 5,375, S = 17,987, E = 177,672; 603,428 allowed state edges;
finished matrix 165.3 MiB (173.3 MB).

- 1,500 of 17,987 hubs (8.3%): 22,184,578 entries, 211.6 MiB, 170.2 s.
- 2,000 hubs (11.1%): 23,881,188 entries, 227.8 MiB, 257.2 s.
- 3,000 hubs (16.7%): 25,296,212 entries, 241.2 MiB, 451.8 s.
- At 8.3% of the hubs the labels already exceed the finished matrix. A linear
  extrapolation of the 3,000-hub build is about 2,709 s (45 minutes) for one class,
  against 16.4-17.7 s single-thread and 12.5 s wall for the current per-class search,
  and 68 s for the whole five-class refresh.
- The build floor is structural: PLL runs one forward and one backward search per state
  (2S = 35,974) against one search per halt (H = 5,375) for the current engine, a ratio
  of 6.7x before coverage checks and before the current three-thread pool is considered.

## Vertex order

At 1,000 hubs on the small fixture:

| Order | Entries (out + in) | Size at 10 B/entry | Build |
|---|---:|---:|---:|
| Degree descending | 5,124,976 | 48.9 MiB | 23.9 s |
| Halt-grouped (states of a halt together) | 4,532,030 | 43.2 MiB | 17.8 s |
| Degree ascending | 13,297,122 | 126.8 MiB | 13.7 s |
| Random | 13,853,380 | 132.1 MiB | 62.3 s |

Degree descending is the best of the simple state orders. Halt-grouped is about 12%
smaller at 1,000 hubs but does not change the gargantuan result.

## Verdict

Hub labels are rejected for the current engine:

- **Memory:** the label set is larger than the matrix well before full coverage and keeps
  growing; it is not a compact replacement at this halt count.
- **Build:** the graph changes on every refresh, so labels cannot be reused; the 3,000-hub
  partial already takes 7.5 minutes and the linear extrapolation for one class is about
  45 minutes.
- **Query:** the label merge is far slower than the O(1) matrix cell.

The matrix remains the routing table. The 32-bit time field is retained deliberately:
overseas destinations may need the full 32-bit range, so narrower 16-bit or 24-bit time
storage is rejected [RECOLLECTION:2026-11-07]. A packed 6 B/entry label format was also
considered; it would reduce label memory by about 40% but not the build cost, and the
full label set would still exceed the matrix.

## Open questions

- None on the hub-label direction. A different compact oracle would have to beat the
  matrix on build cost as well as memory; the assessed PLL does not.

## Provenance

The engine-side dumps and the prototype measurements were executed on `ex-15` at
`5eda2829e`, Profile (server) x64, on the two fixtures above.
[EXECUTION-VERIFIED:2026-11-07]
