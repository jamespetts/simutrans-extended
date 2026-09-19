---
status: draft
verified: ex-15 @ d4893f372
---
# ex-15: Y/H-shaped path-explorer traversal

**Covers:** the design for discovering and time-estimating journeys that pass through a
schedule split or join; the affected code is path_explorer.{cc,h} (phases
`phase_rebuild_connexions` and `phase_explore_paths`), dataobj/schedule.{cc,h},
dataobj/schedule_entry.h, simconvoi.cc (re-combination runtime), simhalt.cc (connexion tables,
laid-over registry), dataobj/settings.cc + simuconf.tab (the depth limit). Parent registry:
[schedule-and-consists](schedule-and-consists.md). Determinism, threading, performance and SIMD
rules are defined elsewhere and only referenced here.

This document records the agreed design and its rationale. Implementation has not started.
All decisions marked [RECOLLECTION:2026-09-19] were taken by the user in the planning session
of that date.

## Definitions

- **Y-shape** — one convoy divides at a stop into two portions, each continuing on a different
  schedule; a join is the same shape traversed in the reverse direction. Requested in the
  original design post, which states that the explorer would have to traverse the whole schedule
  once for each branch and recursively for sub-branches
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.0.html].
- **H-shape** (also called X in the early posts) — a stop where two convoys both divide and join:
  each takes vehicles from the other while its own stop list is unchanged (for example two
  locomotive-hauled trains exchanging locomotives)
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.0.html post #5].
- **Through journey** — a journey whose passengers or goods remain in the same vehicles across a
  split or join. This is what the explorer must record; a journey requiring a change of vehicle
  is an ordinary transfer that the existing system already handles.

## Code-verified current state

- The explorer computes connexions within one schedule only. `phase_rebuild_connexions` walks each
  operating line or lineless convoy's schedule through `schedule_t::increment_index` (linear,
  cyclic, and the mirrored return pass). It reads only the `set_down_only`, `pick_up_only`,
  `lay_over` and `discharge_payload` entry flags and, on ex-15, the consist-order tables derived
  by `schedule_t::parse_orders` (`get_catg_carried_from`/`..._to`, min-class variants)
  [CODE ex-15 @ d4893f372; CODE master @ dc2fc3fcd].
- No code path in path_explorer.{cc,h} on ex-15 reads `target_id_couple`, `target_id_uncouple`, or
  `target_unique_entry_uncouple`; a search of the ex-15 files for couple/uncouple/divide returns
  nothing in those two files [CODE ex-15 @ d4893f372]. Journeys through a split or join are
  therefore neither discovered nor time-estimated.
- The runtime cannot yet execute a divide: `schedule_t::get_uncouple_target` is a TODO in
  schedule.h; `convoi_t::hat_gehalten` carries `// TODO: Implement logic for dividing`, and a
  failed join is silently ignored (`// TODO: Consider what happens if we fail`). Joining data is
  stored, editable and serialised but the `couple` flag has no setter, so the join path is
  unreachable in normal play (registry: [schedule-and-consists](schedule-and-consists.md)).
- Budget and parity: `limit_set_t` holds per-phase iteration limits that a feedback controller
  recalibrates toward the configured `path_explorer_time_midpoint` (simuconf; default 64 ms). In
  network mode peers write local limits and `nwc_routesearch_t` broadcasts the minimum set, so all
  peers perform identical iteration counts. Phase 2 counts one iteration per linkage; phase 5
  counts one per origin row [CODE master @ dc2fc3fcd; mechanism: [network](../network.md)].
- Rebuild triggers today: schedule and vehicle changes call `haltestelle_t::refresh_routing`
  from simconvoi.cc and simline.cc; halt destruction calls `path_explorer_t::refresh_category`
  from simhalt.cc; a flag-only pass runs every `reroute_check_interval_steps` (simuconf; default
  8192) from simworld.cc [CODE master @ dc2fc3fcd].
- Bound: one schedule holds at most 254 entries (`schedule_t::insert`/`append` cap, message
  "Maximum 254 stops in a schedule!"); the entry count is `uint8`
  (`tpl/minivec_tpl.h`, `schedule_t::get_count`) [CODE master @ dc2fc3fcd; same cap on ex-15].
  The mirrored expansion `(count * 2) - 2` in `phase_rebuild_connexions` already truncates for
  counts of 129 or more on assignment to `uint8`, producing an incomplete mirrored walk (no
  out-of-bounds access, because `increment_index` keeps the position in range) [CODE master @
  dc2fc3fcd].
- Matrix layout: `path_element_t` is an array-of-structs cell of `uint32 aggregate_time` plus a
  halt handle; `transport_element_t` holds two `uint16` transport identifiers; the matrices are
  allocated as arrays of row pointers. The phase-5 relaxation is gather-bound: origin and target
  halt indices come from per-transport cluster member vectors and index the matrices at scattered
  addresses [CODE master @ dc2fc3fcd; SIMD consequence: [simd-applicability](../simd-applicability.md)].
- Journey times come from per line/convoy `average_journey_times` keyed by halt pair, booked in
  `convoi_t::laden` from the interval between the booked departure at the previous stop and the
  arrival here; because departure is booked at departure, the recorded leg time excludes the dwell
  at the stop. A distance-and-speed estimate replaces the table when no history exists. Waiting
  time comes from `haltestelle_t::get_average_waiting_time` (history mean, else half the service
  frequency); walking transfer time comes from `haltestelle_t::calc_transfer_time`
  (`get_transfer_time` / `get_transshipment_time`). The connexion aggregate used to fill the
  matrix is `waiting + journey + transfer` [CODE master @ dc2fc3fcd].

## Design history and its bearing on this work

The design changed materially between 2018 and the implemented ex-15 data structures. The
superseded parts must not be reintroduced:

- Entry targeting: the 2018 plan keyed the target by an 8-bit positional entry index; this was
  replaced by the 16-bit `unique_entry_id` so identifiers survive schedule edits. Consist orders
  are stored on `schedule_t::orders`, keyed by `unique_entry_id`, one per entry
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg202940.html].
- A whole-schedule "list of schedules referring to this one" was proposed early but doubted on the
  forum and is absent from the final data list; it does not exist in code. This plan therefore
  assumes no such list.
- An alternative connection-based model was raised in the dedicated path-explorer thread: compute
  each coupling as a connection with its own transfer time, require uncoupling to precede coupling
  at a stop, and feed per-connection times into the matrix search
  [FORUM:https://forum.simutrans.com/index.php/topic,17892.0.html]. That thread's critique of
  independent per-branch recomputation is the origin of the performance reasoning below. The
  model was never formally adopted; the approach chosen here is compatible with its central point
  (per-coupling times entered into the existing matrix search) without the ordering constraint.
- Freight/passenger continuity across a split was left unresolved: the working assumption is that
  cargo is rearranged internally into the correct portion at the split, not constrained at the
  vehicle level [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg193643.html]. The
  explorer must not assume a vehicle-level continuity constraint.
- The 2018 posts on two-party timed joining via the trigger system
  [FORUM:https://forum.simutrans.com/index.php/topic,16980.0.html] describe a mutual wait that is
  still absent from the runtime.

## The design

Record cross-schedule through journeys as additional edges at the exits of a splitting/joining
section, using the existing matrix search, and substitute the through-movement dwell in place of
the walking transfer when the search composes two through edges [RECOLLECTION:2026-09-19].

### Inputs the explorer may read

The explorer decides what to record from two sources only [RECOLLECTION:2026-09-19]:
1. schedule text as saved (entry flags, target line/convoy identifiers, consist orders);
2. saved operating state (whether the target line exists and has convoys, vehicle coupling
   constraints, platform length, trigger identifiers).

It must not read live convoy positions, current halt laid-over pool contents, or current trigger
bitfield values. This matches the existing behaviour, where the explorer builds its working set at
the start of a pass and does not sample per-step convoy state. A through journey is therefore
recorded whenever the saved data permit it, including at moments when the joining convoy is not
present; the runtime handles absence as waiting, retry, or a specified failure (see
Co-requirements). The recorded set changes only on schedule, line, vehicle or station changes.

### Edge generation (phase 2)

For each entry carrying a split or join flag with a target, record directed through pairs from
stops upstream of the coupling point on the source schedule to stops downstream of it on the
target schedule. Each edge's time is the origin stop's waiting time once, the per-leg journey
times stitched from the operating line/convoy histories (distance-and-speed estimate only when
neither leg has history), and the through-movement dwell at the coupling stop in place of the
walking transfer time. Generation order is deterministic (ordered by schedule and entry
identifier).

### Cross-boundary condition

The category-and-class tables (`get_catg_carried_from`/`..._to`, min-class variants) and the
loading flags (`discharge_payload`, `set_down_only`, `pick_up_only`), layover forced discharge and
range-stop forcing are applied across the coupling boundary by one predicate, stated once and used
both when recording edges and when substituting in the search. A portion that terminates at a
forcing-discharge stop must not be joined to a continuing portion; the continuing sub-portion at
an internal divide is exempt. Non-through directions (for example from the target branch back to
the source branch) are not recorded.

### Through-journey substitution (phase 5)

When the relaxation composes two cells and the stored inbound and outbound transport identifiers
are a recorded through pair, the walking transfer time of the destination halt already present in
the second cell is replaced by the through-movement dwell. The test is a direct lookup on the
stored transport pair, not a hash-table lookup, so the innermost loop stays within its existing
cost class. If the pair is not a through pair, behaviour is unchanged.

### Dwell time — open, direction recorded

Dwell is the time a consist stands while vehicles are detached, moved, re-attached and checked; it
is excluded from the recorded leg times (see current state). The primary source is to be measured
per coupling point, because it depends on loading amounts; when no measurement exists the value is
reconstructed from the same inputs the runtime uses to calculate it. The exact inputs, the
measurement history's ownership and reduction, and the initial-period rule are open (see Open
questions). Any measured history is synchronised state; its reduction must be identical on all
peers.

### Rebuild triggers

The existing triggers are retained unchanged: schedule and vehicle changes via
`haltestelle_t::refresh_routing`, halt changes, and the periodic pass. Added:
consist-order edits and the schedule-entry data they derive from (orders, couple/uncouple
targets, loading flags) become rebuilds at the same call sites; edit sites that do not currently
call the refresh path are to be wired to it [RECOLLECTION:2026-09-19].

### Depth limit

A simuconf setting bounds the number of dividing/joining movements composed into one through
journey. Its default is to be large enough not to act in a normal large game (including the
gargantuan fixture) and to act only in extreme cases [RECOLLECTION:2026-09-19]. Beyond the limit
the journey is recorded up to the limit plus an ordinary transfer for the remainder — a slower
recorded time, not an absent journey. There is no limit on the number of distinct targets recorded
at one stop; only the depth limit exists. Placement: a new integer setting in simuconf.tab read in
settings.cc in the manner of `max_route_steps` / `max_choose_route_steps`, serialised with the
other settings and server-authoritative in network mode.

### Scope

The full feature ships in the 15.x release with no partial versions: splits, joins, H-shape
exchanges, nested splits, and portion-sensitive discharge
[RECOLLECTION:2026-09-19].

## Alternatives considered and rejected

- **Virtual concatenated schedules.** Build the joined stop sequence and run the existing
  per-schedule walk over each. Accurate if per-leg tables and dwell are stitched, but its work
  grows as branching factor to the power of depth times stops squared; it breaks the per-linkage
  iteration accounting used for budget/parity; virtual lengths beyond the 254-entry bound require
  widening the schedule container, its count byte and the save format; and invalidation is
  transitive. Retained only as a fallback if the through-journey substitution fails review or
  parity verification.
- **On-demand onward search at goods-reroute time.** Rejected: the source leg would be scored
  without the onward leg, and the cost moves into the single-threaded halt step, which is not
  budgeted or limit-synchronised.
- **Alight-and-reboard treatment of splits and joins.** Rejected: it is not behaviourally
  equivalent to a through journey. It double-counts waiting and walking transfer, splits capacity
  and serving-transport counts, admits category/class combinations the through portion cannot
  carry, and runs fare apportionment twice.

## Accuracy: worked examples (test oracle)

All times in minutes. These are the expected values for the synthetic maps in Verification.

Average case: source `A-B-C-D-E` half-hourly (wait at `A` 15), target `C-F-G` hourly (wait at `C`
30), walking transfer at `C` 6, through dwell at `C` 3, rides `A->C` 22 and `C->G` 25. True `A->G`
= 15 + 22 + 3 + 25 = **65**. A naive recording that adds a second wait and the walking transfer
gives 98; the design (Edge generation and Through-journey substitution) gives 65.

Extreme case: as average, plus a second divide at `G` (dwell 4, walking transfer 8) to `G-H-I`
(ride `G->I` 18), an H-exchange at `D` (dwell 2, stop lists unchanged), and a forcing-discharge
stop `F` on the `C-F-G` branch. True `A->I` = 15 + 22 + 3 + 25 + 4 + 18 = **87**. A direct
recorded `A->I` edge gives 87. If instead the search composes `A->G` with `G->I`, the second cell's
walking transfer at `G` (8) is used unless the through-journey substitution replaces it with the
dwell (4), giving 91 (+4); the error per intermediate divide is walking transfer minus dwell.
`F->I` as one through movement is invalid because `F` forces discharge. The H-exchange at `D` adds
2 minutes and no new origin pair. Both the cross-boundary condition and portion-sensitive discharge
must yield exactly these cells.

## Performance model (against virtual concatenation)

Let `L` be the number of linkages, `S` the mean expanded stop count, `Y` the number of coupling
entries, and (for virtual concatenation) `b` the branching factor and `D` the followed depth. Base
phase-2 work is about `L * S^2` connexion insertions (each with allocation, waiting lookup, journey
lookup and table comparison). The design adds about `Y * S^2` insertions, localised at coupling
entries, plus about `|inbound| * k` relaxations at each affected transfer. Virtual concatenation
adds about `Y_s * b^D * (D*S)^2` insertions per source. At `b=2, D=2` it is roughly 16 times a
source's base work and at `b=2, D=3` roughly 72 times, so it needs only a modest live density to
dominate a pass; the design grows in proportion to the number of coupling entries. The
through-journey substitution adds one direct lookup and a branch to the phase-5 inner loop.
Neither method changes the number of working halts or the matrix sizes.

## SIMD-compatible layout

The staged position in [simd-applicability](../simd-applicability.md) applies: design the
traversal for a dense contiguous aggregate-time array with conditions held in parallel mask/bonus
arrays, implement scalar first and verify, then add a gated SIMD variant; the gate is whether the
relaxation admits dense vector loads or remains gather-bound. The added edge count does not
exacerbate the gather pattern, and the flat-block and array-of-structs-to-struct-of-arrays hygiene
applies independently of the choice of traversal method.

## Co-requirements in the same release

Recording through journeys that the runtime cannot execute would misroute cargo. The following
must ship with the explorer change: the dividing runtime with schedule handover (including the
reversed case and tile re-reservation), two-party timed joining with deadlock avoidance and
specified waiting/retry/failure behaviour, freight and passenger continuity across portions,
platform and home-depot handling, and GUI wiring for the currently unwired flags
(registry: [schedule-and-consists](schedule-and-consists.md)).

## Implementation stages

1. Baselines: per-phase durations and iteration counts on an active window and on the synthetic
   maps, with the Accuracy section's values as a cell-for-cell oracle and byte-identical matrices
   plus iteration counts as a regression oracle.
2. Edge generation.
3. Cross-boundary condition.
4. Through-journey substitution.
5. Transfer status for coupling stops, the added rebuild triggers, and the depth limit.
6. Runtime co-requirements and GUI wiring (same release).

## Verification

Per stage: a loopback server-plus-client run with a checklist comparison every sync step and zero
divergence; a performance-suite pass-duration and total-iteration comparison (throughput, not CPU
share, because the controller holds CPU share near its target); worst-step hitch against the
midpoint; and load/join full-refresh timing. Method and fixtures:
[performance](../performance.md), [sync-and-determinism](../sync-and-determinism.md),
[threading](../threading.md).

## Open questions

- Dwell measurement: the exact runtime inputs from which dwell is reconstructed, the
  measurement history's ownership (halt, line or convoy), its reduction, and the initial-period
  rule before any measurement exists. Direction recorded under Dwell time.
- Depth limit default value. A starting proposal is 64, to be confirmed against the maximum
  depth actually needed on the gargantuan fixture plus the synthetic worst case.
- Do the ex-15 consist-order edit sites already call the refresh path, or must the call be added
  there? To be checked against ex-15 code at implementation time (this session read ex-15 via
  GitHub only and did not switch branch).
- Exact waiting/retry/failure semantics when a recorded through journey's partner is absent at
  runtime (the Inputs section leaves the recording in place).

## Provenance

Design, decision log and performance/accuracy reasoning: user planning session 2026-09-19
[RECOLLECTION:2026-09-19]. Current-state code claims verified against ex-15 @ d4893f372 (read via
GitHub raw files; the local checkout stayed on master) and master @ dc2fc3fcd (read locally).
Forum design posts as cited above. The superseded 2018 items are recorded only so they are not
reintroduced; live status of the runtime features lives in
[schedule-and-consists](schedule-and-consists.md).
