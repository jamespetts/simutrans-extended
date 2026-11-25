---
status: draft
verified: none
---
# Depot consist formation: persistence, network and performance

Parent: [depot consist formation](../depot-consist-formation.md). Current code facts and reuse
points: [code-baseline](code-baseline.md). This document records the saved fields, the network and
determinism rules, the performance constraints, the failure modes and the test plan. Tags:
`[CODE]` is verified against the checked-out tree; `[PROPOSED]` is planned behaviour; `[OPEN]` is
unresolved.

## Persistence

The intended saved fields are:

| Owner | Field | Type | Default | Notes |
|---|---|---|---|---|
| `vehicle_t` | `depot_release_time` | `sint64` | 0 | Ticks; 0 means no pending job. Saved in `vehicle_t::rdwr_from_convoi()`. |
| `vehicle_t` | `depot_job_state` | `uint8` | 0 | None, waiting, in progress, complete. Saved in the same block. |
| `vehicle_t` | `depot_queue_index` | `uint32` | 0 | Position in the depot queue, used to rebuild the queue after a load. |
| `vehicle_t` | `depot_job_duration` | `sint64` | 0 | Length of the maintenance or overhaul job in ticks. |
| `depot_t` | `next_maintenance_queue_index` | `uint32` | 0 | Next queue index to allocate. Saved in `depot_t::rdwr()`. |
| `simline_t` | `target_convoy_count` | `uint16` | 0 | 0 means no target. Saved in `simline_t::rdwr()`. |
| `simline_t` | `fleet_policy` | `uint8` | 0 | Off or automatic. Saved in `simline_t::rdwr()`. |
| `simline_t` | `target_min`, `target_max` | `uint16` each | 0 | Bounds for the automatic policy. |
| `simline_t` | `add_threshold`, `remove_threshold` | `uint16` each | 0 | Load-factor thresholds in tenths of a percent. |
| `simline_t` | `add_months`, `remove_months` | `uint8` each | 0 | Consecutive-month counts for the policy. |
| `simline_t` | `LINE_GOODS_CAPACITY` | new `financial_history` entry | 0 | Freight capacity distance, added to the `MAX_LINE_COST` array. Older saves load zero; the goods load factor is zero until a month of data exists. The passenger denominator remains `LINE_CAPACITY`, which is booked only for passenger vehicles. |
| `convoi_t` | `replacement_awaiting_formation` | `bool` | false | True after `replace_now()` has bought or sold stock but before the one-shot order departs. Saved in `convoi_t::rdwr()`. |
| `convoi_t` | `withdrawal_pending` | `bool` | false | True while a Withdrawal is in progress. A Withdrawal uses the existing maintenance-type `convoi_t::go_to_depot()` call, so no separate depot coordinate is stored. |

The replacement order itself is derived from `replace_data_t`, which is already saved. The
recommendation is to keep `replace_data_t` until the assembled Consist departs and add the
`replacement_awaiting_formation` flag, rather than store a second `consist_order_t`. `replace_now()`
must not clear `replace_data_t` while the flag is set, and `depot_t::convoi_arrived()` must skip the
replacement block while the flag is set so that a later visit does not repeat the purchase and
sale. On departure, clear both. If a save is made between the economic transaction and the
departure, the flag plus `replace_data_t` reconstructs the order. This avoids a heavyweight
per-Convoy order copy.

`replace_data_t` can be shared by several Convoys (`replace_data_t::number_of_convoys`). Deferring
`clear_replace()` until departure keeps the shared object alive for the other Convoys. The
per-Convoy flag must be cleared on departure, sale or destruction, and the shared count must be
decremented exactly once. A Convoy that is destroyed while the flag is set must release its
reference like any other replacement Convoy.

The new fields require the next `EX_SAVE_MINOR` revision. `simversion.h` and the revision array in
`gui/settings_stats.cc` must be updated together [CODE]; see
[savegame versioning](../../savegame-versioning.md). No `NETWORK_VERSION` change is proposed: the
depot order travels in the existing schedule string, the line target and policy travel in tool
commands, and the Withdrawal is a simulation event. The user approved the save revision bump on
2026-11-25 [RECOLLECTION:2026-11-25 user statement]; the precise field list remains subject to the
implementation review. This is a change-restricted area under AGENTS.md rule 5.

Load validation must cover:

- a vehicle with a pending job outside `depot_t::vehicles` (clear or repair);
- a vehicle in `depot_t::vehicles` with a release time in the past (available);
- a Convoy with `replacement_awaiting_formation` but no `replace_data_t` (clear the flag);
- a Held Convoy loaded as an `INITIAL`, line-bound Convoy. `karte_t::load()` calls
  `depot_t::convoi_arrived(cnv, false)` for it; that call must append it to the depot list without
  re-running Decomposition;
- a line target above the number of Convoys the line can physically support (allowed; the line
  stays below target).

## Network and determinism

- The schedule order is part of the schedule string and is committed through
  `TOOL_CHANGE_CONVOI 'g'` or `TOOL_CHANGE_LINE 'g'` [CODE `simtool.cc`]. The line target needs a
  new `TOOL_CHANGE_LINE` case and the same commit path. No UI may mutate live state directly
  [CODE]; see [sync and determinism](../../sync-and-determinism.md).
- Depot Assembly is a simulation event. Every peer must compute it identically at the same sync
  step. The assembly runs in the single-threaded depot step.
- Do not decide from `depot_t::convois` or `simline_t::line_managed_convoys` order. The first is
  rebuilt on load; the second is rebuilt in `convoi_t::finish_rd()` order. Both can differ between
  a long-running server and a joining client. Use `convoihandle_t` ids, line ids and the saved
  `depot_t::vehicles` list order.
- Iterate depots in a deterministic order. `depot_t::all_depots` order should be verified; if it is
  not load-stable, sort by `koord3d` (x, y, z). This is a `[OPEN]` verification item.
- Do not use randomness. If a tie-break between identical vehicles is needed, use the tuple
  (owner, descriptor index, `vehicle_t::get_purchase_time()`, position in the saved pool list).
  `get_purchase_time()` is saved [CODE `vehicle/vehicle.cc`].
- The solver's search order must be deterministic. Reuse the deterministic candidate enumeration
  pattern of `paired_exchange_solver_t` and its explicit candidate limit.
- `depot_t::add_convoi()` creates a `convoi_t`, which advances the `convoihandle_t` allocation
  counter. The counter is part of the checklist, so identical operations on every peer produce
  identical ids [CODE `utils/checklist.h`]; see
  [sync and determinism](../../sync-and-determinism.md).
- Before deleting a Convoy or vehicle, adding a Convoy to the world list, or changing a schedule
  that the path explorer may read, call `world()->await_path_explorer()`. `convoi_t::destroy()`
  already does this for destruction [CODE `simconvoi.cc`]. The assembly path must follow the same
  rule.
- `vehicle_t::calc_route()` is deterministic and is the same call `depot_t::start_convoi()` makes
  [CODE `simdepot.cc`]. The route check must run on the main thread.

## Performance

The measured hot path is `karte_t::sync_list_t::sync_step()` [CODE]; see
[performance](../../performance.md). The design must not add a per-step scan of every depot, every
Convoy and every pooled vehicle.

- Add a per-depot `formation_pending` flag, set by:
  - an arrival with a non-blank order;
  - a vehicle's Release Time being set or reached;
  - a vehicle being added to the pool;
  - an order edit;
  - a line target change;
  - a Deployed Convoy entering a depot and a Held Convoy becoming dispatchable.
- Store the earliest pending Release Time per depot. `depot_t::sync_step()` checks the flag and the
  earliest time; it does O(1) work when nothing is due.
- Run the solver only when the depot is dirty. The pool is usually small; for a large depot, index
  Available Vehicles by waytype and category before matching.
- The depot window refresh is event-driven. Do not recalculate the order verdict every step.
- Measure the depot step on a large save with many depots and pooled vehicles before and after the
  change. The design expects the added cost to be negligible when no formation is pending.

## Failure modes

| Event | Result |
|---|---|
| Blank order | No Decomposition, no re-arrangement; the Convoy leaves together. |
| Order cannot be satisfied | Nothing is dispatched; the Convoy stays in the depot; the owner receives one report naming the first missing or invalid slot. |
| Order excludes the Lead Vehicle | The Original Convoy stays with its Lead Vehicle; a New Convoy may depart if the target allows. |
| Target Fleet Size met | The assembled Convoy is held as a Held Convoy; the depot reports that it is held. |
| Deployed count above target | The line withdraws one Convoy at a time; the Convoy remains Deployed until it enters the chosen depot. |
| Withdrawal cannot route | The owner receives the no-route report; the line tries the next Convoy in the priority order. |
| No route to the next entry | No departure; the vehicles remain in the Depot Vehicle Pool; the owner receives the existing no-route report. |
| Multi-Owner Access Rule denied | No assembly; the owner receives a report naming the owners. |
| Vehicle becomes due while stored | The depot applies the work on selection; the vehicle waits for its Release Time; the cost is booked to its owner. |
| Scrap sale during work | Allowed; the vehicle is deleted and the remaining work is abandoned. |
| Secondhand listing or move during work | Refused until the Release Time. |
| Scrap, listing or move of an Available Vehicle while an order waits | The order remains unsatisfied and the owner receives the missing-slot report. |
| Depot deleted | Refused by `depot_t::is_deletable()` while stock, Convoys or queue entries exist. |
| Line deleted | The existing reference sweeps clear recipients and targets; the depot pool must be added to those sweeps [PROPOSED]. |
| Save or load mid-assembly | The fields above reconstruct the pool, release times and pending replacement; load validation repairs invalid combinations. |
| Client joins mid-assembly | The save contains the pool, release times and flag; the client continues deterministically. |
| Player withdraws the Original Convoy | `convoi_t::destroy()` removes it from the depot lists; its already-detached vehicles remain in the pool; the order may become unsatisfiable and is reported. |
| Held Convoy cannot route from its depot | Reported; the player must move or sell it; the line stays below target. |

## Test plan

- Standard Squirrel suite: 82/82 or better, run with `-nomidi`.
- Pending-runtime maintenance group:
  - rewrite `maintenance-split`, `maintenance-split-invalid`, `maintenance-split-unreachable` and
    `maintenance-cross-convoy` to author Depot Consist Orders;
  - add `depot-order-blank`: a mixed visit with no order leaves one Convoy together;
  - add `depot-order-lead`: the Original Convoy departs with its Lead Vehicle and pool vehicles;
  - add `depot-order-spare`: a New Convoy departs from the pool while the Original is under
    maintenance;
  - add `depot-order-partial`: an unsatisfiable order dispatches nothing and reports the slot;
  - add `depot-order-recipient`: a vehicle reserved to another line is not taken;
  - add `depot-order-owner`: the Multi-Owner Access Rule refuses an assembly;
  - add `depot-order-target`: a Held Convoy is held when the target is met and starts when the
    target is raised;
  - add `fleet-withdraw`: a line above target withdraws the chosen Convoy and the deployed count
    falls when it enters the depot;
  - add `fleet-priority`: the withdrawal priority selects the expected Convoy for a controlled set
    of maintenance, load and distance values;
  - add `fleet-load-factor`: the passenger and goods load factors use the correct numerators and
    denominators, including a mixed line;
  - add `depot-order-save-reload`: save and load between Decomposition and Depot Assembly;
  - add `depot-order-network`: a loopback client joins mid-assembly and the checklist remains
    identical;
  - add `replacement-via-order`: `on_maintenance` and `on_overhaul` replacement, retained stock,
    existing stock and in-place upgrade.
- Two-process determinism run with the same save and tool commands; compare checklists.
- Performance measurement of the depot step on a large save with many depots and pooled vehicles.
