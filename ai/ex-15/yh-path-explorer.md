---
status: draft
verified: ex-15 @ 2debf73ed (2026-11-15, consist-order check, facing-neutral picking and failure warning); ex-15 @ 684d570f9 (implementation, GUI and explorer tests) plus working-tree recombination runtime (2026-11-13); routing-search engine @ a203862c1 (2026-11-07); Y-division design revised 2026-11-07; discharge boundary and paired runtime 2026-11-13; through-wagon model 2026-11-17; loose-stock recipient at pickup @ b501f13f3 (2026-11-15); paired reload fixture @ fa85a59b8 (2026-11-14); ex-15 @ 9d2b75ecea (2026-11-20, runtime through-pair recording); ex-15 @ e74fb897d0 (2026-11-20, committed through-pair script accessors and test case); ex-15 @ 57435c9cb1 plus working-tree passenger loose-unload fix (2026-11-22, passenger and goods continuity resolved by the two-system design)
---
# ex-15: through-journey path-explorer traversal

**Covers:** the design for discovering and time-estimating journeys that pass through a
schedule split or join; the affected code is path_explorer.{cc,h} (the functions
`phase_rebuild_connexions` and `phase_explore_paths`), dataobj/schedule.{cc,h},
dataobj/schedule_entry.h, simconvoi.cc (re-combination runtime), simhalt.cc (connexion tables,
laid-over registry), dataobj/settings.cc + simuconf.tab (the depth limit). Parent registry:
[schedule-and-consists](schedule-and-consists.md). Determinism, threading and performance
rules are defined elsewhere and only referenced here.

**Terminology.** The design was originally described as "Y/H-shaped": a **Y-shaped** service
has one convoy that divides into two portions, each continuing on a different schedule; an
**H-shaped** service has two convoys that both divide and join at the same stop, each taking
vehicles from the other. The routing concern in both cases is the **through journey**:
passengers or goods that stay aboard the same vehicles across the coupling. This doc uses
"through" for the journey and "Y/H-shaped" only for the service shape; the definitions below
are the authority.

The connexion-rebuild exploration and the GUI are implemented. The Y division (split), the
one-sided coupling receive and the atomic two-party H exchange are implemented. A line-bound convoy
records the actual through pair at each arrival, into its line's `average_journey_times`; a
lineless convoy keeps the accumulated-leg fallback. All
decisions marked [RECOLLECTION:2026-09-19] were taken by the user in the planning session of
that date.

Sub-documents (read-when):

- [current-state](yh-path-explorer/current-state.md) — the code-verified baseline this design
  modifies (explorer structure, budgets, matrix layout, bounds, time sources); read when
  implementing or re-verifying.
- [accuracy-performance-verification](yh-path-explorer/accuracy-performance-verification.md) — the
  worked accuracy oracle, the performance model against virtual concatenation and the per-stage
  verification plan; read when implementing stages 1–5 or assessing performance. routing-search engine
  requirements → [path-explorer-optimisation](../path-explorer-optimisation.md).

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
- **Through pair** — the ordered pair of halts `(from, to)` under which one recorded average
  journey time is stored. `from` is the halt at which a packet boarded its vehicle and `to` is a
  halt at which that vehicle arrives with the packet still aboard. For a journey that crosses a
  coupling, the pair therefore spans a stop on the source schedule and a stop on the destination
  schedule.
- **Line-bound convoy** — a convoy whose `convoi_t::get_line()` is bound, that is, one that runs
  for a line. A convoy with no line is lineless.
- **Linkage** — one line or one lineless convoy whose schedule the explorer walks, held as the
  `linkage_t` pair of a `linehandle_t` and a `convoihandle_t` (path_explorer.h); exactly one of the
  two is bound.

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
  independent per-branch recomputation is the origin of the performance reasoning in
  [accuracy-performance-verification](yh-path-explorer/accuracy-performance-verification.md). The
  model was never formally adopted; the approach chosen here is compatible with its central point
  (per-coupling times entered into the existing matrix search) without the ordering constraint.
- Freight/passenger continuity across a split was unresolved in the forum design: the working
  assumption is that cargo is rearranged internally into the correct portion at the split, not
  constrained at the vehicle level
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg193643.html]. The two-system design
  implements passenger division allocation and the committed formation
  ([passenger-and-goods-systems](recombination-design/passenger-and-goods-systems.md)). The
  explorer must not assume a vehicle-level continuity constraint.
- The 2018 posts on two-party timed joining via the trigger system
  [FORUM:https://forum.simutrans.com/index.php/topic,16980.0.html] describe a mutual wait that is
  still absent from the runtime.

## The design

Record cross-schedule through journeys as direct connexion records at the exits of a
splitting/joining section, using the existing matrix search. A record's time is the actual
through pair time recorded by the consist that carries the passengers or goods across the
coupling, including the coupling stop. A line-bound convoy records that time at runtime (see
Recording the through pair at runtime); when no time is recorded, the record uses the accumulated
legs.

### Inputs the explorer may read

The explorer decides what to record from two sources only [RECOLLECTION:2026-09-19]:
1. schedule text as saved (entry flags, target line/convoy identifiers, target entry identifiers,
   direction flags, consist orders);
2. saved operating state (whether the target line exists and has convoys, vehicle coupling
   constraints, platform length, trigger identifiers), including the saved line/convoy
   `average_journey_times` tables.

It must not read live convoy positions, current halt laid-over pool contents, or current trigger
bitfield values. This matches the existing behaviour, where the explorer builds its working set at
the start of a pass and does not sample per-step convoy state. A through journey is therefore
recorded whenever the saved data permit it, including at moments when the joining convoy is not
present; the runtime handles absence as waiting, retry, or a specified failure (see
Co-requirements). The recorded set changes only on schedule, line, vehicle or station changes.

### Connexion rebuild

For each coupling entry, record directed through connexions from stops upstream of the coupling
point on the source schedule to stops downstream of it on the destination schedule.

- **Uncouple entry.** The source is the schedule carrying the entry; the destination is
  `target_id_uncouple`; the destination entry is `target_unique_entry_uncouple`; the destination
  direction is `uncouple_target_sch_is_reversed`. This is exact from saved data.
- **Couple entry.** The source is `target_id_couple`; the destination is the schedule carrying the
  entry. The source entry is `target_unique_entry_couple`; the source direction is
  `couple_target_sch_is_reversed`. If the entry is unset, fall back to the source's paired
  uncouple entry at the same stop, which names the receiver and its entry. If neither resolves and
  the source schedule visits the stop exactly once in its expanded walk, use that visit.
  Otherwise the coupling is ambiguous and no record is generated.
- A couple entry whose source has a paired uncouple entry naming this schedule describes the same
  through pair as that uncouple entry. Generate it once, from the uncouple side.

Each record's time is:

1. the actual pair time `(origin stop, destination stop)` from the destination linkage's
   `average_journey_times`, when recorded; otherwise
2. the sum of the consecutive leg values from the origin stop to the destination stop across both
   schedules, using the existing fallback (recorded consecutive pair, else distance-and-speed; the
   layover/discharge transfer penalty applies exactly as it does within one schedule).

A line-bound destination linkage's table is filled at runtime by `convoi_t::laden` (see Recording
the through pair at runtime). A lineless convoy's own table never receives a through pair, so a
lineless destination linkage always uses the second source.

The origin stop's waiting time and transfer time are stored as for any other connexion. An
actual pair time already includes the interval at every intermediate stop, and the
accumulated-leg fallback omits it exactly as it does at every other intermediate stop.

The record is owned by the linkage on which the passenger or goods board at the origin (the source
linkage). Nested couplings extend the destination walk through further uncouple entries, up to the
depth limit. Generation order is deterministic (schedule order, then entry identifier).

### Cross-boundary condition

The category-and-class tables (`get_catg_carried_from`/`..._to`, min-class variants) and the
loading flags (`discharge_payload`, `set_down_only`, `pick_up_only`), layover forced discharge and
range-stop forcing are applied across the coupling boundary by one predicate, used when recording
the through connexions. A portion that terminates at a
forcing-discharge stop must not be joined to a continuing portion; the continuing sub-portion at
an internal divide is exempt. Non-through directions (for example from the target branch back to
the source branch) are not recorded.

### Routing search

A through connexion is a direct candidate in the full connexion-rebuild list, like any other
connexion, and the expanded-state per-origin Dijkstra consumes it as one edge whose transport
is the boarding linkage. The recorded time already includes the coupling stop (point 1 above),
and the accumulated-leg fallback treats the coupling stop like any other intermediate stop
(point 2). The routing search consumes the recorded time directly; iteration accounting is in
origins.

### Coupling data added for the explorer

`target_unique_entry_couple` (`uint16`) and the `couple_target_sch_is_reversed` flag were added to
`schedule_entry_t` so that a couple order identifies the source schedule's entry and direction,
mirroring the existing uncouple fields. The savegame revision is 8. The schedule window now
exposes a couple target-entry picker and a "Target schedule is reversed" control for each side,
enabled only when the target schedule is mirrored; changing the target clears the stored entry id
and direction. These controls are compile-verified but have not yet been exercised in an
interactive game. The picker window id is shared with the uncouple picker
(`magic_uncouple_entry_picker`), so opening one closes the other; a distinct id would require a
change in `gui/simwin.h`.

### Recording the through pair at runtime

`convoi_t::laden` records the pair when a consist arrives with a vehicle whose `through_departures`
record is present. The first halt is the record's boarding halt; the second is the halt at the
convoy's position at that arrival. The sample is
`arrival_time - record.departure.departure_time`, converted to tenths of minutes, and it is added to
the line's `average_journey_times` (for a through journey that crosses a coupling, the destination
line's table). The start of the interval depends on how the record was made. When
`convoi_t::capture_through_departures` found the boarding halt's departure in `convoi_t::departures`
(a `departure_data_t` record with a non-zero `departure_time` or overall distance), the interval
starts at that `departure_time`, which was booked at departure; a pair booked from the schedule starts
its interval the same way, so the dwell at the boarding halt is excluded. When
`convoi_t::capture_through_departures` found no such record and the vehicle was still at the boarding
halt, it set `departure_time` to the packet's `ware_t::arrival_time`, so the interval starts at the
packet's arrival and includes its wait at that halt before departure.

Only a line-bound convoy records, and it writes only to the line's table, never to the convoy's own
table. A lineless convoy records no through pair, so a lineless destination linkage keeps the
accumulated-leg fallback above. The recording runs at each scheduled arrival with a non-zero journey
distance, while the convoy is in `LOADING`.

One arrival contributes at most one sample per boarding halt, so several vehicles that boarded at
the same halt do not weight the pair several times. A record is skipped when
`record.history_known` is false, when `record.departure.departure_time` is zero, when its boarding
halt is the arrival halt, when `arrival_time - record.departure.departure_time` is not positive, when
the sample is zero (samples are whole tenths of minutes and the explorer treats a zero time as
absent), or when the boarding halt no longer exists.

The record is not consumed and revenue is unaffected. The vehicle's `through_departures` record
remains the revenue source: the same `convoi_t::laden` call advances its accumulated distance, and
`convoi_t::calc_revenue` uses that distance and the per-player way distances before it falls back to
the delivering convoy's schedule. `vehicle_t::prune_through_departures` removes a record only when no
packet that boarded at that halt remains aboard the vehicle.

### Verification status

[EXECUTION-VERIFIED:2026-11-07, Profile (server) x64, `month-boundary.sve`] A temporary
verification hook chose line pairs, injected through pair times, ran full unbudgeted refreshes
and checked the origin halt's connexion and the matrix cell. Seven modes passed:

- mode 0, uncouple forward: record owner line 372, origin 2243, coupling 9, destination 37,
  injected 1, matrix 18 = direct 18;
- mode 1, one-sided couple through `target_unique_entry_couple`: the same pair and values;
- mode 2, reversed target (`uncouple_target_sch_is_reversed`): line 37, origin 2027, coupling
  2026, destination 2577, matrix 18 = direct 18;
- mode 3, mirrored source reverse pass: line 314, origin 2413, coupling 2414, destination 2803,
  injected 2, matrix 2 = direct 2 (the reverse-pass record was generated and used);
- mode 4, nested L1 372 -> L2 64 -> L3 243: the direct origin 2243 to destination 1867 record
  exists at depth 64 and is absent at depth 1, while the first-level record remains;
- mode 5, broken target (id 0): no through record;
- mode 6, blocked coupling (`lay_over`): no through record.

The uncouple scenario was repeated after the other modes and returned the same record and matrix,
a within-process determinism check. The temporary hook is not in the code; the results above are
the verification record. Not yet covered: the category/class consist-order predicate,
cross-process determinism of saves containing couplings, and interactive GUI operation.

The recombination runtime fixture is described in
[scripting-and-tests/consist-recombination](../scripting-and-tests/consist-recombination.md). It is
the runtime fixture for recombination, not for the explorer: its cases exercise the division, paired
exchange and choose-signal call-on paths, its reload cases exercise the loopback join, the
save/transfer/reload and a fresh offline load of the preserved join save, and its `through-pair` case
asserts the line-level recording described above.

The recombination cases do not themselves cover cargo or passenger continuity; the pick-up goods
cases in the same fixture cover the cargo side. A vehicle that remains in a scheduled consist,
including one transferred directly between the two convoys of a pair, keeps its load, and a loaded
vehicle that becomes loose stock also keeps its load under the through-wagon model (see Loose
vehicles and discharge). Revenue uses the vehicle's `through_departures` record, which
`convoi_t::calc_revenue` tries before the delivering convoy's schedule, so a boarding halt that is
not on the receiving convoy's schedule does not defeat the fare. The rules, the loading credit and
the open questions are in [cargo-continuity](recombination-design/cargo-continuity.md).

### Loose vehicles and discharge

The discharge boundary is that a vehicle which remains in a scheduled consist keeps its load, and a
loaded vehicle that becomes loose stock also keeps its load: the **through-wagon model**, in which the
wagon is the unit of collection [RECOLLECTION:2026-11-17 user decision]. The boundary, the loading
credit, the capacity consequence and the collection rules are in
[cargo-continuity](recombination-design/cargo-continuity.md). The Y division implements the boundary
by deferring the discharge of the split candidate until promotion fails, so a promoted portion keeps
its load; a paired exchange discharges only vehicles that leave both final consists. The through
connexions in this document assume the boundary.

A newly loose loaded vehicle is unloaded only for packets whose destination is the stop, because
`convoi_t::unload_cargo_for_stop` passes `destinations_only` for newly loose stock. The remaining load
waits in the laid-over pool aboard the wagon, and the vehicle's `through_departures` record stays with
it, so the load and its journey history survive a save and reload. An explicit `discharge_payload`
entry is a full discharge: the packets go to the halt, the completed leg is booked, and they become
ordinary halt cargo.

The explorer generates through connexions only from couple and uncouple entries, so a collection by a
targetless consist order is not itself a through connexion. A journey that continues by collection
nevertheless gains a line-level pair time when the collecting convoy is line-bound: on its arrival
with that vehicle, `convoi_t::laden` records the pair `(original boarding halt, arrival halt)` against
the collecting line's table.

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

The full feature ships in the 15.x release as one package: splits, joins, H-shape
exchanges, nested splits, and portion-sensitive discharge
[RECOLLECTION:2026-09-19].

## Alternatives considered and rejected

- **Virtual concatenated schedules.** Build the joined stop sequence and run the existing
  per-schedule walk over each. Its work grows as branching factor to the power of depth times
  stops squared; it breaks the per-linkage iteration accounting used for budget/parity; virtual
  lengths beyond the 254-entry bound require widening the schedule container, its count byte and
  the save format; and invalidation is transitive. Retained only as a fallback if direct pair
  recording fails review or parity verification.
- **On-demand onward search at goods-reroute time.** Rejected: the source leg would be scored
  without the onward leg, and the cost moves into the single-threaded halt step, which is not
  budgeted or limit-synchronised.
- **Alight-and-reboard treatment of splits and joins.** Rejected: it is not behaviourally
  equivalent to a through journey. It double-counts waiting and walking transfer, splits capacity
  and serving-transport counts, admits category/class combinations the through portion cannot
  carry, and runs fare apportionment twice.

## Co-requirements in the same release

Recording through journeys that the runtime cannot execute would misroute cargo. The following are
required for the feature: the dividing runtime with schedule handover (including the reversed case
and tile re-reservation), two-party timed joining with specified waiting/retry/failure behaviour,
freight and passenger continuity across portions, and platform and home-depot handling (registry:
[schedule-and-consists](schedule-and-consists.md)). The division and handover, including the
reversed case and tile re-reservation, are implemented and pass the runtime fixture; the paired
exchange runtime is also implemented and passes its fixture. Freight continuity and the retention of a
load in a vehicle that changes consist follow the through-wagon model and the loading credit in
[cargo-continuity](recombination-design/cargo-continuity.md); passenger portion grouping remains
outstanding. The editor checks each list for completeness, power and valid ends and shows which part
of a divided or joined train it describes. The remaining co-requirements are outstanding.

## Implementation stages

1. Baselines: per-step durations and iteration counts on an active window and on the synthetic
   maps, with the accuracy oracle in
   [accuracy-performance-verification](yh-path-explorer/accuracy-performance-verification.md) as a
   cell-for-cell oracle and byte-identical matrices plus iteration counts as a regression oracle.
2. Connexion generation: uncouple entries exact; couple entries from the new source-entry field,
   the paired uncouple entry, or the single-visit fallback.
3. Cross-boundary condition.
4. Transfer status for coupling stops, the added rebuild triggers, and the depth limit.
5. GUI controls: a couple target-entry picker and "Target schedule is reversed" controls for both
   sides. Implemented and compile-verified; not yet exercised interactively. Additional UI work
   beyond the existing schedule/consist UI.
6. Runtime co-requirements and remaining consist-order validation (same release).

Per-stage verification method (loopback determinism, performance-suite oracles, refresh timing):
[accuracy-performance-verification](yh-path-explorer/accuracy-performance-verification.md).

### Sequencing against the optimisation work

The through-connexion generation touches the connexion rebuild only. A through connexion is a
direct candidate with the same first and last transport, so the routing search consumes it as
one ordinary edge with one transport field. The routing-search engine is the expanded-state
per-origin Dijkstra. The runtime
co-requirements, the journey-time handover, consist-order data and
pakset work are independent and can start in parallel. [analysis revised 2026-11-07; engine/pool
status at ex-15 @ a203862c1]

## Open questions

- Mirrored source schedules: a mirrored schedule visits a coupling entry twice (outward and
  return). The direction flags describe the destination side only. Does the runtime attempt the
  coupling on both passes, in which case the explorer should generate both source-direction sets,
  or is a coupling entry intended for one pass? Settle with the runtime design.
- Depth limit default value. A starting proposal is 64, to be confirmed against the maximum
  depth actually needed on the gargantuan fixture plus the synthetic worst case.
- Do the ex-15 consist-order edit sites already call the refresh path, or must the call be added
  there? Check against the checked-out code at implementation time.
- Exact waiting/retry/failure semantics when a recorded through journey's partner is absent at
  runtime (the Inputs section leaves the recording in place).
- Through-edge capacity: if portion-aware boarding control is adopted (the cargo/instance question
  in [cargo-continuity](recombination-design/cargo-continuity.md)), a through edge's effective
  capacity is the split-off portion's capacity per category/class, not the whole consist's; whether
  recorded edges should carry that, and how it interacts with waiting-time-based routing, is open.
- Loose-stock recipient: the runtime now refuses to let a convoy collect a laid-over vehicle whose
  recorded recipient is neither it nor its line (see
  [schedule-and-consists](schedule-and-consists.md)). The explorer does not read the recipient when
  it generates a through connexion, so it can offer a journey through a later service that the
  runtime will not allow to collect the vehicle. Whether the explorer should exclude such
  connexions, or the recipient should be ignored for path-explorer purposes, is open.

## Provenance

Design and performance/accuracy reasoning: user planning session 2026-09-19
[RECOLLECTION:2026-09-19]. Early code claims retain their inline records; current runtime TODOs and
absence of explorer target reads were rechecked locally at ex-15 @ bf447619a. Superseded 2018 items
are recorded only to prevent reintroduction; live runtime status lives in
[schedule-and-consists](schedule-and-consists.md). Split 2026-11-03: the code-verified baseline and
the accuracy/performance/verification detail moved to the two sub-documents listed above
(user instruction; no content dropped). Revised 2026-11-07 [user decisions]: the through record's
time is the actual recorded pair time, including the coupling stop; the routing search
consumes the through connexion as an ordinary edge (the engine is the expanded-state per-origin
Dijkstra); a join is
generated from the paired uncouple entry, with the new
`target_unique_entry_couple`/`couple_target_sch_is_reversed` fields for the one-sided case. The
data fields and savegame revision 8, the connexion-rebuild generation, the cross-boundary predicate, the
depth setting and the GUI controls are implemented and committed at `f4f6fd2b5`. The connexion-rebuild
generation was verified on `month-boundary.sve` for uncouple, one-sided couple, reversed target,
mirrored-source reverse pass, nested couplings to depth 64, broken target, blocked coupling and
repeated-refresh determinism. The temporary verification hook was removed before commit; the
runner scripts require the test functions to be re-added before they can run again. Interactive
GUI operation and cross-process determinism with couplings present remain to be verified.

The runtime through-pair recording was checked at ex-15 @ 9d2b75ecea (2026-11-20): `convoi_t::laden`
adds the sample to the line's `average_journey_times` for a line-bound convoy only, and
`path_explorer_t::compartment_t::get_recorded_pair_time` reads the destination linkage's table before
falling back to the legs. The `through-pair` case and the two script accessors it uses are committed
at ex-15 @ e74fb897d0 (2026-11-20).
