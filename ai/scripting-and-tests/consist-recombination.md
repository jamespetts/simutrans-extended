---
status: draft
verified: ex-15 @ bf157b7a7 plus working-tree pick-up goods package (2026-11-17, scenario cargo API, injected-cargo cases, through-wagon cases and the collection-loading credit); ex-15 @ dafe7031e plus working-tree test tooling (2026-11-18, runner parallelism, fast-forward and reload timings); ex-15 @ 93da398e6 (2026-11-18, Multi-Owner Access Rule and ownership cases); ex-15 @ db8bb34e5a (2026-11-20, pair-reload exchange limit, flash-reload frames per step and observation-limit hold); ex-15 @ e74fb897d0 (2026-11-20, line-level through-pair recording, the `through-pair` case, the two script accessors and the `pickup-reload` re-assertion); ex-15 @ 57435c9cb1 plus working-tree passenger loose-unload fix (2026-11-22, passenger/freight H cargo cases, schedule-edit cases, passenger capacity, mail/TPO cases, nested split and three loaded pair reload cases); ex-15 @ 6998ff08e1 plus working-tree per-operation shunting delay (2026-11-24, delay arithmetic against the runner limits); ex-15 @ 1578e7a689 plus working-tree standing-order and deferred-unload fix (2026-11-25, `rear-portion-order`, `departure-reversal` and `departure-reversal-freight` pass; `schedule-edit-divergence` accepts the D packet continuing); earlier cases as cited
---
# Consist recombination scripting and tests

**Covers:** `tests/pending-runtime/` (`harness.nut`, `pair-harness.nut`, `choose-harness.nut`,
`pickup-harness.nut`, `ownership-harness.nut`, `cases/`, `pair-cases/`, `choose-cases/`,
`pickup-cases/`, `ownership-cases/` and the reload scenarios),
`scripts/run-pending-runtime-tests.sh`, `scripts/run-pending-runtime-reload-tests.sh` and
`scripts/run-pending-runtime-reload-tests.ps1`. Parent:
[scripting-and-tests](../scripting-and-tests.md). Feature and runtime:
[consist-recombination](../consist-recombination.md). Design and feature status:
[ex-15/recombination-design](../ex-15/recombination-design.md),
[ex-15/schedule-and-consists](../ex-15/schedule-and-consists.md).

## Terms

The vocabulary used here — target, mover, contact tile, coupling point, approach link and through
coach — is defined in [consist-recombination](../consist-recombination.md#terms). The through pair
(the halt pair under which a recorded journey time is stored) and the line-bound convoy (a convoy
that runs for a line) are defined in
[yh-path-explorer](../ex-15/yh-path-explorer.md#definitions). TPO (Travelling Post Office) is
defined in
[passenger-and-goods-systems](../ex-15/recombination-design/passenger-and-goods-systems.md#terms).

## Scope

`tests/pending-runtime/` is a separate fixture for the recombination runtime: Y division (one convoy
splitting into two portions), paired H exchange (two convoys exchanging vehicles), choose-signal
call-on, the Multi-Owner Access Rule at assembly, a targetless order that turns a vehicle to reverse
a consist, pick-up goods and through-wagon working, passenger division capacity, mail/TPO handling,
and the loaded passenger and freight cases. It is not part of the default Squirrel suite
([scripting-and-tests](../scripting-and-tests.md)) and no CI job runs it. Every case runs in its own
game process, so cases cannot contaminate one another; the runner starts several processes at once
because the fixture worlds are tiny and each process has its own log and watchdog.

## Fixture layout

- `harness.nut` builds the infrastructure, the convoy, the target and the consist orders through
  public script interfaces, then owns the Y-division, facing, `plain-uncouple-passenger` and
  `nested-split` observers and assertions. The passenger case injects packets with
  `halt_x.add_cargo`; `nested-split` creates a second target line and a second division.
- `pair-harness.nut` does the same for two convoys that meet at one stop on two platforms of the same
  halt, with a reciprocal `couple` / `uncouple` entry and an end-state order on each. The loaded
  cases add a loading halt west of the junction, a delivery halt beyond the east stop, and consumer
  industries for the freight cases; they inject cargo with `halt_x.add_cargo` after the loading
  convoy leaves the depot.
- `choose-harness.nut` builds the two-platform junction from `pair-harness.nut` with a choose signal
  and signalbox in the throat, then asserts the choose-signal behaviour through `sign_x.get_state()`,
  `convoy_x.get_working_method()` and `convoy_x.get_vehicle_snapshot()`.
- `pickup-harness.nut` uses public construction, schedule and consist-order calls to build the
  freight, passenger and mail fixture in the empty map, and the halt cargo API to place its cargo
  packets. For the goods cases it selects a consumer industry and builds only that industry; those
  cases create their cargo with `halt_x.add_cargo` instead of by factory production, so they do not
  depend on factory dispatch. The passenger cases select passenger railcars and inject passenger
  packets; the mail cases build mail-enabled station extensions and inject mail packets. The
  harness reads waiting cargo, vehicle loads, loading time and player way-toll statistics, and
  watches the loose-stock sequence. `halt_x.clear_cargo` removes any other waiting cargo before
  each injection. `pickup-reload/` and `flash-reload/` use that harness with the ordinary
  network-join reload runner.
- `ownership-harness.nut` creates a second player. Player 2 builds a branch and a depot, runs a
  donor convoy to the junction and leaves a vehicle in the laid-over pool; player 0's collector
  then tries to take it. The case decides whether player 0 permits player 2 before the collector
  reaches the junction.
- All five harnesses read a case selector from the `case_kind` global that the case's
  `scenario.nut` sets, and print `RUNTIME CASE <case> PASS` or
  `RUNTIME CASE <case> FAIL: <assertion>`.
- `scenario.nut` selects the forward Y case; `cases/<case>/scenario.nut` selects each further Y
  case; `pair-cases/<case>/scenario.nut` selects each paired case;
  `choose-cases/<case>/scenario.nut` selects each choose-signal call-on case;
  `pickup-cases/<case>/scenario.nut` selects each pick-up goods case;
  `ownership-cases/<case>/scenario.nut` selects each Multi-Owner Access Rule case.

## Y-division and targetless cases (`cases/`)

The baseline case is `forward` (the shared `scenario.nut`). It builds a source convoy and the target
line or lineless convoy, authors one end-state order for each portion and starts the convoy. It
observes the created portion's line, schedule, entry, vehicle membership and identity, and then its
normal advance from the target entry. The reversed cases also check the traversal direction.
[EXECUTION-VERIFIED:2026-11-13, Debug|x64 with `-nomidi`]

The further cases are a plain uncouple, a deleted target line, a target entry without an order, an
unsatisfiable order, an unpowered result, collection from the stop's pool, a reversed target
schedule, a lineless target convoy, a mirrored source schedule, a targetless order that reverses a
two-vehicle consist, a plain uncouple of a loaded passenger carriage, a nested split, a rear
portion order check and two departure reversals:
`plain-uncouple`, `broken-target`, `target-no-order`, `unsatisfiable-target`, `unpowered-target`,
`pool-collection`, `reversed-target`, `lineless-target`, `reversed-source`, `facing`,
`plain-uncouple-passenger`, `nested-split`, `rear-portion-order`, `departure-reversal` and
`departure-reversal-freight`.

`facing` builds `[retained, turned]`, a two-vehicle consist that is valid as written. Its entry at
the junction carries a targetless consist order for `[turned, retained]`. The turned vehicle is an
unpowered bidirectional vehicle that cannot be at the front in its current orientation; it can be
at the front only after a turn. The runtime must therefore set its `is_reversed` flag and add the
turntable reversal time to the shunting delay. The case asserts the reversed sequence and the flag,
then lets the convoy continue to the east entry.

`plain-uncouple-passenger` releases a loaded passenger carriage as loose stock. Two passenger
packets are injected: one whose next transfer is the uncouple stop and destination is beyond, and
one whose next transfer is a later stop. The first is unloaded at the stop; the second stays aboard
the loose carriage. The case selects a passenger railcar and carriage whose accommodation classes
make the load unambiguous, and asserts that the loose carriage holds only the through packet.

`nested-split` starts `[first, second, first]`, divides at the junction into a retained `[first]`
and a portion on line P `[second, first]`, then divides that portion at the north stop into
`[second]` on line P and `[first]` on line Q. It asserts both final portions, the vehicle count and
the absence of stranded or duplicated vehicles. Both cases passed on 2026-11-22
[EXECUTION-VERIFIED:2026-11-22, Debug x64].

`rear-portion-order` starts `[retained, split, split_tail]` and retains only `retained`, so the
displaced vehicles are the rear `[split, split_tail]` group. The target entry's order has two slots
whose alternatives match any vehicle, so the order treats either vehicle order as valid and a
reversed collection order reaches `convoi_t::match_consist_order` unchanged. The case asserts that
`split`, the vehicle that stood immediately behind `retained`, is the portion's leading vehicle and
that it stands ahead of `split_tail`. `convoi_t::commit_recombined_consist` creates the displaced
group in the order in which the vehicles stood, so the portion is promoted in that order and
departs with `split` leading. The case passed on the fixed build
[EXECUTION-VERIFIED:2026-11-25, Debug x64].

`departure-reversal` uses a multiple unit whose rear car has a driving cab. Its target line leaves
the junction west while the source arrives east, so the portion must reverse.
`convoi_t::get_terminal_shunt_mode` selects `change_direction`: the array order reverses and the
train is driven from the rear cab. The case asserts the order in which the vehicles stood before
departure, then the reversed order and the flipped per-vehicle orientation flags.

`departure-reversal-freight` uses a powered leader and a rear vehicle with no head-end cab (a
brake van). The target line also leaves west. `convoi_t::get_terminal_shunt_mode` uses a turning or
shunting mode; the two-vehicle array order stays the same, every vehicle is turned, and the
locomotive remains leading with the brake van trailing. The case asserts the unchanged order and
the flipped flags.

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
emptied donor is destroyed (`pool-fallback`). The `already-at-end-state` orders place an optional
empty slot between the locomotive and the coach, so `paired_exchange_solver_t` must omit that slot
without moving either mandatory vehicle or charging a shunting delay.

Four loaded cases use the same pair harness with a loading halt west of the junction and a delivery
halt beyond the east stop. The freight cases also build consumer industries. The harness injects
cargo with `halt_x.add_cargo` after the loading convoy leaves the depot. `make_cargo_order` sets
each slot's `catg` because `schedule_t::parse_orders` derives the carried-category tables from the
order slots; without it the path explorer does not route a lineless convoy for the cargo.

- `pair-passenger-through`: the target's loaded passenger carriage is transferred directly to the
  receiver; the passengers continue beyond the junction and the receiver delivers them.
- `pair-passenger-loose`: the receiver's loaded carriage is released at the junction. A passenger
  whose next transfer is the junction leaves the carriage; a passenger whose next transfer is later
  stays aboard as a through coach. The receiver collects the carriage on its return and books the
  through fare.
- `pair-freight-through`: a loaded goods wagon is transferred directly; it keeps its load and the
  receiver delivers it.
- `pair-freight-loose`: a loaded goods wagon is released; goods bound for the junction are delivered
  there, the rest stays under the through-wagon model, and the receiver collects and delivers it.

The loose cases start the target first so that the exchange commits before the receiver's deferred
unload can repair a junction-transfer packet's route. All four cases passed on 2026-11-22
[EXECUTION-VERIFIED:2026-11-22, Debug x64]; the pair group passed 11/11.

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

## Ownership cases (`ownership-cases/`)

`ownership-harness.nut` creates player 2 and a second depot on its own branch, connected to
player 0's trunk at (2, 2). Player 0 grants player 2 access before the donor starts, so the donor
can use player 0's trunk and stops; the donor is player 2's convoy and leaves the donor's second
vehicle in the junction's laid-over pool. The donor is destroyed after the release, and player 0's
collector then runs to the junction with a targetless order that asks for its own locomotive and
the pooled vehicle.

- `ownership-allowed`: player 0 permits player 2, so the order takes the pooled vehicle. The final
  consist is mixed-owner, and the collected vehicle keeps player 2's ownership.
- `ownership-denied`: player 0 withdraws player 2's access after the release and before the
  collector arrives, so the order must fail. The collector keeps its original consist and player
  2's vehicle stays in the pool.

The only difference between the cases is the access grant. The allowed case shows that the pooled
vehicle is otherwise collectable, so the denied case isolates the Multi-Owner Access Rule from the
other matching conditions.

## Pick-up goods cases (`pickup-cases/`)

The fixture selects a consumer industry from the loaded pakset for the goods cases and builds only
that industry. The cases that inject cargo clear the fixture halts and inject their own cargo with
`halt_x.add_cargo`, so the cargo amount, routing, origin and arrival time are fixed by the case
rather than by factory production. The destination position is the consumer's position for goods,
the destination halt for passengers, and the mail-enabled halt extension for mail. The injected
packet is ordinary saved halt cargo; no factory-produced packet takes part in any assertion.
`empty-distribution` and `recipient-same` use empty wagons only and do not inject cargo.

- `empty-distribution` releases five empty wagons, then repeatedly calls a collector whose order is
  `[locomotive, optional wagon, optional wagon, optional wagon, mandatory rear vehicle]`, with the
  optional alternatives restricted to empty vehicles. The first visit forms three wagon slots and
  leaves two. The mandatory D order releases the first batch, so the second B visit collects the two
  remaining wagons. After that batch is also released at D, a further B call cannot add anything. The
  assertions include the rear vehicle's position and the pool contents.
- `recipient-same` releases an empty wagon to the Same recipient (`available_to_flags` value 2,
  which resolves to the releasing lineless convoy's own id) and collects it when the same convoy
  returns. It reads the loose vehicle's recipient from the vehicle snapshot: the convoy flag, the
  Same flag and the releasing convoy's id. The collecting order asks for a wagon that is empty, so
  the collected vehicle can only be the released one.
- `cargo-through` injects a wagon load at A with D as the ultimate destination and B as the next
  transfer halt. The donor's line is A-B, so the donor loads the packet at A, carries it to B and
  releases the loaded wagon onto B's loop platform. A separate B-D collector has an optional slot
  restricted to loaded wagons. The donor waits for one per cent of a load (`minimum_loading` 1)
  before it departs. The assertions use the wagon's own load: it must be positive when the wagon is
  loose and unchanged when the collector has it. This is the through-wagon model: the wagon is the
  unit of collection, its existing load receives no loading credit, and the collector must serve the
  load. The collector must book the whole fare when it unloads at D. Way-toll apportionment to the
  second way owner is not observable here: the fixture map's saved settings carry no toll revenue
  percentage, so no payment reaches a way owner however the journey is apportioned.
- `through-pair` checks the line-level through-pair recording. A line-bound collecting convoy carries
  the through packet from the recombination to D, so `convoi_t::laden` records the pair from the
  packet's boarding halt A to D. The case reads the collecting line's table (the destination line of
  the through journey) with `line_x.get_recorded_journey_time(halt_x from, halt_x to)` and the
  collecting convoy's own table with `convoy_x.get_own_recorded_journey_time(halt_x from, halt_x to)`;
  both return the recorded average in tenths of minutes, or `-1` when the pair is absent. The
  assertions are that the line has a positive `(A, D)` time and that the convoy's own table has no
  `(A, D)` pair. `through-pair` is the recording's non-reload case; `pickup-reload` repeats the two
  assertions after its loopback join and again after the fresh offline load.
- `multi-destination-same-portion` loads one wagon with packets for two next transfers that the same
  committed portion serves. The target line runs B-D-P and the wagon remains loaded through both
  stops.
- `schedule-edit-divergence` starts with a source A-B-P and a target B-D-P; the target is edited
  mid-run to B-D. A trailing wagon holds one packet for D and one for P, so the two requirements
  diverge at B; `convoi_t::discharge_divergent_wagons` empties the wagon at B. The P packet becomes
  halt cargo. The D packet is still served by the edited target schedule and may wait at the halt
  or continue on the portion. A full leading wagon for P keeps the discharged P packet at the
  halt for the remainder of the pass.
- `schedule-edit-through-pool` removes the target entry's consist order mid-run, so no split-off
  portion forms. A wagon loaded for D stays loose at B with its load rather than being carried past
  its next transfer.
- `passenger-capacity` loads two passenger railcars to ordinary capacity from A for P, then edits
  the target portion to a destination that does not serve P. The retained railcar fills its
  overcrowded capacity; the split railcar's passengers are discharged at B rather than transferred
  to the non-serving portion.
- `mail-no-tpo` and `mail-tpo` use mail-enabled station extensions and mail/TPO vehicles.
  `mail-no-tpo` keeps junction-transfer mail in the loose mail van under the goods system and
  delivers mail destined for B; `mail-tpo` unloads junction-transfer mail at B, keeps later-transfer
  mail in the loose mail van, and leaves the loose TPO vehicle empty. The TPO case verifies that
  `convoi_t::get_catering_level(goods_manager_t::INDEX_MAIL) > 0` selects the passenger system. The
  fixture map's station coverage is four tiles, so the mail destination must lie within that
  coverage; an out-of-coverage destination halt is not a valid destination and
  `haltestelle_t::liefere_an` drops the packet when `find_route` fails.
- `flash-full`, `flash-partial` and `flash-late` release one empty wagon at B and inject a full
  wagon load at B for D. These cases test the empty-wagon loading credit: the cargo waits at the
  halt, the empty wagon is collected, and the cargo is loaded into it with the credit. The collector
  runs a B-D loop with no wagon in its consist before the
  wagon is released; its first two observed B arrivals give its looping period. A calibration
  donor run measures the travel time from the fixture's depot to the wagon release, and the real
  donor is then started so that the release falls a fixed margin after a collector arrival. That
  gives the wagon almost a full period of lead before the collecting arrival and keeps the donor
  clear of the junction at that arrival. The collecting tick is predicted as one period after the
  last actual B arrival before the release, which absorbs any delay the donor caused on the
  approach. The packet's declared arrival time is placed relative to that prediction: at the
  wagon's release tick for full credit, half a load share before the collection for partial
  credit, and 3000 ticks before it for the late case. The injection call itself runs at the
  release, so the result does not depend on the call's wall-clock tick; the credit uses the
  declared arrival time. Each case computes the expected result from the descriptor's
  minimum/maximum loading ticks, total and overcrowded capacity, the actual loaded amount and the
  snapshot's collection and earliest-arrival fields, and requires the collector's reported loading
  time to equal it. Full availability must credit the entire ordinary contribution, the partial
  case must produce a nonzero partial credit, and the late case must produce less than the full
  credit. The full case requires the interval from the release to the collecting arrival to be at
  least the load share, because the collector takes the empty wagon at its first arrival after the
  release.
- `pickup-reload/` holds the loaded loose wagon for a loopback client join and a fresh offline load of
  the join save; it also names the collecting line as the wagon's recipient and checks that choice
  after both loads. It is the named-recipient through-wagon case: the cargo stays aboard the wagon
  across the reload and the named collector takes both. It also re-asserts the `(A, D)` through pair
  recorded on the collecting line, and its absence from the collecting convoy's own table, after each
  load. `flash-reload/` holds the collected, fully credited wagon during its departure
  interval and repeats the delivery observation after both loads. Use the existing PowerShell reload
  runner with `-Harness tests\pending-runtime\pickup-harness.nut -HarnessName pickup-harness` and
  `-CaseName pickup-reload -ScenarioName pending-runtime-pickup-reload -Selector
  tests\pending-runtime\pickup-reload\scenario.nut`. For `flash-reload`, use `-CaseName flash-reload`,
  `-ScenarioName pending-runtime-flash-reload` and
  `-Selector tests\pending-runtime\flash-reload\scenario.nut`.

## Reload cases (`reload/`, `pair-reload/`, `pair-passenger-loose-reload/`,
`pair-freight-through-reload/`, `pair-freight-loose-reload/`, `choose-reload/`, `pickup-reload/`,
`flash-reload/`)

These are deliberately outside `cases/`, so the baseline runner does not select them. Each runs on a
loopback server, joins a client so that the server saves, transfers and reloads its state
(`nwc_sync_t`), re-asserts the restored state from the loaded world, and then loads the preserved
join save in a fresh offline process and repeats the assertions. The pick-up reload cases are
described under Pick-up goods cases.

- `reload` runs the forward division, holds after the split-off portion forms, re-asserts the
  divided state in `resume_game()` and advances the portion.
- `pair-reload` runs the paired exchange's one-way transfer, re-asserts both final consists and the
  laid-over vehicle from a string of each expected vehicle descriptor's scalar `vehicle_desc_x`
  properties (`get_power()`, `get_topspeed()`, `get_weight()`, `get_length()` and the coupling and
  electrification flags), because descriptor names are not reliable after a load, and then observes
  both convoys advance. The pair harness uses `RELOAD_EXCHANGE_STEP_LIMIT` (8,000 observations)
  instead of the ordinary `EXCHANGE_STEP_LIMIT` (1,000 observations) for this case: the reload
  runner's server reaches the wait for the counterpart and the exchange in more observations than
  the fast-forward pending-runtime runner.
- `pair-passenger-loose-reload` runs the passenger-loose exchange and holds after the loose carriage
  is laid over. After the reload it re-asserts the two final consists, the laid-over carriage and
  its through-only load, then lets the receiver collect and deliver it. The reload runner's 300 s
  shunting hold lets the junction-transfer passenger exceed its maximum tolerable wait; the case
  therefore accepts either the passenger boarding the receiver or leaving the junction, and asserts
  the carriage's through load and delivery.
- `pair-freight-through-reload` holds the transferred loaded wagon, re-asserts its load after the
  reload, then delivers it and checks the through fare.
- `pair-freight-loose-reload` holds the loose wagon, re-asserts the fare already booked at the
  junction and the wagon's through load, then collects and delivers the rest. The cargo reload
  cases use `RELOAD_DEPARTURE_STEP_LIMIT` (16,000 observations); the freight-loose exchange took
  11,905 observations at the reload server's pace.
- `choose-reload` holds the receiver at the choose signal with the approach link bound, asserts the
  reciprocal link and the hold after the reload, and advances.

`[EXECUTION-VERIFIED:2026-11-14, Debug|x64]` The division and paired reloads pass in the join phase
and in the fresh offline load; both peers stayed running through the observation window; no parsed
checklist pair mismatched (12,108 pairs for the paired case).

`[EXECUTION-VERIFIED:2026-11-22, Debug|x64]` The three loaded pair reloads passed in the join phase
and in the fresh offline load: `pair-passenger-loose-reload` READY 60 s, client join 16 s, server
PASS 204 s, 20,942 checklist pairs with 0 mismatches; `pair-freight-through-reload` 77 s, 16 s, 78 s
and 8,352 pairs; `pair-freight-loose-reload` 144 s, 16 s, 210 s and 21,523 pairs. The offline load
printed RESTORED and PASS for each. The existing `pair-reload` regression passed with 6,600 pairs.

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
`tests/pending-runtime/choose-harness.nut` and `choose-harness`. The three loaded pair reloads use
the same options with their own case, scenario and selector names, listed in the runner headers and
in the scenarios. The PowerShell runner stages the scenario through a directory junction and
removes the junction without following it.

Both runners set `shunting_time_seconds` to 300, which is about 55 wall-clock seconds at the
server's game-time rate and leaves a wide margin over the client join; the case fails with an
explicit message if the join outlasts it. The server runs at 100 frames per second. It uses one
frame per simulation step for the division, paired, choose and pick-up reloads, and four frames per
simulation step for the flash reload. One frame per step gives those four cases four times as many
simulation steps per wall-clock second as four frames per step. The flash reload is timed in game
time (`world.get_time().ticks`), so four frames per step advances four times as much game time per
observation and reaches its collection in one quarter of the observations. The
`-ServerFramesPerStep` (PowerShell) and `--server-frames-per-step` (POSIX) options override the
case default. The fresh offline load uses `-fast_forward`. On the maintainer's machine the
division reload reached READY after 37 seconds and PASS after 76 seconds, with the client joining
after 14 seconds [EXECUTION-VERIFIED:2026-11-18, ex-15 @ dafe7031e plus working-tree test tooling,
Debug x64].

## Runner

`scripts/run-pending-runtime-tests.sh` stages each selected case in the pakset scenario directory with
its harness and all fixture maps, runs one process per case with `-nomidi -nosound -fast_forward`,
and requires a printed PASS marker plus any case-specific log assertion. Each case has its own
process, scenario directory, log and watchdog; up to `PENDING_RUNTIME_JOBS` cases run at once
(default: the processor count, capped at 8). A script error, a process exit before a marker, no
output progress for the stall timeout, the overall timeout and a crash all fail the case. The runner
selects `harness.nut` for `cases/`, `pair-harness.nut` for `pair-cases/`, `choose-harness.nut` for
`choose-cases/`, `pickup-harness.nut` for `pickup-cases/` and `ownership-harness.nut` for
`ownership-cases/`.

The default engine debug level is 1, which keeps the script's `print` output and the engine's error
lines but suppresses the per-step warning log. The three cases that assert an in-game warning
message (`target-unreachable`, `unresolvable-order` and `pair-unresolvable`) use level 3, because
`message_t::add_message` logs the message text through `DBG_MESSAGE`, which is emitted only at
`LEVEL_MSG`. Each harness prints `RUNTIME CASE <case>: heartbeat step <n>` every 200 observation
steps, so the stall watchdog does not depend on engine per-step logging.

Environment: `PENDING_RUNTIME_GROUPS`, `PENDING_RUNTIME_CASES`, `PENDING_RUNTIME_JOBS`,
`PENDING_RUNTIME_FAST_FORWARD`, `PENDING_RUNTIME_DEBUG`, `PENDING_RUNTIME_LIST`,
`PENDING_RUNTIME_TIMEOUT`, `PENDING_RUNTIME_PICKUP_TIMEOUT`, `PENDING_RUNTIME_STALL_TIMEOUT`,
`PENDING_RUNTIME_BINARY` and `PENDING_RUNTIME_OBJECTS`. `PENDING_RUNTIME_CASES` is a space-separated
subset of case names and overrides `PENDING_RUNTIME_GROUPS`; the runner rejects a name that is not a
committed case. `PENDING_RUNTIME_GROUPS` is a space-separated subset of `division`, `pair`, `choose`,
`pickup` and `ownership`; the groups map to `cases/`, `pair-cases/`, `choose-cases/`,
`pickup-cases/` and `ownership-cases/`.
`PENDING_RUNTIME_LIST=1` prints the selected cases and exits. `PENDING_RUNTIME_FAST_FORWARD=0` runs
at wall-clock speed; the default is 1. `PENDING_RUNTIME_DEBUG` sets the engine level for the cases
that do not require level 3; the default is 1. The slow `pickup-cases/` cases (`cargo-through` and the
`flash-*` cases) use `PENDING_RUNTIME_PICKUP_TIMEOUT` (default 600 seconds, against 300 for the
others) because each of them builds a consumer industry, several services and a wagon release
sequence, and then waits for the scheduled collection. The pick-up reload cases use the reload
runner's `-ServerReadyTimeout`, `-ClientTimeout` and `-OfflineTimeout` instead. Each result line
carries the case's wall-clock duration. Exit code 0 means every case passed its marker and
case-specific assertions.

The fixture uses `is_equal` for vehicle-descriptor and loose-stock identity. Sampled
`vehicle_desc_x` names are invalid immediately after map load (open bug in
[known-bugs](../known-bugs.md)), so name comparison is not used. Successful cases print convoy
identities and vehicle counts, not descriptor names.

## Test cadence

The fixture is outside the default suite. Run only the cases that can observe the change during
editing; run the full set only before pushing or merging a package:

- **Iteration.** Run the case or cases that exercise the changed branch. A change to
  `choose-harness.nut` can affect every choose case, but only `target-present`,
  `target-present-ignore-choose`, `multiple-choose-signals`, `target-arrives-after` and
  `committed-target` execute the coupling-point assertion described above; `one-sided-layout` and
  `pair-layout` check the post-commit layouts, `unresolvable-order` and `pair-unresolvable` check the
  hold, `straddling-contact` checks the contact point in the tile behind a long rear vehicle, and
  `target-absent` is enough as a load smoke test. A change confined to one case's `scenario.nut`
  needs that case only. A change to the orientation choice, `vehicle_t::set_reversed` or the
  unit/turntable reversal delay must run `facing` and the paired cases. A change to the cargo API,
  the loading credit or the route repair must run `cargo-through`, one of the `flash-*` cases and
  the matching reload case (`pickup-reload` or `flash-reload`) rather than the whole pick-up group.
  A change to the through-pair recording in `convoi_t::laden` or to
  `path_explorer_t::compartment_t::get_recorded_pair_time` must run `through-pair`; if the change
  touches the saved journey-time table, run `pickup-reload` as well. A change to
  `unload_newly_loose_cargo`, the loose-stock passenger rule or the TPO selection must run
  `plain-uncouple-passenger`, `pair-passenger-loose`, `mail-tpo` and `mail-no-tpo`. A change to
  the order in which `convoi_t::commit_recombined_consist` collects displaced vehicles must run
  `rear-portion-order`; a change to its deferred stop unload for a portion being formed must run
  `goods-division`, `multi-destination-same-portion` and `schedule-edit-divergence`. A change to
  the departure direction, `convoi_t::reverse_order` or `convoi_t::get_terminal_shunt_mode` must
  run `departure-reversal` and `departure-reversal-freight`. A change to
  `convoi_t::match_consist_order`, including the branch that
  handles optional slots, or to the Multi-Owner Access Rule must run both `ownership-*` cases; a
  change to the incremental access check in `paired_exchange_solver_t` must run the paired cases.
- **Package.** Run the whole group once the package's behaviour is complete and before committing
  it: `forward` plus `cases/` (the division and targetless cases), `pair-cases/` (the paired
  exchanges), `choose-cases/` (the choose-signal cases), `pickup-cases/` (the pick-up goods
  cases) or `ownership-cases/` (the Multi-Owner Access Rule cases).
- **Integration.** Run the whole pending-runtime set plus the reload cases before pushing or merging
  a package, and after any change to save/load code, track reservation ownership
  ([signals-and-blocks](../signals-and-blocks.md)), the network checklist, convoy state transitions,
  the paired-exchange solver or the network protocol. The reload cases use their own runners and are
  not part of the pending-runtime set. A full set that already passed in the same session need not
  be repeated when only test or scenario code has changed since; record the earlier result.
- **Stop redundant runs.** Stop a run that can no longer produce new evidence, keep its partial log,
  and kill stale game processes before the next invocation. One runner invocation may run several
  cases at once; do not start a second invocation while the first is running.
- **Prefer a deterministic fixture.** A cargo case should inject the packet it needs instead of
  waiting for factory production, and a loaded-wagon case should build the load through an explicit
  order sequence. The fixture does not preload a loose wagon physically: no public script call
  places freight into a stationary unscheduled vehicle, and the loading credit is the mechanism
  that the case asserts.

Do not run the fixture for documentation, comments, translation strings or help text. Measured with
the default runner settings on the maintainer's machine (24 logical processors, Debug x64,
`-debug 1 -fast_forward`): a choose case takes 3-7 seconds, or 16-18 seconds for the three hold
cases at `-debug 3`; a Y case takes 4-6 seconds; a paired case takes 4-6 seconds, or 20-88 seconds
for the loaded cargo cases; a pick-up case takes 5-28 seconds. The 15-case pick-up group took 56
seconds with 8 jobs, including the five known fare-rounding failures, and the 43-case non-pick-up
set took 148 seconds with 8 jobs [EXECUTION-VERIFIED:2026-11-22]. An ownership case takes 6 seconds.
The pakset load, about 5 seconds per process at `-debug 1`, is now the largest per-case cost, so the
job count matters more than case selection for the whole set.

`PENDING_RUNTIME_CASES` selects a subset, for example:

```sh
PENDING_RUNTIME_CASES='target-present target-arrives-after' \
PENDING_RUNTIME_BINARY='./Simutrans-Extended-debug.exe' \
PENDING_RUNTIME_OBJECTS='pak128.Britain-Ex' \
scripts/run-pending-runtime-tests.sh
```

`PENDING_RUNTIME_GROUPS` selects a whole group without naming every case, for example
`PENDING_RUNTIME_GROUPS='choose'` or `PENDING_RUNTIME_GROUPS='division pair'`.

## Planned coverage

The runtime fixture covers the Y division, one-sided receive, paired H exchange, targetless
assembly, choose-signal call-on, passenger and goods division, and the loaded pair cases. The
remaining planned coverage is: a multi-stop pick-up goods train; locomotive replacement with
unchanged hauled vehicles, including an electrification boundary and a cross-player case;
multiple-unit division and rejoining in both orientations; a locomotive-hauled split with a
non-driveable loose remainder; a tractor exchanging loading trailers; and a motor boat with zero,
one and several butties. A cross-player paired exchange remains unscripted. Save/reload and
server/client determinism are covered for the division, paired exchange, choose signal, pick-up and
the loaded pair cases; the remaining operations above still need them
[RECOLLECTION:2026-11-03 operational scope; UNVERIFIED fixture composition].

The `pickup-cases/` set extends the base fixture to cargo continuity, optional collection slots,
loaded/empty predicates, loading credit, the line-level through-pair recording, passenger division
capacity and mail/TPO handling. It is described above. The general cargo and fare rules are in
[cargo-continuity](../ex-15/recombination-design/cargo-continuity.md).

The `ownership-cases/` set covers the Multi-Owner Access Rule for a targetless pool collection. The
cross-player part of the planned locomotive-replacement case and a cross-player paired exchange
remain unscripted.

## Open questions

- Way-toll apportionment of a through journey across a release and a collection has no scripted case. The
  fixture map's saved settings carry no toll revenue percentage, so `player_t::get_way_tolls()` cannot
  distinguish a working apportionment from a missing one. A case would need a map whose saved settings enable
  the toll, or a way for a script to read the apportioned revenue directly.
- `schedule-edit-divergence` accepts the D packet either remaining at the halt or continuing on
  the portion, because the edited target schedule still serves D. It asserts that the divergent P
  packet is discharged and that the wagon carries at most the D load. Whether a loading pass
  should exclude any packet discharged at the same stop is open.
- The flash-load full-credit case requires the interval between the wagon release and the collector's next B
  arrival to be at least the load share. Whether a future change to loading times or service timings could
  make that interval too short is not bounded by the fixture; the case fails with an explicit message rather
  than silently weakening the assertion.
- The partial and late flash cases place the packet on the halt when the empty wagon is released, but give
  it a later `arrival_time` (half a load share or 3000 ticks before the predicted collection) so that the
  loading-credit intersection is deterministic without waiting for the clock. `halt_x.add_cargo` preserves
  any supplied tick. Whether the API should refuse a tick later than the current world tick, or the fixture
  should wait for the chosen tick and inject then, is open; the current assertion depends on the explicit
  value.
- No case exercises the named-recipient hybrid recorded in
  [cargo-continuity](../ex-15/recombination-design/cargo-continuity.md): a named recipient keeps the
  load, while Same or Any would discharge the cargo without booking a fare and flash-load it with its
  through record preserved. `cargo-through`, `pair-freight-through`, `pair-freight-loose` and
  `pickup-reload` cover the current through-wagon model; the discharge+flash-load alternative has no
  case.
- A one-sided receive with a loaded passenger carriage has no scripted case. The next-transfer rule
  is covered by `plain-uncouple-passenger` and `pair-passenger-loose`, but the one-sided path itself
  is unscripted.
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
- No case asserts the shunting delay's arithmetic. A paired exchange that swaps one vehicle each
  way performs four coupling operations and therefore waits two and a half times the configured
  `shunting_time_seconds`. The reload departure limit covers that arithmetically; the
  fast-forward `DEPARTURE_STEP_LIMIT` has no measured margin for it. A case would need to observe
  a convoy across a measured number of ticks for an operation with a known coupling count.
- No CI job runs either pending-runtime fixture. The fixture map is an Extended 14 save loaded and
  re-saved by ex-15; whether that is an adequate substitute for an ex-15-native save remains open.
- The flash collection logs `vehicle_base_t::leave_tile()` with `'typ 67' ... could not be removed
  from 5 2` once per collection. The log line means `gr->obj_remove(this)` failed for the ground at
  the vehicle's position. The function then checks the plan square and returns without reaching the
  whole-map search; the log contains no warning that it removed the vehicle from another ground
  there. The log therefore does not establish whether the vehicle was in any tile's object list.
  The case passes, but that does not prove the object list is consistent. The cause is not
  identified; a future investigation should identify the vehicle, its convoy state and the ground
  that holds it, and repeat the check after `convoi_t::commit_recombined_consist`.

## Provenance

The fixture layout, case list, assertions and runner facts were checked against ex-15 @ 9fdefa153
(2026-11-16). The division cases were executed against ex-15 @ 97029f453 / Debug|x64 on 2026-11-13;
the paired and choose-signal cases on 2026-11-15 (choose cases against ex-15 @ 0692167b6 and later);
the reload cases on 2026-11-14 and 2026-11-16. The committed-target hold correction and the
`committed-target` and `contact-reservation` cases are from ex-15 @ 03bf5306e (2026-11-16). The
orientation choice and the `facing` case are from ex-15 @ 9fdefa153 (2026-11-16). The full
pending-runtime set (35 cases) and the Y, paired and choose reloads passed on 2026-11-16
[EXECUTION-VERIFIED:2026-11-16, DEBUG build]. The scenario cargo API
(`halt_x.add_cargo` and `halt_x.clear_cargo`) and the injected-cargo pick-up goods rewrite were
executed on 2026-11-17. One pending-runtime run passed all 35 standard cases and the pick-up cases
`cargo-through`, `empty-distribution`, `flash-full`, `flash-partial` and `flash-late`; the run was
then stopped after `recipient-same` had started, and that case passed in a targeted run. The
`pickup-reload` and `flash-reload` loopback join and fresh offline reload both passed with no
diagnosed checklist-pair mismatch; the parsed pair count depends on the observation window, and the
2026-11-17 through-wagon `pickup-reload` verification parsed 1636 pairs (the `flash-reload`
verification parsed 604). After the API-validation rebuild, `cargo-through` passed again in 275
seconds; after the through-wagon rebuild, `pickup-reload` passed again with the narrowed layover
exemption (READY 143 s, server PASS 179 s, offline RESTORED 16 s). All runs used the Debug x64 build with
`-nomidi` [EXECUTION-VERIFIED:2026-11-17, DEBUG build, working tree]. On 2026-11-18 the runner
defaults changed to parallel processes at `-debug 1 -fast_forward`, and the harnesses gained the
progress heartbeat; the 35 standard cases then passed in 46 seconds with 8 jobs, and the six
pick-up cases in 12 seconds with 6 jobs. The reload runner's new defaults (300 shunting seconds,
one frame per step, offline fast-forward) reached READY after 37 seconds and PASS after 76 seconds,
with the client joining after 14 seconds, 840 parsed checklist pairs out of the server and client
logs, no mismatch, and the offline load RESTORED after 14 seconds. All runs used the Debug x64
build with `-nomidi -nosound` [EXECUTION-VERIFIED:2026-11-18, ex-15 @ dafe7031e plus working-tree
test tooling]. On 2026-11-18 the Multi-Owner Access Rule was added to
`convoi_t::match_consist_order`, the two `ownership-*` cases were added, and the 43-case set passed
in 47 seconds with 8 jobs. The division, choose and pick-up reloads passed under the same runner
[EXECUTION-VERIFIED:2026-11-18, ex-15 @ 93da398e6, DEBUG build]. On 2026-11-20 the pair harness
gained `RELOAD_EXCHANGE_STEP_LIMIT` and the reload runner selected four server frames per
simulation step for `flash-reload`; the observation limit no longer counts the flash reload's
`awaiting-reload` hold. `pair-reload` passed READY 44 s, client join 14 s, server PASS 61 s,
6,742 checklist pairs with 0 mismatches, and offline RESTORED 14 s. `flash-reload` passed READY
260 s, client join 16 s, server PASS 43 s, 4,855 checklist pairs with 0 mismatches, and offline
RESTORED 16 s. Both used the Debug x64 build with `-nomidi -nosound`. The division, choose and
pick-up reloads also passed with the same runner on 2026-11-20, each completing the join and the
fresh offline load with no checklist mismatch. The 43-case pending-runtime set passed in
116 seconds with 8 jobs on the same day
[EXECUTION-VERIFIED:2026-11-20, ex-15 @ db8bb34e5a].
The `through-pair` case and the two script accessors it calls are committed at ex-15 @ e74fb897d0
(2026-11-20); the line-level recording they assert is committed at ex-15 @ 9d2b75ecea. The 44-case
pending-runtime set passed in 129 seconds with 8 jobs, and `through-pair` passed in 24 seconds. The
division reload passed with 6,709 checklist pairs and 0 mismatches; `pickup-reload` passed with 15,272
checklist pairs and 0 mismatches, including the through-pair assertion after the join and after the
fresh offline load [EXECUTION-VERIFIED:2026-11-20, ex-15 @ e74fb897d0].
On 2026-11-22 the passenger loose-unload fix and its cases were added:
`plain-uncouple-passenger`, `nested-split`, `multi-destination-same-portion`,
`schedule-edit-through-pool`, `schedule-edit-divergence`, `passenger-capacity`, `mail-no-tpo`,
`mail-tpo`, `pair-passenger-through`, `pair-passenger-loose`, `pair-freight-through`,
`pair-freight-loose`, `pair-passenger-loose-reload`, `pair-freight-through-reload` and
`pair-freight-loose-reload`. The 43-case non-pick-up set passed in 148 seconds with 8 jobs; the
15-case pick-up group had 10 passes and the five known fare-rounding failures; the three loaded pair
reloads and the `pair-reload` regression passed with the checklist pair counts recorded above
[EXECUTION-VERIFIED:2026-11-22, ex-15 @ 57435c9cb1 plus working-tree passenger loose-unload fix,
Debug x64]. On 2026-11-25 the `rear-portion-order` case was added with the standing-order and
deferred-unload fix in `convoi_t::commit_recombined_consist`: it failed before the fix with
`split_tail` in the leading slot and passes after it. The 16-case division group passed, including
`departure-reversal` and `departure-reversal-freight`, which were added on the same day and passed
on the same build; the 15-case pick-up group had 10 passes and the five known fare-rounding
failures; the 11-case paired
group, the 17-case choose-signal group, the two ownership cases and the 20-case road and
maintenance groups all passed on the fixed Debug x64 build with `-nomidi`
[EXECUTION-VERIFIED:2026-11-25, ex-15 @ 1578e7a689 plus working-tree fix]. The planned coverage
list is the operation-scope record from
[ex-15/recombination-design](../ex-15/recombination-design.md).
