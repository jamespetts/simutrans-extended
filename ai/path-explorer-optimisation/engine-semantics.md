---
status: draft
verified: ex-15 @ 375ebf369
---
# Path explorer optimisation: route semantics

**Covers:** what the routing database computes and how passengers act on it. Code:
path_explorer.cc (connexion rebuild, graph build, routing search),
`haltestelle_t::find_route`, `fetch_goods`,
`liefere_an`, `starte_mit_route`, `calc_ready_time` (simhalt.cc),
`vehicle_t::unload_cargo` (vehicle.cc). Untagged prose is [CODE] at the frontmatter value;
the examples are constructed illustrations.

## Engine

1. **Edges (connexion rebuild):** per linkage (line with convoys, or lineless convoy), a candidate
   from every stop to every other stop, omitting (on ex-15) set-down-only origins,
   pick-up-only targets and pairs across consist-order validity zones. Weight = waiting
   time (origin and destination halt, mean of the last 32 samples) + journey time (linkage
   average for the pair, else an accumulated estimate) + the origin halt's transfer time.
   Passengers also get walking candidates between halts within walking distance (waiting 0,
   walking time, destination halt's transfer time).
2. **Candidate list:** every candidate is kept in `full_connexions` before the round's
   deduplication. The halts' runtime `connexions_map` keeps one best full weight per halt
   pair; the routing-search graph uses the full list. The best weight compares waiting,
   journey and transfer time together, so a walking edge with no waiting time can beat a
   line whose ride is shorter (Example E).
3. **State graph (graph build):** a state is `(halt, arriving transport)`; transport 0 is
   walking and is exempt from the no-reboarding rule. The origin state has no arriving
   transport. An edge from a state on transport X to transport X is forbidden unless X is
   walking.
4. **Search:** one Dijkstra per origin over the state graph. The stored cell is
   `(time, next transfer halt)`; the next transfer is the first hop of the best path. The
   search is exact under the no-reboarding rule.
5. **Packet behaviour:** `find_route` sets the destination halt (`ziel`) and the next
   transfer (`zwischenziel`), which is a halt, not a linkage. `fetch_goods` boards a packet
   onto a convoy that reaches the next transfer (or the destination), subject to the
   wait-for-a-faster-convoy check. `unload_cargo` unloads at the next transfer;
   `liefere_an` re-routes there, and the packet can board again only after that halt's
   transfer time (`calc_ready_time`). At generation the route's journey time decides the
   tolerance test.

The matrix stores one route per halt pair. The expanded state is internal to the search;
the consumer sees only `(time, next transfer)`.

## The no-reboarding rule

The rule is why the routing-search state includes the arriving transport. For a halt V reached on
linkage X:

- Staying on X is represented by a single direct edge on X from the origin to the
  destination; it excludes V's transfer time.
- Changing to a different linkage Y at V pays V's transfer time and the waiting time for Y.
- An edge out of V on X is not available from the X-arrival state. Composing an arrival on X
  with a departure on X would charge the average wait for X, but a passenger who has just
  arrived on X has just missed that convoy; the next X is a full headway away, not the
  average. Allowing the composition under-estimates the journey.

The state graph makes that distinction exactly. A hub label or a halt-only graph cannot:
it either forbids a valid X→Y change or uses the average wait for an X→X change.

## Example A — the no-reboarding rule in action

Halts O, V, T. Linkage A runs O–V–T hourly; linkage B runs O–V every 10 min, timed to
connect into A at V. Measured values (minutes): waiting O→V 5, O→T 30, V→T 4; transfer
times O 2, V 3; rides A O→V 10, V→T 10, O→T 21; B O→V 12.

- Candidates: O→V keeps A (ride 10 beats 12): 5 + 10 + 2 = **17**. V→T (A): 4 + 10 + 3 =
  **17**. O→T (A): 30 + 21 + 2 = **53**.

The engine gives O→T = **36** with next transfer V: B into V (5 + 12 + 2) + A out of V
(17). The X→X composition is unavailable; the engine uses the valid B→A change.

Realised behaviour:
- The B→A passenger realises ≈36.
- The passenger who boards A first and alights at V waits for the next A (≈85); the
  companion boarding rule is what keeps the realised journey on the estimate.

## Example B — results unchanged

As A, plus linkage C on V–T (ride 9). V→T keeps C: 4 + 9 + 3 = 16. The stored transports
are A (in) and C (out), so the engine gives 17 + 16 = 33, next transfer V.

## Example C — ties

Linkages P: O→V, Q: V→T, R: O→W, S: W→T, each candidate weighing 17; no direct O→T. Both
routes weigh 34. The engine's tie-break is `(distance, state index)`, so V and W are ordered
by state index. A compact distance oracle (hub labels) would tie by hub order unless the
query compares `(time, first hop)`. Ties change line loads, not the minimum time.

## Example D — directional cells

Halts O, X, T, not within walking distance.
- Linkage L runs X→O and stops at O set-down-only, so O→X is omitted.
- Linkage M runs X↔T; X→T weighs 20.
- Linkage N runs O↔T; direct O→T weighs 58.
- X is a transfer halt; assume no other O→X path is known.

The engine gives O→T = 58, next transfer T: O is not an origin at X because O→X is
infinite, while X→O remains usable in the opposite direction.

## Example E — walking deduplication

Halts P and Q are 7 min apart on foot; bus Z rides P→Q in 3 min with a 10 min wait;
transfer time 2.

- Walking: 0 + 7 + 2 = 9. Bus: 10 + 3 + 2 = 15.
- The connexion rebuild compares full weights (9 < 15) and keeps the walking connexion in
  the runtime `connexions_map`, so `get_preferred_line` leaves the line unbound and the
  passenger walks.
- The matrix keeps both candidates and chooses the walk (9).

## Consequences

- **Balance:** the engine finds the exact no-reboarding route, so more trips pass the
  tolerance test and demand and line loads reflect the full candidate list. This is an
  intentional balance change.
- **Companion rule:** the stored route is a halt, not a linkage. Without a stay-aboard rule
  or a stored next linkage in `ware_t`, a passenger who reaches its next transfer on a
  convoy that continues can be forced to alight and wait. The rule is required by the
  current engine and by comfort routing.
- **Through journeys:** the through candidates are direct connexion-rebuild edges; the state
  records the boarding transport.
- **Verification:** the engine's times were checked cell-for-cell against a standalone
  Dijkstra over the same full candidate list (zero differences on 27,989,638 finite pairs;
  next transfers differed only by tie order on 0.13%).

## Open questions

- Companion boarding rule: exact form (stay aboard vs stored next linkage in `ware_t`).
- Comfort routing: connexion-rebuild weights and the per-leg split incentive; the engine supports the
  decomposable weights.
- Tie-breaking: `(distance, state index)` is deterministic; should it prefer a specific line
  or halt order to match player expectations?
- Compact distance oracle: only if the O(H²) memory limit becomes binding.

## Provenance

The engine and examples were re-derived from path_explorer.cc and the routing-search measurements
on `ex-15` at `a203862c1`, 2026-11-07. The runtime-table deduplication was corrected to compare
full weights at `375ebf369`. [CODE; EXECUTION-VERIFIED where tagged]
