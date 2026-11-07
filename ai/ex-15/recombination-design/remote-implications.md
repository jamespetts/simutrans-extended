---
status: draft
verified: ex-15 @ bf447619a
---
# ex-15: re-combination — remote implications review

**Covers:** the cross-cutting implications (realism, player cognitive overhead, performance,
anomalies/exploits) of the consist re-combination design decisions, as an independent review of the
analysis recorded in [cargo-continuity](cargo-continuity.md),
[recombination-design](../recombination-design.md) and [yh-path-explorer](../yh-path-explorer.md).
Read before settling the cargo/instance decision or the fare booking model, and before finalising
the through-journey stages. Severity: [H] high, [M] medium, [L] low. Items are analysis, not decisions; code
anchors are [CODE] at the frontmatter value, inferences are [UNVERIFIED].

Provenance: two independent review agents contributed to this file on 2026-11-03 — **Review A**
(the R/C/P/A item series) and **Review B** (the addendum below). Their six conflicts (inter-agent
disagreements and Review B disputes of the earlier recorded analysis in
[cargo-continuity](cargo-continuity.md)) were SETTLED by the user on 2026-11-03; outcomes are
recorded per item in *Conflicts between the two reviews* and inline in
[cargo-continuity](cargo-continuity.md).

## Realism

- **R1 [H] — passengers need load-time portion grouping too.** The analysis exempted passengers
  from load-time placement because they can walk through the train. (a) Pre-gangway stock cannot be
  walked through, which covers most of the game's first century; (b) real practice was
  "through carriages to X" labelling at boarding, i.e. load-time grouping by the same static
  portion-outcome key as freight. Doing that makes separate portion-capacity accounting
  unnecessary: portion-bound passengers board portion-bound coaches and simply do not board when
  those are full. **[CONFLICT — SETTLED 2026-11-03: Review B R7's distinction stands — capacity
  accounting is unnecessary only where the portion plan also commits vehicles at load time; with
  slot alternatives or capacity ranges, deciding which coaches are portion-bound is itself the
  allocation problem. R1's conclusion (load-time placement) stands; its premise does not.]** The
  gate must be symmetric — the continuing portion's passengers must not
  overflow into the split-off coaches either. Trains leaving with empty seats and passengers on the
  platform is correct behaviour but must be legible.
- **R2 [H] — contiguity.** Contents-derived assignment can select an interleaved set of vehicles
  (wagons 2 and 4 to A, 3 to B). A real divide is one cut; extracting a middle wagon is a shunt
  taking much longer. Either load-time grouping must respect the portions' block positions (knowable
  from the two orders plus orientation at the divide, including reversals), or the dwell must scale
  with the number of cuts.
- **R3 [H] — dwell granularity.** One `shunting_time_seconds` cannot distinguish an MU uncouple
  (1–2 min) from detaching a locomotive, running round and attaching another (10–20 min); the
  balance point that loco-hauled operation has realistic turnaround penalties versus MUs is lost.
  The reversing code already classifies MU / run-round / turntable; the dwell model should reuse
  that classification per operation type. Also affects how through journeys are recorded.
- **R4 [M] — fare booking model at coupling events.** The `calc_revenue` lookup failure occurs at
  joins as well as splits. Alternative to "fix the lookup": book the leg at the coupling event
  (mileage apportionment — Railway Clearing House model), which sidesteps it and is the realistic
  multi-operator answer. Trade-offs: concave pakset fares make two legs earn more than one through
  fare (see A1); whichever leg absorbs the coupling dwell, the dwell must appear in some leg's
  journey time or the speed bonus is inflated (leg times currently exclude dwell). The
  alight-and-reboard alternative for through journeys concerns the explorer; its fare clause
  does not settle booking.
- **R5 [M] — loco-less waiting portions cannot move.** Through coaches detached at a through
  platform occupy it until traction arrives; real practice used bay platforms or a pilot shunt.
  Without a pilot mechanism, realistic branch-coach working requires the divide itself to occur in
  the bay — a constraint players will meet as deadlock. State it now: it shapes where divides must
  be placed.
- **R6 [L] — no-timeout versus connection allowance.** The realistic behaviour was neither
  indefinite wait nor abandonment: depart after a connection allowance and leave the late portion
  for the next train, which absorbs it (a waiting portion plus the next train's couple order almost
  supports this). Recorded as the realism alternative to the current no-timeout decision; not a
  request to reopen it.

## Player cognitive overhead

- **C1 [H] — editing (not deleting) the target schedule silently breaks divides.** Reference
  sweeps cover deletion of the target line/convoy. Removing or altering the target schedule's
  consist order at the named entry converts the source's scheduled division into "dump the
  remainder as loose stock", with no warning from another schedule window. A commit-time check for
  entries that are someone's uncouple target is the narrow form of the referring-list question and
  is now justified.
- **C2 [M] — replacement and auto-upgrade invalidate specific-vehicle orders.** The replacer and
  overhaul auto-upgrade change descriptors under the orders; specific-vehicle alternatives then
  stop matching and divides fail silently (feeding A2). Either orders default to rule-based
  matching or upgrades sweep affected orders.
- **C3 [M] — two indistinguishable standing states.** "Portion awaiting traction on a schedule"
  and "loose stock for a named recipient" look identical on the map and both appear as convoys in
  the convoy list; pick-up-goods networks will produce dozens. Needs explicit state display and
  list filtering.
- **C4 [L] — legibility of correct-but-surprising outcomes.** Empty seats with passengers left
  behind (R1), goods piling at origin because the portion has fewer slots, transhipment at the
  divide: all correct, all read as bugs without per-portion load display and bounded messages.

## Performance

- **P1 [H] — explorer refresh storms and flickering branch lines.** The explorer builds connexions
  only for operating lines with convoys, and convoy/line membership changes trigger
  `haltestelle_t::refresh_routing` [CODE]. Two consequences [UNVERIFIED until the trigger path is
  checked]: a branch line fed solely by divided portions has zero convoys whenever its portions are
  merged, so its connexions vanish and reappear across passes; and every divide/join (convoy
  created, convoy destroyed) may trigger a category refresh — with frequent divides the explorer
  may never complete a pass. Both the "has convoys" predicate and the refresh triggers need
  redefining for portion-fed lines before the through-journey stages are finalised.
- **P2 [M] — dormant convoys in the hot lists.** `sync_list_t::sync_step` is the measured #1
  hotspot (23.7% self, a raw list walk; [performance](../../performance.md)). Loose wagons and
  waiting portions are full `convoi_t` objects; if LAYOVER/AWAITING convoys remain in the sync list
  and the per-step convoy iteration, a pick-up-goods network pays per standing vehicle group every
  step. Park dormant convoys out of the hot lists.
- **P3 [L] — grouping cost** is a memoisable per-loading-event filter on existing `fetch_goods`
  scanning; benchmark at busy hubs. Fallbacks feed `check_transferring_cargoes` (the measured
  moderate cost). As analysed in cargo-continuity; nothing further.

## Anomalies and exploits

- **A1 [H] — fare splitting.** If legs are booked at coupling events and pakset fares are concave
  in distance, a trivial divide/rejoin raises revenue for free; the transfer-based version of this
  is throttled by waiting time, which a divide does not incur. Plus the dwell-exclusion speed-bonus
  inflation (R4). The pakset fare curves decide severity — check before choosing the booking model.
- **A2 [H] — captive cargo on failure paths.** Unloading triggers only at a packet's next-transfer
  or destination halt [CODE]. If a divide fails (unsatisfiable order, no traction, invalid
  exchange) and the convoy continues on the original schedule, portion-bound packets are carried
  past the divide and never unload; likewise packets aboard a portion whose target schedule
  changed. Required invariant: at every coupling event, every packet ends either on a portion that
  serves it or discharged at that stop. The success and fallback paths were analysed; the failure
  path was not.
- **A3 [M] — standing vehicles as penalty-free storage.** Cargo aboard loose or waiting vehicles
  is outside the halt: no overcrowding, no waiting-time refund/discard, no halt capacity limit, and
  layover's staff-cost relief makes it nearly free. Factories' in-transit accounting still counts
  it, so an uncollected loaded wagon can throttle a chain indefinitely (no timeout). Favours the
  2018 retrospective-credit model (goods stay at the halt with existing penalties) and the
  "unload unless a named recipient serves them" rule for loaded loose wagons; also argues that
  time in a waiting portion counts in the recorded journey time.
- **A4 [M] — the explorer records authored intent, not feasibility.** Through journeys are
  recorded from saved data even where the orders can never be satisfied (e.g. no traction will ever
  be in the pool) — acceptable only with A2's invariant and with measured dwell/feedback eventually
  de-preferring the route, and the feedback loop for an under-capacity or never-forming portion is
  weak (the through edge uses origin waiting time once). Related: loaded-wagon pick-ups via
  untargeted release are not recorded as through edges (only entries with a flagged target are),
  so that case silently degrades to transhipment at the divide unless recipient-based edges are
  added.
- **A5 [L] — freight portion capacity equals slot count**, so the same origin gating as R1 applies
  to goods; without it, over-demand becomes junction transhipment instead of a visible pile at
  origin. Order-derived capacity is a range when alternatives/rules exist — gate conservatively.
  **[CONFLICT — SETTLED 2026-11-03: Review B R7 correct — alternatives and capacity ranges make
  per-portion capacity instance-dependent even without external joins; conservative gating
  over-refuses. A5's origin-gating conclusion survives as the conservative special case only.]**

## Decision-list consequences

Add as decisions (not sub-questions):

1. fare booking model at coupling events (R4/A1);
2. dwell classification per operation type (R3);
3. the never-carry-past invariant (A2).

Correct these assumptions in [cargo-continuity](cargo-continuity.md): passengers get load-time
portion grouping (R1); contiguity constrains assignment or prices the dwell (R2). P1 is a
pre-condition check before the through-journey stages are finalised. C1 justifies the narrow commit-time form
of the referring-list question.

## Addendum — second review (2026-11-03)

A second independent review of the same decision space. Deduplicated against the items above:
load-time passenger placement (R1), contiguity (R2), dwell granularity (R3), fare booking and fare
splitting (R4/A1), captive cargo (A2), standing-vehicle storage (A3), explorer feasibility (A4),
legibility (C4) and grouping cost (P3) were re-derived and are already recorded. The points below
are additions, qualifications and conflicts only; same tags and severity scale as above.

### The central correction: compatibility is not allocation

- **R7 [H] — grouping solves compatibility, not capacity allocation.** Keeping incompatible loads
  out of one wagon does not ensure the loaded wagons can be allocated to the prescribed portions.
  Two units (100 and 50 seats), 80 passengers for branch A and 70 for branch B, total demand equal
  to total capacity: no allocation carries everyone. The load must relate to the *feasible
  allocation*, and contents selection and vehicle selection are interdependent (each constrains the
  other), not sequential. Rules/alternatives make this ordinary: `vehicle_description_element`
  carries min/max capacity and a specific-vehicle alternative bypasses rules, so a slot's capacity
  is a range, not a count [CODE]. The claim that end-state orders determine capacity independently
  of vehicle selection holds only for fixed, single-vehicle slots.
- **R8 [M] — three compatibility classes, and "either" is slack.** Loads are A-only, B-only, or
  usable by either. Free allocation of "either" loads can consume the only vehicle capacity the
  other class needs. Also, "both portions serve the destination" is not sufficient: one may reach it
  directly and the other only via reversal or circuit. The right compatibility relation is "these
  loads can share a vehicle journey until they leave it", not a destination label and not a portion
  label. A waggon group key must be derived from actual workable continuations, not mere eventual
  reachability.
- **R9 [H] — the constraint propagates upstream through joins and nested divides.** A feeder whose
  schedule contains no divide can still load waggons that a main-line train later splits; loading
  on the feeder is then already constrained by the downstream divide. Likewise loads compatible at
  the first of two nested divides may be incompatible at the second. Therefore "a schedule with no
  divide is completely unaffected" is false (conflict, below), and transhipment fallback plus
  "marshal before loading" cannot repair a wagon the game itself loaded mixed.
- **R10 [M] — passenger and mail refinements to R1.** A gangway within a unit is not a gangway
  between coupled units; some multiple units are indivisible groups; mail is handling work, not
  free walking. The correct abstraction is competent *boarding into the intended portion with that
  portion's capacity enforced* — no individual seat simulation, no free inter-portion rearrangement.

### Fairness, cognition and stock

- **C5 [H] — players must not be charged for avoidable loader misallocation.** Distinguish three
  causes: constraints the player chose; unforeseen disruption after boarding; failure of the
  loader to find a reasonable feasible arrangement of vehicles and loads already present. Only the
  first is the player's operational trade-off. Iteration-order-dependent outcomes, a small early
  consignment blocking a larger compatible flow, or adding a vehicle/alternative breaking a working
  arrangement are loader defects, not poor marshalling. Grounds C4 as a correctness requirement,
  not merely a display one.
- **C6 [M] — two groups can still starve under convoy-level minimum loading.** A skewed flow (most
  traffic for A, small B portion) can leave abundant unusable space while `minimum_loading` is
  unreachable; both branches then lose service and downstream stock wait. "Much weaker than
  per-destination" is right; "weak" is not (conflict, below). The departure policy for
  partially-filling multi-portion consists needs an intelligible rule. Also: the existing
  per-category shortcuts (`skip_catg`, `lines_loaded`) are category-wide [CODE]; a group-filtered
  wagon reporting "cannot fill" would suppress other groups of the same category in the same pass
  unless made group-aware [UNVERIFIED interaction — verify at implementation].
- **C7 [M] — merge capacity is collective; empty stock is not unconstrained.** Two feeders can each
  be feasible alone and jointly exceed the onward portion or pool; priority questions follow
  (already-aboard vs waiting; two incoming through portions; does a late service keep its claims;
  another company consuming relied-upon stock). Contents-driven allocation says nothing about where
  empty waggons, trailers or units go: the system can move every current load and progressively
  starve an origin. This weakens the "no persistence" confidence (conflict, below) and is a network
  planning question, not a per-split one.

### Anomalies

- **A6 [M] — fallback unload is not always available and can loop.** Discharge requires suitable
  stop facilities and reachable vehicles: `hat_gehalten` handles only vehicles within the station
  length [CODE]; a coupling location need not be an interchange. A recovery loop is possible —
  discharge, reroute, board the same nominal through connection, discharge again — so the fallback
  must strictly improve the packet's situation (cf. A2's invariant). Related to R5 (a waiting
  portion can occupy the very platform the fallback needs).
- **A7 [M] — journey history must survive recombination.** Beyond the R4 lookup defect: comfort,
  catering and journey time are taken from the delivering convoy at unload (`calc_revenue` →
  `goods_desc_t::get_total_fare`) [CODE ex-15 @ bf447619a — independently verified:
  `calc_revenue` reads `get_comfort(g_class)` and `get_catering_level(...)` of the delivering
  convoy for the whole leg from `last_transfer` (simconvoi.cc:6213-6219); freight ignores
  comfort/catering (:6243)], so a short final leg in a premium portion can price a long
  uncomfortable one, and late-attached catering can monetise earlier travel. Principle: changing
  convoy identity must not erase or retroactively alter economically relevant journey history;
  earlier operators and infrastructure owners must be paid correctly after the original convoy is
  gone. R4's mileage-apportionment alternative answers this directly.
- **A8 [M] — retrospective loading credit can be gamed by late-arriving cargo.** The 2018 model
  (A3 favours it) credits time stood: a trailer standing ten hours, with cargo arriving one minute
  before its tractor collects it, could receive ten hours' credit. The credit must be the
  intersection of cargo-present and suitable-capacity-available time, and must not double-credit
  across successive loads; grouped loading narrows "suitable" further (the trailer was available,
  but not for that cargo's onward working).

### Performance

- **P4 [M] — loading re-runs per step while loading [CODE ex-15 @ bf447619a — independently
  verified].** The state switch calls `laden()` every sync step in the LOADING,
  AWAITING_TRIGGER, AWAITING_RECOMBINATION, REPLENISHING and WAITING_FOR_LOADING_* states
  (simconvoi.cc:2472-2478) → `halt->request_loading` → `hat_gehalten` for every loading
  convoy at the halt, once per step. A "once per loading event" derived key is therefore recomputed
  every step while convoys wait, not once at arrival; and absence from the hotspot profile (P3)
  establishes neither cheapness nor bounded worst cases on recombination-heavy worlds.
- **P5 [M] — continuation enumeration growth.** The portion-path key through joins and nested
  divides (R9) must be derived across several schedules; "the inputs are static" does not mean the
  derivation is cheap or that its size is bounded. Benchmark the derivation, not just the filter.
- **P6 [L] — the mixedness measurement answers a counterfactual only weakly.** Existing saves'
  wagon loads were produced by the existing ungrouped loader and existing routing; measured
  mixedness is an upper bound at best and cannot settle whether grouping is needed or what the
  capacity-pressure policy should be (cf. the cargo-continuity measurement note).

### Decision-list additions

4. boarding promise/guarantee policy: planned through service with provisioned capacity vs mere
   chance of continuing vs explicitly planned transfer (grounded in the project goal that players
   should make realistic decisions, not mechanics decisions);
5. the automatic-loader competence boundary (C5): what arrangement the loader guarantees, and what
   remains a genuine player trade-off;
6. persistence for service commitments and stock circulation (C7) — a decision, not an assumption
   of none.

### Conflicts between the two reviews — SETTLED by the user, 2026-11-03

Inter-agent disagreements. **Review A** and **Review B** generated the conflicting findings where
marked; where a review agent disputes the earlier recorded analysis instead, the item says so.
All six were settled by the user on 2026-11-03 after a structured walkthrough of both sides and the
session agent's adjudication; the outcome is appended to each item, and
[cargo-continuity](cargo-continuity.md) carries the corrected claims inline.

1. **[CONFLICT — Review B vs recorded analysis, marked in cargo-continuity]** "The key set is
   schedule-level static … needs no instance prediction and no persistence" — Review B R7
   (allocation is instance-dependent even for self-contained splits where slots have alternatives
   or capacity ranges) and C7 (multi-service merge claims and empty-stock circulation may need
   persistent commitments). Recommend marking the claim [UNVERIFIED] until decision 6 is settled.
   → **SETTLED 2026-11-03 (user, adjudication accepted):** compatibility keys remain static;
   capacity is instance-dependent wherever slots have alternatives/ranges (fixed single-vehicle
   slots excepted); persistence for commitments/stock circulation stays an open decision
   (addition 6), not an assumed no.
2. **[CONFLICT — Review B vs recorded analysis, marked in cargo-continuity]** "A schedule with no
   divide is completely unaffected" and "marshal-before-loading is the realistic player answer" —
   Review B R9 (upstream joins and nested divides propagate the constraint; marshalling cannot
   unmix a wagon the loader mixed) and C5.
   → **SETTLED 2026-11-03 (user, adjudication accepted):** Review B correct; both claims withdrawn.
   Corrected claim recorded in cargo-continuity.md: unaffected only where there is no downstream
   divide after joins; the divide-time transhipment fallback carries upstream-loaded mixed traffic;
   the loader-competence boundary (C5) is a correctness requirement.
3. **[CONFLICT — Review B vs recorded analysis, marked in cargo-continuity]** "the stall scenario
   is much weaker" — Review B C6: weaker than per-destination grouping, still capable of starving a
   busy branch under convoy-level minimum loading. Recommend "weaker, not weak".
   → **SETTLED 2026-11-03 (user, adjudication accepted):** "weaker, not weak"; the departure
   policy for partially filled multi-portion consists needs an intelligible rule (C6).
4. **[CONFLICT — Review B and Review A vs recorded analysis, marked in cargo-continuity]** "A
   divide discharge is correctly two fares because the ride genuinely became two legs" — Review B
   A7 (journey history must survive recombination) and Review A R4 (the alight-and-reboard fare
   clause concerns the explorer and does not settle booking) both dispute it. Two tickets is a
   booking-model choice, not an established realism principle. Recommend removing the parenthetical.
   → **SETTLED 2026-11-03 (user, adjudication accepted with addition):** the parenthetical is
   withdrawn. User rule recorded in cargo-continuity.md: where the consist is scheduled through, a
   journey using the through movement is always priced as a through fare — alight-and-reboard
   pricing is decisively wrong. The booking-model choice (fix the cross-schedule lookup vs
   compute-and-apportion per leg, mileage-apportionment style) remains an open decision
   (addition 1), with A7's journey-history principle attached.
5. **[CONFLICT — Review B vs Review A, marked in A5 above]** A5 "freight portion capacity equals
   slot count" — Review B R7: wrong where alternatives/rules give capacity ranges [CODE]; "gate
   conservatively" then over-refuses feasible traffic. A5's origin-gating conclusion survives as the
   conservative special case.
   → **SETTLED 2026-11-03 (user, adjudication accepted):** Review B correct wherever capacity is a
   range; A5's origin-gating conclusion survives only as the conservative special case (fixed
   single-vehicle slots).
6. **[CONFLICT — Review B vs Review A, marked in R1 above]** R1 "Doing that makes separate
   portion-capacity accounting unnecessary" — Review B R7: true only where the portion plan also
   commits vehicles at load time; with alternatives/ranges, deciding which coaches are
   portion-bound is itself the allocation problem. R1's conclusion (load-time placement) stands;
   its simplifying premise does not.
   → **SETTLED 2026-11-03 (user, adjudication accepted):** exactly so — R1's conclusion (load-time
   placement, qualified by R10's gangway notes) stands; the premise that it removes
   portion-capacity accounting does not.
