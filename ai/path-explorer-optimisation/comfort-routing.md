---
status: draft
verified: ex-15 @ 375ebf369 (layout/engine status; 2026-11-07); ex-15 @ ad1815c (design)
---
# Comfort-aware routing (design)

**Covers:** the planned design for letting vehicle, stop and walking comfort affect passenger
route choice and journey-time tolerance, the related revenue changes (removing the speed bonus and
comfort weighting from fares) and private-car comfort; and their effect on the route tables.
Parent: [path-explorer-optimisation](../path-explorer-optimisation.md). Roadmap entries:
[project-roadmap](../project-roadmap.md) ("Comfort-aware routing", "Waiting time limits and stop
facilities", "Different private cars for different levels of passenger wealth").

Status: design only; no code. User decisions are tagged [RECOLLECTION]; derivations are
[UNVERIFIED]; untagged code facts are [CODE] at the frontmatter value.

## Goal

Routing as realistic as possible: both route choice and the journey-time tolerance test respond to
comfort [RECOLLECTION:2026-11-05]. People pay per unit of distance; they choose one line over
another for comfort, pay to upgrade for more comfort, and do not ride at all if comfort is too low
[RECOLLECTION:2026-11-05].

## Existing comfort mechanics

- `settings_t::tolerable_comfort`: integer piecewise-linear table, journey time (tenths of minutes)
  → comfort required. pak128.Britain-Ex values (minutes → comfort): 5 → 10, 60 → 50, 150 → 125,
  360 → 165, 1440 → 250.
- Revenue: `goods_desc_t::get_total_fare` applies `base_comfort_revenue` (comfort − tolerable
  comfort → penalty or bonus percentage), weakened for short journeys by `comfort_derating`. The
  journey time is per leg (`convoi_t::calc_revenue`: line or convoy average from the packet's last
  transfer to this stop).
- `convoi_t::get_comfort(class)`: capacity-weighted mean over the convoy's vehicles, including
  catering; convoys on one line can differ.
- Passenger behaviour: comfort affects only class choice at boarding (`fetch_goods`, using the
  per-packet `comfort_preference_percentage`, including waiting for a better class). Routing
  ignores comfort.
- Per-packet walking tolerances already exist (`walking_tolerance`,
  `walking_time_preference_threshold` in `karte_t::generate_passengers_or_mail`); `find_route`
  adds the walk from each destination stop per query.
- Runtime give-up: `haltestelle_t::step` discards packets that have waited longer than
  `passenger_max_wait` ÷ speed bonus (packets whose goods have a speed bonus of 0 are never
  discarded).

## Decisions

1. Comfort sensitivity is **per leg** [RECOLLECTION:2026-11-04].
2. Comfort tolerance does **not vary by class**: wealth changes which option is affordable, not
   tolerance of discomfort [RECOLLECTION:2026-11-04]. Class compartments remain because the
   comfort experienced differs: a class-c passenger rides the best accommodation of class ≤ c
   that the line offers.
3. **Stop comfort** multiplies the effective waiting time of edges *from* that stop and imposes a
   hard limit: no routing through a stop whose comfort is more than X below tolerance, or below a
   minimum tolerable value set in simuconf.tab [RECOLLECTION:2026-11-04].
4. A **per-stop alighting/boarding discomfort constant** is accepted in principle
   [RECOLLECTION:2026-11-05].
5. **Walking:** per-passenger walk tolerance with comfort penalties beyond it for the first and
   last walks (computed per packet); a default value for mid-journey walks once routed
   [RECOLLECTION:2026-11-04].
6. **simuconf.tab settings** control how much comfort matters [RECOLLECTION:2026-11-05].
7. **Revenue:** remove the speed bonus entirely and remove the comfort weighting from fares
   [RECOLLECTION:2026-11-05]. Catering revenue and class fare multipliers are not affected by
   this decision.
8. **Private car:** each car type gets a comfort rating; a car-owning packet picks an
   era-suitable car from the weighted, era-constrained pool for the journey; if the packet chooses
   the private car, an object of that car type spawns. Pools by wealth bracket later, when the
   graphics exist [RECOLLECTION:2026-11-05].
9. Memory saving is valued: the route-table footprint is already large [RECOLLECTION:2026-11-06].
   Leaning: blended weight (option B below), final decision open.

## Edge-weight model

Per connexion h → t, per compartment, in tenths of minutes:

    waiting(h→t) × stop multiplier(h) + boarding constant(h) + transfer time(h)
      + ride time + per-leg vehicle comfort penalty(ride time, line comfort for the class)

- Each connexion is exactly one continuous ride, so every term is local to one edge. Staying
  aboard is one edge and pays no boarding cost; each change of vehicle pays one.
- Hard limits remove edges (stop below its limit for the edge's wait; vehicle below a minimum for
  the leg's duration). Removal is edge-local.
- All terms are non-negative ("comfort above tolerance does nothing").
- Mid-journey walking connexions carry the default walking penalty; first and last walks are
  costed per packet outside the tables.

Requirements on the connexion-rebuild weight model and the routing-search engine (→ parent):

- **Decomposability:** weights must be one non-negative number per edge per compartment,
  depending only on that edge. Then the routing-search engine (an expanded-state per-origin
  Dijkstra) and any future graph search work unchanged; only the connexion rebuild and the table
  consumers change.
- **Single-route sufficiency:** one stored route per halt pair is right for every passenger only
  if every passenger is judged by the same blended cost with the same weights and differs only in
  the limit on it. Individual comfort sensitivity in the tolerance test, a different comfort weight
  for the tolerance test than for route choice, and a whole-trip discomfort cap each break this:
  the stored route can fail a passenger while a slower, more comfortable route would pass. Uniform
  per-edge hard limits, the per-passenger time tolerance, and per-passenger choices at boarding
  (class, waiting for a more comfortable convoy on the same next stop) do not break it.
- **Deduplication** in the connexion rebuild compares the full weight, not journey time
  alone, so a walking edge with no waiting time can beat a line whose ride is shorter.
- **Settings changes** that affect weights must force a refresh when a save is loaded
  (`path_explorer_t::set_must_refresh_on_loading`); otherwise the saved tables keep old weights.

## Integer arithmetic and rounding [UNVERIFIED analysis]

Rounding is not a problem if: weights stay in tenths of minutes (`uint32`); rates are fixed point
(e.g. scale 2¹⁶) from an integer table indexed by comfort shortfall (comfort is an integer);
each edge's weight is computed once in the connexion rebuild in one expression with a 64-bit intermediate,
`(L × (S + r) + S/2) / S`; results round to nearest; nothing is built up by repeated small
increments. Error ≤ 0.5 tenth of a minute (3 s) per edge, unbiased; ≤ 15 s over 5 legs. The
32-sample waiting means already carry standard errors of ~0.5 min (10-minute headway) to ~3 min
(hourly), 10–60× larger. Problems arise only from integer rates in natural units (0.3 → 0), a
discomfort total truncated to whole minutes, or chained rounded multipliers. All peers perform the
same integer operations, so determinism is unaffected. Integer helpers exist:
`piecewise_linear_tpl` and `sigmoid()` (utils/simrandom.h).

## Split incentive of per-leg sensitivity [UNVERIFIED illustration]

Because required comfort rises with ride length, two short rides are penalised less than one long
one, and the router can send passengers to change onto a parallel line of similar comfort. With
the pak128.Britain-Ex table, a vehicle of comfort 60 and an illustrative penalty (ride time ×
(1 + shortfall/220)):

| Ride | Penalty as one ride | Split into 75-min rides | Saving |
|---|---|---|---|
| 150 min | ≈ 44 min | ≈ 2 min | ≈ 42 min |
| 300 min | ≈ 128 min | ≈ 3 min | ≈ 124 min |

A typical change costs 10–30 min, so it does not offset this. The "no re-boarding the same
linkage" rule (the current expanded-state engine) blocks it on one line only. Every per-leg
tolerance that rises with ride length has this property; a per-minute rate depending on comfort
alone does not, but is not per-leg sensitivity. Relief from discomfort during a break cannot be
represented in a precomputed table. The current expanded-state engine enforces the rule (→
[engine-semantics](engine-semantics.md)).

## Stop comfort details

- Facility data: a new halt attribute derived from its buildings (shelter, waiting room, lounge,
  hotel → tolerable wait); deterministic at load, so no savegame field is needed [UNVERIFIED]. The
  roadmap entry "Waiting time limits and stop facilities" also proposes per-category limits (for
  example livestock pens), a monthly warning to the stop's owner, and revenue for extension
  buildings from waiting passengers.
- Compute the penalty over the 32 waiting samples, not from their mean: a hinge on the mean hides
  long waits (samples 10 and 50 min at a 30-min stop average to no penalty).
- Hard limits apply to mean waits, not to individuals; the runtime give-up time is global today.
  A give-up time derived from stop comfort would match the routing limit and would also replace
  the speed-bonus rule.
- Limits switch routes on and off when a mean wait crosses them; crowding raises waits, so routes
  can oscillate between passes. Hysteresis or a steep soft penalty inside the limit prevents this.
- An edge from a stop serves both the first boarding and transfers there. One value per cell
  treats origin waits like transfer waits (consistent with the simulation, where generated
  passengers wait at the stop), so a hard limit would also stop trips starting at that stop.
  Different origin treatment needs a stored first-edge component, or a second table for exact
  origin-optimal routes.
- Known limitation: passengers are generated at random times, so measured waits for infrequent
  timetabled services are long; real passengers plan their arrival [RECOLLECTION:2026-11-04 known
  issue]. Per-packet routing could mitigate this but is not feasible at scale (→
  [per-packet-routing](per-packet-routing.md)).

## Companion behaviour and through journeys

- Routes store only the next stop, and boarding takes the fastest convoy to it. Boarding must
  compare perceived costs (including the convoy's comfort) or prefer the connexion's best line, and
  passengers must stay aboard where the route continues on the same convoy (→
  [engine-semantics](engine-semantics.md) Example A); otherwise realised stop and comfort costs
  differ from routed ones.
- Through journeys: stop penalties make correct deboarding more critical [RECOLLECTION:2026-11-04]. Through
  edges take the comfort of the portion the passenger stays in and count as one leg; through
  passengers pay no stop terms during the dwell. Composition beyond the depth limit would have to
  remove the coupling stop's terms from the second cell, which a blended cell does not hold, so it
  is approximate; direct through edges up to the depth limit are exact.

## Sequencing against the optimisation work

Comfort weights are computed in the connexion rebuild; the routing-search kernel is unchanged, so the
weight/scales/pakset/revenue work can proceed in parallel. The cell layout is fixed: one 4-byte
cost field (`finished_times`/`working_times`, currently the aggregate time; it can hold the
blended cost when comfort weights land) and the 2-byte next stop
(`finished_next_transfers`/`working_next_transfers`), with no separate discomfort array. The
comfort cost is one field per cell. The measured memory and performance results support B over S
and uniform over banded sensitivity. Adding a discomfort array later needs a savegame step and one
refresh, but the flat arrays leave room for it [analysis, 2026-11-06].

## Blended (B) or separate (S) storage

- **B:** one 4-byte blended cost and the 2-byte next stop per cell.
- **S:** B plus a 2-byte discomfort total D for the stored route (actual time = cost − D), packed
  beside the next stop.

| Bytes per cell | B (current) | S (alternative) |
|---|---|---|
| Finished table | 6 | 8 |
| Working table | 10 | 12 |
| Savegame | 6 | 8 |

Peak with 5 large compartments: S 1.3 / 5.2 / 20.8 GB, B 1.0 / 4.0 / 16.0 GB at
H = 5k / 10k / 20k. Compute: with the current engine S is within ~1-2% of B (routes are still chosen on
the blended cost; D is written only when a cell improves and is read beside the next stop)
[UNVERIFIED].

Under single-route sufficiency, S's remaining advantages are actual time (for the `fetch_goods`
planned-versus-direct check, displays, tie-breaking) and "too slow" versus "too uncomfortable"
statistics. Both can be had without per-cell storage:
- compare perceived costs in `fetch_goods` (the direct convoy's perceived cost is computable from
  its estimated arrival and comfort);
- for failed passengers only, sum leg discomfort along the next-stop chain from the connexions
  (~1–5 µs per failed passenger; approximate, adequate for statistics);
- drop tie-breaking by actual time; no GUI reads table times.

With these measures, a D array can be added later beside the next-stop array (savegame version
step and one refresh) without changing the kernel's comparison.

## Individual comfort sensitivity

Recommended: one uniform comfort weighting in route choice and tolerance, individual variation
only in time tolerance and at boarding; a comfort-sensitive individual then cannot choose a
different stop-to-stop route. If individual route choice is needed, the options are:
- **bands** of tables (memory and routing-search compute ×k; worst-case cost overstatement
  (s_max/s_min)^(1/2k), e.g. 1.41 at k = 2 for a 0.5–2× spread; each packet stores its band, a
  savegame change);
- **on-demand search** on comfort-only failures (20 µs–3 ms each on the critical path; viable only
  below ~1% of decisions);
- **Pareto sets** of routes per cell (infeasible).

Without bands, the stored route's perceived cost for a passenger with relative sensitivity s is at
most max(s, 1/s) times their optimum. A whole-trip cap has no such bound.

## Revenue changes (decision 7)

- Speed bonus: not used in fares. Its functional use is the give-up rule in `haltestelle_t::step`,
  which needs a replacement (stop-comfort give-up time); also a label in gui/goods_frame_t.cc and
  a comment in player/ai_goods.cc. Keep the pakset field readable for old paksets.
- Comfort weighting: remove the comfort block of `goods_desc_t::get_total_fare`
  (`base_comfort_revenue`, `comfort_derating`, the luxury and discomfort settings). Those settings
  are written by `settings_t::rdwr`, so removal needs savegame compatibility handling (AGENTS.md
  rule 5). `tolerable_comfort` becomes the routing tolerance curve; `max_tolerable_journey` stays
  (class choice, GUI).

## Private-car comfort (decision 8)

- The era-constrained weighted pool exists: `private_car_t::liste_timeline`, rebuilt by
  `private_car_t::build_timeline_list`. `stadt_t::generate_private_cars` spawns ≈ passengers ×
  city traffic level ÷ 1000 visible cars per car trip; each spawned car draws its own type in the
  `private_car_t` constructor.
- Design: draw the car type once per car-owning packet from that pool, use its comfort for the car
  leg (one leg: no split incentive), and pass the type to the spawned car instead of a second
  draw. Cost: one weighted pick per car-owning packet. Car trips are evaluated per packet at
  generation, outside the route tables.
- Sync: a synced draw in the deterministically seeded passenger threads; it changes the random
  sequence and must happen for every car-owning packet. Data: `citycar_desc_t` needs a comfort
  field (pakset, makeobj, reader; default when absent → [data-and-pak](../data-and-pak.md)). A
  packet's passengers share one car type.

## Open questions

- Split incentive: accept with sized boarding costs, use a smaller scale or flatter curve, or
  use a length-independent per-minute rate?
- Scales: the vehicle comfort penalty, stop multipliers per facility, the boarding constant per
  stop, the hard-limit offset X and the simuconf minimum, the default mid-journey walking penalty.
- Does a per-leg vehicle hard limit apply ("will not ride at all if comfort is too low"), and
  with which parameters?
- Origin waits: same multiplier and hard limit as transfer waits, or different?
- Hysteresis for hard limits; is the runtime give-up time derived from stop comfort?
- Stop facility data in the pakset: which buildings, which attribute, per goods category?
- Line comfort for an edge: capacity- or frequency-weighted mean of the line's convoys, or the
  minimum?
- S storage (would add a 2-byte discomfort array and a savegame step).
- Is individual comfort sensitivity in route choice required (bands), or is uniform weighting
  accepted?
- Penalty from waiting samples (recommended) or from the mean?
- Speed-bonus replacement for the give-up rule; compatibility handling for removed revenue
  settings.
- Boarding rule: exact form of the perceived-cost comparison and the stay-aboard rule.
- Private car: comfort field format and default; whether the car draw also sets car speed.
- Target release, and roadmap markers (balance-critical?) for comfort routing and the revenue
  changes.
