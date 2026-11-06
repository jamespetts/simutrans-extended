---
status: draft
verified: ex-15 @ bf447619a
---
# Path explorer optimisation: route semantics and behaviour of alternative engines

**Covers:** what the present routing database computes and how passengers act on it, and the
behaviour changes of the result-changing levers (0, 7, 8) of
[path-explorer-optimisation](../path-explorer-optimisation.md), with worked examples. Code:
path_explorer.cc (phases 1, 2, 4, 5), `haltestelle_t::find_route`, `fetch_goods`, `liefere_an`,
`starte_mit_route`, `calc_ready_time` (simhalt.cc), `vehicle_t::unload_cargo` (vehicle.cc).
Untagged prose is [CODE] at the frontmatter value; the examples are constructed illustrations.

## Present semantics

1. **Edges (phase 2):** per linkage (line with convoys, or lineless convoy), a connexion from
   every stop to every other stop, omitting (on ex-15; master has none of these flags)
   set-down-only origins, pick-up-only targets and pairs across consist-order validity zones.
   Weight = waiting time (origin halt, destination halt) +
   journey time (linkage average for the pair) + the origin halt's transfer time. For passengers,
   phase 1 also adds walking connexions between halts within walking distance (waiting 0, walking
   time, the *destination* halt's transfer time).
2. **Deduplication:** one connexion per halt pair, the one with the smallest journey time. Between
   linkages this keeps the smallest weight, because waiting and transfer times are the same for
   every linkage of a pair. Against a walking connexion it does not (Example E).
3. **Closure (phase 5):** one stored path per pair, with its first and last transport. At a
   transfer halt a composition is rejected when the stored path into the halt ends on the same
   linkage as the stored path out of it starts (walking excepted), even if a different linkage
   also reaches the halt (Example A). Ties keep the first value found, in transfer-halt index
   order.
4. **Packet behaviour:** `find_route` sets the destination halt (`ziel`) and the next transfer
   (`zwischenziel`), which is a halt, not a linkage.
   - `fetch_goods` boards the packet onto any convoy that reaches the next transfer (or the
     destination), subject to the wait-for-a-faster-convoy check.
   - `unload_cargo` unloads it at its next transfer.
   - `liefere_an` re-routes it there, and it can board again only after that halt's transfer time
     (`calc_ready_time`).
   - At generation, the route's journey time decides the tolerance test (trip accepted or "too
     slow").

## The alternative engines

- **7a — Dijkstra rows on the present graph:** one shortest-path search per source over the
  deduplicated connexions, with no transport condition. Next transfer = first hop; ties resolved
  by settle order (time, then halt index).
- **7b — Dijkstra rows on an expanded graph:** node = (halt, arriving linkage); edges per linkage
  without cross-linkage deduplication (walking connexions kept as separate edges); continuing
  from linkage X onto linkage X at a halt is
  forbidden (walking excepted). This gives the exact minimum under "no re-boarding of the same
  linkage". The graph is larger: nodes ≈ Σ over halts of (serving linkages + 1), edges ≈ Σ S².
- **8 — hub labels** built on the 7a or the 7b graph: the same distances as that variant. The next
  transfer comes from first hops stored in the label entries; ties are resolved by hub order
  unless the query compares (time, first hop) (Example C).

Every route the present closure records is a valid path in both graphs (it composes stored paths
only where their transports differ). So for every pair the present engine resolves, 7a and 7b
each give an estimate no greater than the present one: **estimated times never increase, and more
trips pass the tolerance test.** 7a is usually the lower of the two, but not always: 7b keeps
edges that phase 2's deduplication discards (Example E).

## Example A — a composition the present closure misses

Halts O, V, T. Linkage A runs O–V–T hourly; linkage B runs O–V every 10 min, timed to connect
into A at V. Measured values (minutes): waiting O→V 5, O→T 30, V→T 4; transfer time O 2, V 3;
rides A O→V 10, V→T 10, O→T 21 (including the dwell at V), B O→V 12.

- Connexions: O→V keeps A (ride 10 beats 12): 5 + 10 + 2 = **17**. V→T (A): 4 + 10 + 3 = **17**.
  O→T (A): 30 + 21 + 2 = **53**.

| Engine | O→T estimate | Next transfer | Why |
|---|---|---|---|
| Present | 53 | T | at V, O is registered under A and T under A: same linkage, composition rejected; B's path to V is not stored |
| 7a | 34 | V | 17 + 17; no transport condition |
| 7b | 36 | V | arriving on A cannot continue on A; via B: (5 + 12 + 2) + 17 |
| 8 | 34 / 36 | V | as 7a / 7b, whichever graph it is built on |

Realised behaviour:
- **Present:** packets wait at O for A (B does not reach T); ≈ 53. With a tolerance of 45 the trip
  is "too slow" and is not made.
- **7a and 7b:** the trip is accepted; packets board the first convoy that reaches V.
  - On B: unloaded at V, boardable after 3, the timed A follows: ≈ 36. The 7b estimate is right;
    7a's is 2 low (it costs the first leg at A's ride).
  - On A (whenever an A is the first convoy to V): unloaded at V because V is their next
    transfer. They can board only after V's transfer time, so if that A leaves V sooner they wait
    for the next A: ≈ 5 + 10 + 3 + ~57 + 10 ≈ 85. Neither estimate holds for them.

Hence 7 and 8 need a **companion rule** so that realised journeys match estimates. Either keep a
packet aboard at its next transfer when its convoy serves the continuation from there, or store
the next linkage in `ware_t` (a savegame change). Without it, some present "too slow" rejections
become accepted trips that take longer than estimated.

## Example B — results unchanged

As A, plus linkage C on V–T (ride 9). V→T keeps C: 4 + 9 + 3 = 16. At V the stored transports are
A (in) and C (out), so the present closure composes 17 + 16 = 33. 7a gives 33 and 7b gives
min(A then C 33, B then C 35, direct 53) = 33; next transfer V in all engines. Behaviour is
unchanged wherever each transfer's best continuation is on a different linkage.

## Example C — ties (lever 8)

Linkages P: O→V, Q: V→T, R: O→W, S: W→T, each connexion weighing 17; no direct O→T. Both routes
weigh 34.
- Present and 7a: V has the lower working index, so V is found first and kept; next transfer V.
- 8: the query finds hubs V and W, both at 34. A query that keeps the first minimum in hub order
  returns W when W ranks above V (for example, more serving linkages). Passengers from O to T then
  board R to W instead of P to V: same estimate, different line loads.
- A query that compares (time, first-hop index) over all minimal hubs reproduces 7a's choice, at
  extra query cost.

## Example D — the closure bug (levers 0, 7, 8)

Halts O, X, T, not within walking distance of each other.
- Linkage L runs X→O and stops at O as set-down-only, so the connexion O→X is omitted.
- Linkage M runs X↔T; connexion X→T weighs 20.
- Linkage N runs O↔T; direct connexion O→T weighs 58.
- X is a transfer halt (served by L and M). Assume no other O→X path is known when X is processed
  (for example, X precedes T in transfer-halt order).

What happens:
- **Present:** O is registered at X because `[X][O]` is finite. `[O][X]` is `UINT32_MAX`, so the
  sum wraps to 19, which is below 58. `[O][T]` becomes 19 with a null next transfer and reports
  **no route**; N's direct service is lost.
- **Levers 0, 7, 8:** 58, next transfer T. Passengers from O to T travel on N.

## Example E — walking deduplication (present behaviour; kept by 7a and by 8 on 7a's graph)

Halts P and Q, 7 min apart on foot; bus linkage Z rides P→Q in 3 min; waiting P→Q 10; transfer
times 2.
- Walking connexion: 0 + 7 + 2 = 9. Bus connexion: 10 + 3 + 2 = 15.
- Phase 2 compares journey times (3 < 7), so the bus replaces the walk: `[P][Q]` = 15 and the
  preferred line is Z.
- At generation `starte_mit_route` walks only when there is no preferred line or convoy, so these
  passengers wait for Z. At transfers `liefere_an` compares estimated arrival times and may walk.
  The matrix carries 15 instead of 9 in every route through P→Q.
- 7a (and 8 built on its graph) consumes phase 2's deduplicated output, so it keeps the 15 until
  phase 2 compares full weights (→ [known-bugs](../known-bugs.md)). 7b keeps both edges and
  chooses the walk (9).

## Consequences for adopting 7 or 8

- **Balance:** more routes are found and estimates fall, so more trips pass tolerance tests,
  demand on networks with many transfers rises, and line loads and revenue shift. Long-running
  network games change at the upgrade.
- **Companion rule** (Example A) is part of any adoption.
- **Y/H:** the through-journey substitution needs the 7b state (arriving linkage) at coupling
  halts.
- **Verification:** the present matrices cannot be a byte-identical oracle. Use cell-for-cell
  expectations from worked examples like these on synthetic maps. Route outcomes decide later
  `simrand` draws, so compare old and new engines per decision on frozen snapshots, not by running
  both games forward.
