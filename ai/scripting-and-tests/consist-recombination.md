---
status: draft
verified: ex-15 @ 9fdefa153 (2026-11-16, facing case and 35-case set); case outcomes as cited
---
# Consist recombination scripting and tests

**Covers:** `tests/pending-runtime/` (`harness.nut`, `pair-harness.nut`, `choose-harness.nut`,
`cases/`, `pair-cases/`, `choose-cases/`, `reload/`, `pair-reload/`, `choose-reload/`),
`scripts/run-pending-runtime-tests.sh`, `scripts/run-pending-runtime-reload-tests.sh` and
`scripts/run-pending-runtime-reload-tests.ps1`. Parent:
[scripting-and-tests](../scripting-and-tests.md). Feature and runtime:
[consist-recombination](../consist-recombination.md). Design and feature status:
[ex-15/recombination-design](../ex-15/recombination-design.md),
[ex-15/schedule-and-consists](../ex-15/schedule-and-consists.md).

## Terms

The vocabulary used here — target, mover, contact tile, coupling point and approach link — is
defined in [consist-recombination](../consist-recombination.md#terms).

## Scope

`tests/pending-runtime/` is a separate fixture for the recombination runtime: Y division (one convoy
splitting into two portions), paired H exchange (two convoys exchanging vehicles), choose-signal
call-on and a targetless order that turns a vehicle to reverse a consist. It is not part of the
default Squirrel suite
([scripting-and-tests](../scripting-and-tests.md)) and no CI job runs it. Every case runs in its own
game process, so cases cannot contaminate one another.

## Fixture layout

- `harness.nut` builds the infrastructure, the convoy, the target and the consist orders through
  public script interfaces, then owns the Y-division and facing observers and assertions.
- `pair-harness.nut` does the same for two convoys that meet at one stop on two platforms of the same
  halt, with a reciprocal `couple` / `uncouple` entry and an end-state order on each.
- `choose-harness.nut` builds the two-platform junction from `pair-harness.nut` with a choose signal
  and signalbox in the throat, then asserts the choose-signal behaviour through `sign_x.get_state()`,
  `convoy_x.get_working_method()` and `convoy_x.get_vehicle_snapshot()`.
- All three harnesses read a case selector from the `case_kind` global that the case's
  `scenario.nut` sets, and print `RUNTIME CASE <case> PASS` or
  `RUNTIME CASE <case> FAIL: <assertion>`.
- `scenario.nut` selects the forward Y case; `cases/<case>/scenario.nut` selects each further Y
  case; `pair-cases/<case>/scenario.nut` selects each paired case;
  `choose-cases/<case>/scenario.nut` selects each choose-signal call-on case.

## Y-division and targetless cases (`cases/`)

The baseline case is `forward` (the shared `scenario.nut`). It builds a source convoy and the target
line or lineless convoy, authors one end-state order for each portion and starts the convoy. It
observes the created portion's line, schedule, entry, vehicle membership and identity, and then its
normal advance from the target entry. The reversed cases also check the traversal direction.
[EXECUTION-VERIFIED:2026-11-13, Debug|x64 with `-nomidi`]

The further cases are a plain uncouple, a deleted target line, a target entry without an order, an
unsatisfiable order, an unpowered result, collection from the stop's pool, a reversed target
schedule, a lineless target convoy, a mirrored source schedule and a targetless order that
reverses a two-vehicle consist: `plain-uncouple`, `broken-target`, `target-no-order`,
`unsatisfiable-target`, `unpowered-target`, `pool-collection`, `reversed-target`,
`lineless-target`, `reversed-source` and `facing`.

`facing` builds `[retained, turned]`, a two-vehicle consist that is valid as written. Its entry at
the junction carries a targetless consist order for `[turned, retained]`. The turned vehicle is an
unpowered bidirectional vehicle that cannot be at the front in its current orientation; it can be
at the front only after a turn. The runtime must therefore set its `is_reversed` flag and add the
turntable reversal time to the shunting delay. The case asserts the reversed sequence and the flag,
then lets the convoy continue to the east entry.

## Paired-exchange cases (`pair-cases/`)

`pair-harness.nut` gives two lineless convoys whose pair entries lie on two platforms of the same
halt; each entry carries one of `couple` and `uncouple`, names the other convoy and the other
entry's unique id, and has an end-state order. One convoy starts first and must enter
`convoi_t::AWAITING_RECOMBINATION` at its entry; the other then executes the exchange.

The cases cover a symmetric locomotive swap with either convoy started first
(`symmetric-swap`, `symmetric-swap-target-first`), a one-way transfer that leaves a vehicle as loose
stock (`one-way-to-receiver`), an already-satisfied pair (`already-at-end-state`), a joint-solver
backtracking conflict (`conflict-backtrack`), an unsatisfiable pair refused all-or-nothing
(`unsatisfiable`), and a receiver that collects a coach from the stop's laid-over pool while the
emptied donor is destroyed (`pool-fallback`).

## Choose-signal call-on cases (`choose-cases/`)

The cases are:

- `target-present`: a target waiting on the loop platform, with the receiver routed to it and
  admitted under call-on.
- `target-present-ignore-choose`: the same with `ignore_choose` set on the receiver entry.
- `target-absent`: no target present; the receiver uses its free scheduled platform and no call-on
  is issued.
- `target-absent-ignore-choose`: no target present, `ignore_choose` set and the scheduled platform
  occupied, so the receiver waits on the scheduled approach.
- `target-absent-occupied`: no target present, the scheduled platform occupied and a two-tile free
  alternative available.
- `target-arrives-after`: a target started after the receiver has passed the signal, which must
  become the mover.
- `committed-target`: a target still running through the halt towards its stop when the receiver
  reaches the signal; the signal must hold the receiver, record the reciprocal approach link with
  the receiver as the mover, and then admit the receiver to the coupling point.
- `target-platform-blocked`: an unrelated convoy on the target platform approach.
- `moving-block`: a moving-block choose beacon on the 1992 fixture map, retaining `drive_by_sight`
  after call-on.
- `multiple-choose-signals`: two consecutive choose signals, the first selecting the target
  platform.
- `target-unreachable`: a target platform whose usable approach is rejected for its direction
  restrictions.
- `one-sided-layout`: a one-sided receive that takes both target vehicles, so the emptied target is
  destroyed and the receiver's former vehicles must be laid out behind it as loose stock.
- `pair-layout`: a paired exchange reached through the choose signal, whose joint commit must lay
  out the target's final consist on the target's former front slot, the receiver's final consist
  from the target's former rear slot, and the loose stock behind it.
- `unresolvable-order`: a receiver whose order cannot be completed from the two consists, held at
  the signal with no call-on.
- `pair-unresolvable`: a paired exchange whose two orders cannot be solved jointly, held in the
  same way.
- `straddling-contact`: a target whose long rear vehicle's rear end lies in the tile behind its
  front tile, so the receiver must stop at the coupling point in that behind tile.
- `contact-reservation`: the receiver leaves the shared contact tile while the target still holds
  it, so the target must remain the reservation holder.

Assertions:

- The present-target cases assert that the receiver reaches the target's rear tile (the coupling
  tile) and stops at the coupling point; `target-arrives-after` asserts the same for the later
  target, and `committed-target` for the released committed target. `steps` is a vehicle front's
  position within its tile, in vehicle steps. The mover's leading vehicle's `steps` must equal the
  standing rear vehicle's `steps` minus its length in vehicle steps;
  `vehicle_desc_x.get_length()` reports car units, with 16 vehicle steps per car unit.
- `moving-block` keeps its earlier tile expectation because the moving-block beacon does not hold
  the target's reservation.
- `one-sided-layout` asserts the final consist's exact positions and `steps` in the target's former
  slots and that the loose convoy is behind the receiver.
- `pair-layout` asserts the paired commit's layout (the target's final consist on the target's
  former front slot, the receiver's final consist from the target's former rear slot, and the loose
  stock behind it).
- `straddling-contact` asserts a stop at the coupling point in the tile behind the long rear
  vehicle's front tile.
- `facing` asserts the source convoy's final sequence and that the turned vehicle's
  `vehicle_slot_x.reversed` flag is set, so the runtime chose the orientation that makes the
  reversed coupling valid.
- `target-unreachable` asserts no call-on, no entry onto the third platform and a stable
  `WAITING_FOR_CLEARANCE` hold; its runner also requires the unusable-geometry warning in the game
  log.
- `unresolvable-order` and `pair-unresolvable` assert that the choose signal holds the receiver at
  danger with no call-on, and their runner requires the hold warning.
- `contact-reservation` asserts that the target keeps the contact tile's reservation after the
  receiver's last vehicle leaves it.
- The signalling cases use a satisfiable receiver order that restates the receiver's own consist,
  so the receive changes nothing; the two hold cases use an order that cannot be completed.
  `committed-target` and `contact-reservation` use the same no-change order.

## Reload cases (`reload/`, `pair-reload/`, `choose-reload/`)

These are deliberately outside `cases/`, so the baseline runner does not select them. Each runs on a
loopback server, joins a client so that the server saves, transfers and reloads its state
(`nwc_sync_t`), re-asserts the restored state from the loaded world, and then loads the preserved
join save in a fresh offline process and repeats the assertions.

- `reload` runs the forward division, holds after the split-off portion forms, re-asserts the
  divided state in `resume_game()` and advances the portion.
- `pair-reload` runs the paired exchange's one-way transfer, re-asserts both final consists and the
  laid-over vehicle from a string of each expected vehicle descriptor's scalar `vehicle_desc_x`
  properties (`get_power()`, `get_topspeed()`, `get_weight()`, `get_length()` and the coupling and
  electrification flags), because descriptor names are not reliable after a load, and then observes
  both convoys advance.
- `choose-reload` holds the receiver at the choose signal with the approach link bound, asserts the
  reciprocal link and the hold after the reload, and advances.

`[EXECUTION-VERIFIED:2026-11-14, Debug|x64]` The division and paired reloads pass in the join phase
and in the fresh offline load; both peers stayed running through the observation window; no parsed
checklist pair mismatched (12,108 pairs for the paired case).

Runners: `scripts/run-pending-runtime-reload-tests.sh` (POSIX) and
`scripts/run-pending-runtime-reload-tests.ps1` (Windows). Both start a client to force the join,
check for desync markers and parsed checklist-pair mismatches, require the joined peers to stay alive
for an observation window, and then load the preserved join save in a fresh offline process. Both
default to the division reload and select the pair reload with
`-CaseName pair-reload -ScenarioName pending-runtime-pair-reload -Selector
tests\pending-runtime\pair-reload\scenario.nut -Harness tests\pending-runtime\pair-harness.nut
-HarnessName pair-harness` (PowerShell) or the corresponding `--case`, `--scenario`, `--selector`,
`--harness` and `--harness-name` options (POSIX). The choose reload uses the same options with
`choose-reload`, `pending-runtime-choose-reload`, `tests/pending-runtime/choose-reload/scenario.nut`,
`tests/pending-runtime/choose-harness.nut` and `choose-harness`. The PowerShell runner stages the
scenario through a directory junction and removes the junction without following it.

## Runner

`scripts/run-pending-runtime-tests.sh` stages the selected case in the pakset scenario directory with
its harness and all fixture maps, runs one process per case with `-nomidi`, and requires a printed
PASS marker plus any case-specific log assertion. A script error, a process exit before a marker, no
log progress for the stall timeout, the overall timeout and a crash all fail the case. It selects
`harness.nut` for `cases/`, `pair-harness.nut` for `pair-cases/` and `choose-harness.nut` for
`choose-cases/`.

Environment: `PENDING_RUNTIME_TIMEOUT`, `PENDING_RUNTIME_STALL_TIMEOUT`, `PENDING_RUNTIME_BINARY`,
`PENDING_RUNTIME_OBJECTS` and `PENDING_RUNTIME_CASES`. `PENDING_RUNTIME_CASES` is a space-separated
subset of case names; the runner rejects a name that is not a committed case. Each result line
carries the case's wall-clock duration. Exit code 0 means every case passed its marker and
case-specific assertions.

The fixture uses `is_equal` for vehicle-descriptor and loose-stock identity. Sampled
`vehicle_desc_x` names are invalid immediately after map load (open bug in
[known-bugs](../known-bugs.md)), so name comparison is not used. Successful cases print convoy
identities and vehicle counts, not descriptor names.

## Test cadence

The fixture is outside the default suite, and the full set is an integration gate rather than an
edit loop. Run only the cases that can observe the change:

- **Iteration.** Run the case or cases that exercise the changed branch. A change to
  `choose-harness.nut` can affect every choose case, but only `target-present`,
  `target-present-ignore-choose`, `multiple-choose-signals`, `target-arrives-after` and
  `committed-target` execute the coupling-point assertion described above; `one-sided-layout` and
  `pair-layout` check the post-commit layouts, `unresolvable-order` and `pair-unresolvable` check the
  hold, `straddling-contact` checks the contact point in the tile behind a long rear vehicle, and
  `target-absent` is enough as a load smoke test. A change confined to one case's `scenario.nut`
  needs that case only. A change to the orientation choice, `vehicle_t::set_reversed` or the
  unit/turntable reversal delay must run `facing` and the paired cases.
- **Package.** Run the whole group once the package's behaviour is complete and before committing
  it: `forward` plus `cases/` (the division and targetless cases), `pair-cases/` (the paired
  exchanges), or `choose-cases/` (the choose-signal cases).
- **Integration.** Run the whole pending-runtime set plus the reload cases before pushing or merging
  a package, and after any change to save/load code, track reservation ownership
  ([signals-and-blocks](../signals-and-blocks.md)), the network checklist, convoy state transitions,
  the paired-exchange solver or the network protocol. The reload cases use their own runners and are
  not part of the pending-runtime set.

Do not run the fixture for documentation, comments, translation strings or help text. A 35-case run
took about 23 minutes on 2026-11-16; a choose case took 14-53 seconds, a Y case took 26-51 seconds
and a paired case took 40-76 seconds on the same machine [EXECUTION-VERIFIED:2026-11-16]. The full
run therefore averaged about 39 seconds per case, so the cost scales with the number of cases rather
than with one slow case.

`PENDING_RUNTIME_CASES` selects a subset, for example:

```sh
PENDING_RUNTIME_CASES='target-present target-arrives-after' \
PENDING_RUNTIME_BINARY='./Simutrans-Extended-debug.exe' \
PENDING_RUNTIME_OBJECTS='pak128.Britain-Ex' \
scripts/run-pending-runtime-tests.sh
```

## Planned coverage

The runtime fixture does not yet exercise every operation the feature supports. The planned coverage
is: a multi-stop pick-up goods train; locomotive replacement with unchanged hauled vehicles,
including an electrification boundary and a cross-player case; multiple-unit division and rejoining
in both orientations; a locomotive-hauled split with a non-driveable loose remainder; through
coaches collected later by a branch service; a tractor exchanging loading trailers; and a motor boat
with zero, one and several butties. Include pool contention, broken or absent targets, save/reload
and server/client determinism checks [RECOLLECTION:2026-11-03 operational scope; UNVERIFIED fixture
composition].

Neither this fixture nor the reload cases covers cargo or passenger continuity: a vehicle that
becomes loose stock is discharged under the current runtime, and the intended loose-stock treatment
is described in [cargo-continuity](../ex-15/recombination-design/cargo-continuity.md).

## Open questions

- The holder-to-mover transfer half of `rail_vehicle_t::hand_over_contact_reservation` has no
  scripted case. The mechanism and the narrow stale-target window in which it runs are in
  [reservation-handoff](../consist-recombination/reservation-handoff.md#the-transfer-window). A
  fixture would have to produce that ordering: the target must still hold the contact tile when the
  mover enters, but must already have started departing so that the mover's recombination pass does
  not commit. Staging it is not deterministic with the current fixture. Whether the branch is a
  defensive case is a design question for
  [ex-15/recombination-design](../ex-15/recombination-design.md).
- Whether a turn should require a reversal facility at the stop is open. The runtime turns any
  bidirectional vehicle during the shunt and charges the existing unit or turntable reversal time;
  `facing` covers the orientation choice and the flag application, but no case asserts the added
  delay. A facility-gated model has no scripted case because the facility model does not exist.
- No CI job runs either pending-runtime fixture. The fixture map is an Extended 14 save loaded and
  re-saved by ex-15; whether that is an adequate substitute for an ex-15-native save remains open.

## Provenance

The fixture layout, case list, assertions and runner facts were checked against ex-15 @ 9fdefa153
(2026-11-16). The division cases were executed against ex-15 @ 97029f453 / Debug|x64 on 2026-11-13;
the paired and choose-signal cases on 2026-11-15 (choose cases against ex-15 @ 0692167b6 and later);
the reload cases on 2026-11-14 and 2026-11-16. The committed-target hold correction and the
`committed-target` and `contact-reservation` cases are from ex-15 @ 03bf5306e (2026-11-16). The
orientation choice and the `facing` case are from ex-15 @ 9fdefa153 (2026-11-16). The full
pending-runtime set (35 cases) and the Y, paired and choose reloads passed on 2026-11-16
[EXECUTION-VERIFIED:2026-11-16, DEBUG build]. The planned coverage list is the operation-scope
record from [ex-15/recombination-design](../ex-15/recombination-design.md).
