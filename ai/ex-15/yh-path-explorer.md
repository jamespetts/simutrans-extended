---
status: draft
verified: ex-15 @ ad753f5c6 (sequencing); design baseline ex-15 @ ad1815c
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

Sub-documents (read-when):

- [current-state](yh-path-explorer/current-state.md) — the code-verified baseline this design
  modifies (explorer structure, budgets, matrix layout, bounds, time sources); read when
  implementing or re-verifying.
- [accuracy-performance-verification](yh-path-explorer/accuracy-performance-verification.md) — the
  worked accuracy oracle, the performance model against virtual concatenation and the per-stage
  verification plan; read when implementing stages 1–5 or assessing performance. Phase-5 kernel
  and SIMD requirements → [path-explorer-optimisation](../path-explorer-optimisation.md).

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
stored transport pair, not a hash-table lookup. If the pair is not a through pair, behaviour is
unchanged. The transport pair is fixed per cluster pair at a transfer halt, so the substitution
is applied once per cluster pair outside the inner loop
(→ [path-explorer-optimisation](../path-explorer-optimisation.md)).

### Dwell time — open, direction recorded

Dwell is the time a consist stands while vehicles are detached, moved, re-attached and checked; it
is excluded from the recorded leg times (see
[current-state](yh-path-explorer/current-state.md)). For asynchronous release followed by
later collection, the arrival-to-departure interval can also include waiting for the collecting
service [FORUM:https://forum.simutrans.com/index.php/topic,17892.msg170661.html]. Whether that
waiting is part of the same recorded dwell, and how either case is reconstructed before measurement,
is open; do not assume that the global shunting delay alone suffices. The primary source is to be
measured per coupling point, because it depends on loading amounts; when no measurement exists the
value is reconstructed from runtime inputs. The exact inputs, history ownership/reduction and
initial-period rule are open. Any measured history is synchronised state; its reduction must be
identical on all peers.

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

## Co-requirements in the same release

Recording through journeys that the runtime cannot execute would misroute cargo. The following
must ship with the explorer change: the dividing runtime with schedule handover (including the
reversed case and tile re-reservation), two-party timed joining with deadlock avoidance and
specified waiting/retry/failure behaviour, freight and passenger continuity across portions,
platform and home-depot handling, and the remaining per-portion consist-order validation
(registry: [schedule-and-consists](schedule-and-consists.md)).

## Implementation stages

1. Baselines: per-phase durations and iteration counts on an active window and on the synthetic
   maps, with the accuracy oracle in
   [accuracy-performance-verification](yh-path-explorer/accuracy-performance-verification.md) as a
   cell-for-cell oracle and byte-identical matrices plus iteration counts as a regression oracle.
2. Edge generation.
3. Cross-boundary condition.
4. Through-journey substitution.
5. Transfer status for coupling stops, the added rebuild triggers, and the depth limit.
6. Runtime co-requirements and remaining consist-order validation (same release).

Per-stage verification method (loopback determinism, performance-suite oracles, refresh timing):
[accuracy-performance-verification](yh-path-explorer/accuracy-performance-verification.md).

### Sequencing against the optimisation work

The through-edge recording and substitution touch phase 2 and phase 5, the same code as the
cell-layout, comparison-loop and threading work. The flat-array cell layout, the dense SSE2/NEON
comparison loop and the phase-5 thread pool are merged into `ex-15` (merge `ad753f5c6`), but the
Y/H hooks (per-cluster-pair substitution in the dense target row, through edges with first !=
last) are not yet present. Their route-table integration follows the final layout/loop/iteration
model, which is now in place; lever 4 is implemented and verified. Y/H remains on the present
engine family (not levers 7/8). The runtime co-requirements, dwell measurement, consist-order data
and pakset work are independent and can start in parallel. [analysis, 2026-11-06; layout/SIMD/
threading status at ex-15 @ ad753f5c6]

## Open questions

- Dwell measurement: exact runtime inputs, whether asynchronous collection waiting is included,
  history ownership (halt, line or convoy), reduction, and the initial-period rule before any
  measurement exists. Direction recorded under Dwell time.
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
the accuracy/performance/SIMD/verification detail moved to the two sub-documents listed above
(user instruction; no content dropped).
