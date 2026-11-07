---
status: draft
verified: ex-15 @ f4f6fd2b5 (implementation, GUI and tests); routing-search engine @ f9d574ee5 + working tree (2026-11-07); design revised 2026-11-07
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

The connexion-rebuild exploration and the GUI are implemented; the runtime split/join is not. All
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
- Freight/passenger continuity across a split was left unresolved: the working assumption is that
  cargo is rearranged internally into the correct portion at the split, not constrained at the
  vehicle level [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg193643.html]. The
  explorer must not assume a vehicle-level continuity constraint.
- The 2018 posts on two-party timed joining via the trigger system
  [FORUM:https://forum.simutrans.com/index.php/topic,16980.0.html] describe a mutual wait that is
  still absent from the runtime.

## The design

Record cross-schedule through journeys as direct connexion records at the exits of a
splitting/joining section, using the existing matrix search. A record's time is the actual
through pair time recorded by the consist that carries the passengers or goods across the
coupling, including the coupling stop.

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

The runtime requirement remains separate: the consist that arrives at the destination must book
the actual through pair across the schedule handover. `convoi_t::departures` is keyed by schedule
entry index and `convoi_t::set_schedule` clears it, so a handover must carry the pre-split
departure times by halt ID. Until that runtime work exists, the explorer uses the accumulated-leg
fallback for every through pair; tests inject the pair times directly.

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

Recording through journeys that the runtime cannot execute would misroute cargo. The following
must ship with the explorer change: the dividing runtime with schedule handover (including the
reversed case and tile re-reservation), two-party timed joining with deadlock avoidance and
specified waiting/retry/failure behaviour, freight and passenger continuity across portions,
platform and home-depot handling, and the remaining per-portion consist-order validation
(registry: [schedule-and-consists](schedule-and-consists.md)).

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
status at ex-15 @ f9d574ee5 + working tree]

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
