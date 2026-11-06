---
status: draft
verified: ex-15 @ bf447619a
---
# ex-15: re-combination — cargo and passenger continuity

**Covers:** how passengers, mail and freight are handled when vehicles move between consists at a
split or join; the instance-identity decision (which physical vehicle enters which portion); and the
fare/revenue consequences. Parent: [recombination-design](../recombination-design.md); journey-time
recording across boundaries: [yh-path-explorer](../yh-path-explorer.md). Untagged claims are [CODE]
at the frontmatter value. The independent reviews' disputes of claims here were SETTLED by the user
on 2026-11-03; outcomes are inline and in [remote-implications](remote-implications.md),
*Conflicts between the two reviews*.

## Working assumption and its status

The 2018 working assumption is that cargo is rearranged internally into the correct portion at a
split; it need not have been assigned to its eventual post-split vehicle when it first boarded. The
path explorer must not impose a vehicle-level pre-boarding constraint
[FORUM:https://forum.simutrans.com/index.php/topic,17852.msg170134.html;
RECOLLECTION:2026-09-19]. This does not permit an alight-and-reboard model at the boundary: that
would double-count waiting and walking transfers and run fare apportionment twice
([yh-path-explorer](../yh-path-explorer.md)). NOTE: this abstraction was justified in 2018 by
implementation capacity; that rationale is under reconsideration and the passenger side of the
continuity question is reopened (see Open questions) [RECOLLECTION:2026-11-03 user statement].

Cargo-derived vehicle selection, passenger load-by-routing, internal shuffling and fallback
unloading/reloading were all discussed in 2018
[FORUM:https://forum.simutrans.com/index.php/topic,17892.msg170680.html,
https://forum.simutrans.com/index.php/topic,17892.msg170688.html].

## Code constraints [CODE ex-15 @ bf447619a]

- Cargo packets (`ware_t`) carry final destination, next transfer (`zwischenziel`), origin,
  `last_transfer` (boarding halt, used for revenue distance), class and comfort preference — no
  per-vehicle binding, no accumulated fare history. Unloading runs before consist processing in
  `hat_gehalten`, so only through traffic is aboard when a split executes.
- Revenue is recognised per leg when a packet leaves a convoy: `convoi_t::calc_revenue` computes
  from `last_transfer` plus the delivering convoy's recorded distance/time for the leg, with class
  capped by `class_reassignments` (the existing en-route downgrade mechanism). `calc_revenue`
  searches the delivering convoy's own schedule for the boarding halt; for a through journey across
  a split the boarding halt is on the pre-split schedule, so the lookup fails and revenue falls
  back to a crude distance estimate. **Any stay-aboard design must fix this** — it is required
  work in the continuity package under every variant. SETTLED 2026-11-03 [RECOLLECTION:2026-11-03
  user statement]: where the consist is scheduled through, a journey using the through movement is
  always priced as a through fare — alight-and-reboard pricing is decisively wrong. How the
  through fare is computed and apportioned across portions and operators (fix the cross-schedule
  lookup vs compute-and-apportion per leg, Railway Clearing House mileage style) remains an open
  booking-model decision (remote-implications R4/A1/A7).
- Minimum-loading stipulations are convoy-level (`convoi_t::loading_level` is the aggregate fill
  percentage; `loading_limit` comes from the entry's `minimum_loading`). Destination-grouped
  loading interacts with them via convoy-level capacity fragmentation, not per-wagon fill levels.
- Loading at arrivals (`fetch_goods`/`load_cargo`) is absent from the measured hotspot profile
  (gargantuan fixture, 2026-09-10; [performance](../../performance.md)); the nearest measured cost
  is halt cargo handling (`check_transferring_cargoes` ~2.6% self), which is what junction
  fallbacks would feed. No profile of a recombination-heavy world exists, so benchmark rather than
  assume.
- Loading already decides per packet, not per convoy: `haltestelle_t::fetch_goods` heap-sorts the
  category's waiting packets by arrival time (three class passes), then walks the convoy's
  remaining schedule (per-entry halt cache) to check the convoy serves each packet's next transfer
  or destination, and compares this convoy's arrival time against alternatives. Vehicles are filled
  in consist order with no destination grouping within a wagon beyond `mixed_load_prohibition`
  (goods type only). A grouping rule is therefore a local filter added to this existing selection,
  not new machinery (see "Grouped loading — refined scope" below); its scan cost at busy hubs must
  be benchmarked. [Review B P4/P5 qualification: "local filter" understates it — loading re-runs
  every step while a convoy is in the LOADING state, and cross-schedule continuation derivation
  grows with joins and nested divides; see
  [remote-implications](remote-implications.md).]

## Realism constraints [RECOLLECTION:2026-11-03 user statement]

- Physically unloading and re-boarding costs time even for passengers (minutes, against a minute
  or two for an uncouple) and consumes waiting capacity at the divide stop; walking through a
  gangwayed train en route is free. The design must distinguish the two, not price them alike.
- If the through portion is full, either would-be through passengers are not boarded at the origin
  (portion-aware capacity at loading) or the excess is discharged at the divide (junction halt
  capacity/overcrowding consequences, including the existing overcrowd-discard-with-refund path in
  `vehicle_t::unload_cargo`). SETTLED 2026-11-03 (Review B R7 correct): portion capacity is
  statically knowable only where the orders use fixed single-vehicle slots; slot alternatives/rules
  make capacity a range and instance-dependent even without external joins, so static boarding
  gating covers only the fixed-slot case.

## Grouped loading — refined scope [RECOLLECTION:2026-11-03 user statement]

The grouping rule is narrower than "one next transfer per wagon":

- It applies only when the consist's schedule contains a divide at which the packet would still be
  aboard. Packets that leave at or before the divide are neutral filler and may share any wagon.
  SETTLED 2026-11-03 (Review B R9 correct): a schedule with no divide is unaffected only if there
  is also no downstream divide after its joins — a feeder with no divide of its own can load wagons
  that a later main-line train splits, and the constraint cannot be applied upstream (which onward
  train a wagon will join is unknowable), so upstream-loaded mixed wagons raise the
  transhipment-fallback rate at the downstream divide.
- The grouping key is the packet's **portion outcome at that divide** — which portion's schedule
  serves its next transfer — not the literal next transfer. Freight for two different destinations
  on the same portion shares a wagon freely. For a simple Y split there are at most two groups.
  [Review B R8 qualification: "the destination is served" must mean a workable continuation
  (direct vs circuitous vs reversed), and loads usable by *either* portion form a third class whose
  allocation is not free.]
- The compatibility key set is schedule-level static: it derives from the two consist orders and
  the serves predicate and is computable once per loading event. SETTLED 2026-11-03 (Review B
  R7/C7/P5 correct in part): portion *capacity* is instance-dependent wherever slots have
  alternatives or capacity ranges, even without external joins; cross-schedule continuation
  derivation grows with joins and nested divides (benchmark the derivation, not just the filter);
  and whether service commitments/stock circulation need persistence is an open decision, not an
  assumption of none. Also note Review B P4 [CODE verified ex-15 @ bf447619a]: loading re-runs
  every sync step while a convoy is in the LOADING state, so "once per loading event" values need
  explicit caching.
- Nested divides: either group by the portion path through all future divides (same predicate,
  finer partition, still static) or accept transhipment fallback at nested divides — sub-question.
  [Review B R9 note: the constraint also propagates upstream, beyond the divides in this schedule.]
- Wagons joining at the divide (couple/pool) were loaded under another schedule and may be mixed:
  the transhipment fallback applies to them. SETTLED 2026-11-03 (Review B R9/C5 correct):
  marshal-before-loading is not a sufficient answer — the loader itself can mix wagons the player
  cannot un-mix, and players must not be charged for avoidable loader misallocation (the
  loader-competence boundary, remote-implications C5, is a correctness requirement, not display
  polish).
- If the target schedule changes en route, the divide-time assignment re-reads contents against
  current data and falls back if unservable — grouping can be stale, never wrong in its favour.

Because the key is per portion-outcome (two groups in the common case) and neutral short-hop
freight fills any wagon, capacity fragmentation shrinks from per-destination to per-portion-class.
SETTLED 2026-11-03 (Review B C6 calibration accepted): the stall scenario is weaker, not weak —
a skewed flow (mostly portion A traffic, small B portion) can still leave unusable space with
convoy-level `minimum_loading` unreachable, starving both branches; the departure policy for
partially filled multi-portion consists needs an intelligible rule.

## Open questions

Cross-cutting implications of the options below (realism, cognitive overhead, performance,
anomalies): [remote-implications](remote-implications.md) — R1 and R2 correct assumptions in this
question (passengers get load-time grouping too; contiguity); R3, R4 and A2 add decisions (dwell
classification, fare booking at coupling events, the never-carry-past invariant). Its 2026-11-03
addendum adds R7–R10, C5–C7, A6–A8, P4–P6 and three further decisions (boarding promise policy,
loader competence boundary, persistence for commitments/stock circulation). The six inter-agent
conflicts generated by the two reviews were SETTLED by the user on 2026-11-03; outcomes are inline
here and in remote-implications.md.

- **Instance identity and cargo continuity** — REOPENED 2026-11-03 [RECOLLECTION:2026-11-03 user
  statement]: the 2018 lightweight abstraction (orders assign vehicles; passenger position
  abstracted) is no longer assumed for passengers. Candidate shapes: (i) full abstraction with a
  free reshuffle at the split; (ii) contents-derived assignment for freight (a wagon follows its
  load; unservable loads tranship at the divide) with abstraction only for passengers/mail;
  (iii) contents-derived assignment for all vehicles, with a free en-route-walk correction for
  passengers/mail up to the portion's capacity and genuine discharge (with its time/capacity/fare
  costs) beyond it. Decide with freight continuity, not as a standalone vehicle-numbering feature
  [FORUM:https://forum.simutrans.com/index.php/topic,16980.0]; any persistent marking engages the
  savegame/network rules before implementation. The `calc_revenue` cross-schedule leg fix recorded
  above is required under every stay-aboard variant. Sub-questions:

  - the mixed-wagon/mixed-group tie-break rule (sync-relevant, must be exact);
  - whether loaded wagons may become loose stock with loads aboard, and whether a collecting
    convoy may take only loaded wagons it can serve;
  - mandatory-slot behaviour when no content-compatible wagon exists (empty-if-permitted vs order
    failure — interacts with the empty-slot question in
    [schedule-and-consists](../schedule-and-consists.md));
  - fallback message policy (bounded: one per consist-order execution per category, not per
    packet);
  - mail's treatment (freight-like grouping is realistic — mail is sorted — but a light hand-carry
    correction is defensible);
  - the portion-capacity boarding rule (realism constraints above);
  - whether grouped loading is needed at all, and at what threshold (measure per-wagon load
    mixedness in a real save first; note divides do not exist yet, so distinct-next-transfer
    counts per wagon are an upper bound — collapse them to portion classes per
    "Grouped loading — refined scope" before judging);
  - **grouped loading under capacity pressure** [RECOLLECTION:2026-11-03 user statement]: when no
    wagon with a matching portion-class group and no empty wagon has space, either the goods wait
    at the halt (honest and visible as building stock; but loading stipulations are convoy-level,
    so the unusable reserved space lowers the convoy's aggregate loading level, and a high
    `minimum_loading` on a many-destination flow can stall the train) or mixing is permitted (and
    the divide transhipment fallback returns). With portion-class grouping rather than
    per-destination grouping the stall scenario is weaker, not weak (settled calibration, see
    "Grouped loading — refined scope") — the mixedness measurement informs the residual choice;
  - freight grouping and contents-derived assignment stand or fall together: assignment without
    grouping means routine transhipment of mixed wagons; grouping without contents-assignment buys
    nothing [UNVERIFIED].
