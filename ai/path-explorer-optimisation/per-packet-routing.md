---
status: draft
verified: ex-15 @ bf447619a; waiting-time mechanisms @ ex-15 @ f809a1e55 plus working-tree changes (2026-11-12)
---
# Path explorer optimisation: per-packet routing (assessed, rejected)

**Covers:** whether each passenger, mail or goods packet could search for its route at decision
time, using precomputed edge costs, instead of reading a precomputed all-pairs database. Parent:
[path-explorer-optimisation](../path-explorer-optimisation.md). Untagged prose is [CODE] at the
frontmatter value; cost figures are [UNVERIFIED] derivations.

## Verdict

Not feasible at gargantuan scale.
- **Cost:** one search per decision costs about 20 µs–3 ms on this graph, against about
  0.1–0.2 µs per database cell (0.3–1 µs per `find_route` call): 20× slower at best, typically
  300–3,000×.
- **Caching does not help:** caching search results per halt rebuilds the all-pairs matrix row by
  row, with unbudgeted stalls inside `step()` on every cache miss.
- **Break-even:** only for packets that fail through thousands of alternative destinations.

## Why a search is expensive here

- The graph is a union of per-linkage cliques with weights per halt pair, plus walking connexions
  (→ parent, Cost and memory structure). A halt's out-degree is the sum of (stops − 1) over its
  serving linkages: ~20–300 on average, far more at hubs. Every settled node pays its out-degree.
- Cost per decision ≈ settled nodes × (heap operation 20–50 ns + out-degree × 2–4 ns per edge).
  From 200 nodes at degree 20 up to 3,000 nodes at degree 300 this is ≈ 20 µs–3 ms.
- Goal-directed search (ALT landmarks) gains little, because waiting times inside the edge
  weights make lower bounds weak.
- Weights cannot be read live at this rate: `get_average_waiting_time` sums
  up to 32 retained samples, can read a current-wait snapshot entry, and has
  an expensive fallback (service frequency, preferred line or convoy) when
  neither source has observations. Any search needs a weight snapshot.

## Decision volume and placement

- `find_route` decisions are made:
  - at passenger and mail generation (alternatives, return and onward trips; worker threads);
  - at every intermediate unload (`liefere_an` inside the single-threaded `convoi_t::step`);
  - in `haltestelle_t::step` (reroute, transfer deliveries);
  - in `fetch_goods`;
  - in factory shipping and lead-time calculation;
  - in the AI.
- The alternative-destination loop stops at the first successful destination.
- Measured: `get_path_between` was 0.5% of in-game samples on the gargantuan fixture
  (→ [performance](../performance.md)) ≈ 6 ms CPU per real second ≈ 1 ms per step: roughly
  30–60k cell reads per second, ~10³–10⁴ decisions per step. In the same capture
  `find_destination` was 3.0%: destination sampling, not the routing database, dominates the cost
  per packet.
- Per-packet search at that volume costs 20 ms to tens of seconds per step, against ~1 ms now and
  a step interval of ~160 ms. Much of it would fall on the main thread.
- Sharing does not close the gap. Origins are random weighted buildings and destinations are
  sampled map-wide. The only real sharing is one full search per unloading halt per step (~5 ms
  each, tens to hundreds of halts per step).

## Further consequences

- **The connexion rebuild remains** (loading, walking decisions, the GUI, the AI and the
  waiting-time fallback read the connexion tables), together with its budget synchronisation.
  Only the eligible-halt selection, graph build and routing search would go, and routes are only
  as fresh as the last connexion rebuild.
- **Network play:** millisecond-scale, unbudgeted work inside `step()` on every peer. Decisions
  cannot be deferred without changing outcomes, so slow peers fall behind
  (→ [frame-pacing-smoothness](../frame-pacing-smoothness.md)).
- **Savegame and join:** a joining client needs the server's exact weight snapshot, so the
  snapshot must be serialised. The eligible-halt selection swaps connexion tables over several
  steps, so reading them directly would give searches a mixture of old and new tables.
- **Route loops:** packets re-route at every transfer. With weights changing between hops, a
  packet can be sent from A to B and back; the loop guard covers walking only.
- **Feedback instability:** waiting-time estimates pool retained samples with
  current-wait snapshot observations, and routing decisions themselves change
  passenger queues and later retained samples. Fast response lets flows switch
  together between alternatives; the long present refresh period damps this.
- **`reroute_goods` stays:** without it, cargo waiting for a removed linkage is never collected or
  discarded, and factory in-transit accounting (`fabrik_t::update_transit`) goes wrong.
- **Validation:** route outcomes decide later `simrand` draws (→ parent, engine-semantics).

## What remains valid from the idea

- For packets that evaluate thousands of alternatives, one tolerance-bounded search per packet
  costs about as much as their per-alternative lookups. This is not a reason to change engines.
- Freshness and memory are addressed better by the precomputed-database design in the parent doc.
