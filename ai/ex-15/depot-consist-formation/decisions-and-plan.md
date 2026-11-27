---
status: draft
verified: none
---
# Depot consist formation: decisions, plan and open questions

Parent: [depot consist formation](../depot-consist-formation.md). This document records the
decisions taken on 2026-11-25, the decisions still open, the implementation order, the
documentation impact and the remaining open questions. Tags: `[CODE]` is verified against the
checked-out tree; `[PROPOSED]` is planned behaviour; `[OPEN]` is unresolved;
`[RECOLLECTION:2026-11-25 user statement]` records a user decision.

## Rationale

An earlier design analysis rejected a vehicle-for-vehicle exchange between two depot Convoys. It
gave two reasons: maintenance is applied on arrival, so an exchange after both Convoys are in the
depot cannot shorten either visit; and `vehicle_t` has no persistent identity, so a promise to
restore the original formation would need new saved and synced state with release hooks on every
fleet operation. The analysis is in `ai/temp/train-overhaul-swap/REPORT.md`.

The Depot Consist Formation design does not exchange vehicles between two Convoys. It decomposes
each arriving Convoy into the Depot Vehicle Pool and re-forms a Consist from the pool according to
an order. No vehicle identity or promise is needed. The vehicles' own counters, ownership and
livery travel with them. The Original Convoy's identity is carried by its `convoi_t` object and its
Lead Vehicle, and a New Convoy is an ordinary `convoi_t` with its own identity. The design
generalises beyond two Convoys and beyond the case in which one train's locomotive and another
train's carriages are due for overhaul while the other vehicles need only routine maintenance.

The existing matcher, coupling checks, Multi-Owner Access Rule, schedule-copy code and depot
vehicle primitives are reused. The main new work is the per-vehicle Release Time, the depot pool
assembly trigger, the Target Fleet Size with dispatch and Withdrawal, and the automatic adjustment.
The code baseline is in [code-baseline](code-baseline.md).

## Decisions taken

1. **Automatic release rule.** A blank Depot Consist Order means no re-arrangement, and
   `release_maintained_vehicles()` is removed. Accepted
   [RECOLLECTION:2026-11-25 user statement].
2. **Lead Vehicle anchor.** The Original Convoy keeps its Lead Vehicle. An order that excludes the
   Lead Vehicle leaves the Original Convoy in the depot while a New Convoy may depart. Accepted.
3. **Target Fleet Size.** The target counts **Deployed Convoys**, meaning Convoys actually running
   on the line, not Convoys in depots. A Convoy in a depot is not counted. Accepted with that
   clarification.
4. **Above the target.** If the line is above the target, it must withdraw Convoys to depots until
   the count returns to the target. The user chose the priority order: most needing maintenance
   first, already heading to a depot second, farthest from a depot in its schedule third, least
   loaded fourth, lowest `convoihandle_t` id last. The rationale is that a Convoy farther from a
   depot serves more of its route before withdrawal, so more passengers and cargo reach their
   intended stops
   [RECOLLECTION:2026-11-25 user statement]. Recorded in
   [fleet-size](fleet-size.md#choosing-which-convoy-to-withdraw).
5. **Automatic fleet adjustment.** The passenger load-factor guard is fixed (done). A goods load
   factor is to be added. Automatic adjustment is in scope for this feature, not deferred. The line
   management window gains a **Fleet** tab after the **Consists** tab. The goods load factor uses a
   new `LINE_GOODS_CAPACITY` line counter; the line chart gains a **Freight capacity** curve with
   its own colour and translation entry. The policy uses the last two complete months, not a
   per-line window setting. Accepted [RECOLLECTION:2026-11-25 user statement].
6. **Fleet tab contents.** The tab shows the number of Convoys on the line, the current average
   loading, the depots served by the line, and for each depot the matching vehicles currently loose
   or under maintenance. Depots without a Depot Consist Order are greyed out with an explanatory
   mouseover; Convoys under maintenance at those depots are still listed under them. The tab states
   the maximum number of Consists that can run at present from the available vehicles. The Consists
   tab gains a **Maintenance** column showing each Convoy's maintenance state. Accepted.
7. **Replacement precedence.** The replacement order takes priority over a schedule order on the
   same visit. The player is warned in the replacement window, and the depot reports that the
   schedule order was not applied. Accepted with the explicit warning.
8. **Recipient, sale and depot limit.** The default Recipient is the same line; `Any` is opt-in. A
   pool vehicle can be scrapped while its work is in progress; secondhand listing and moves wait
   until the Release Time. The selling mechanism is planned to split into immediate scrap and a
   secondhand listing. The current full-value "Refund" for a new vehicle that has never left its
   depot remains the return-to-manufacturer undo. The per-line depot limit remains an arrival
   policy, separate from the target; a Held Convoy and the Depot Vehicle Pool do not count against
   it. The explanation is rewritten in
   [interface](interface.md#recipient-sale-and-the-per-line-depot-limit).
   Accepted after clarification
   [RECOLLECTION:2026-11-25 user statement].
9. **Saved state.** The new fields are a finding, not a recommendation: the design requires them.
   The save revision bump is approved [RECOLLECTION:2026-11-25 user statement]. The fields are listed
   in [remote-and-persistence](remote-and-persistence.md).
10. **Scope.** The Target Fleet Size, Held Convoys, automatic adjustment and the Fleet tab are part
    of the same 15.x package as the Depot Consist Order. Accepted.
11. **UI process.** The detailed interface design is done last, by a subagent, to a high standard.
    The subagent treats [interface](interface.md) as the information architecture and
    [fleet-size](fleet-size.md) as the policy model, and considers the additional information
    players need.
12. **Withdrawal depot and the manual withdraw controls.** A Withdrawal uses the next depot on the
    Convoy's schedule. Only when the schedule has no depot entry does it fall back to the existing
    search for the nearest suitable depot, and then to `home_depot`. The manual withdraw controls
    (the Convoy detail button and the line's **Withdraw All** button) use the same maintenance-type
    `convoi_t::go_to_depot()` call instead of selling the Convoy when it becomes empty.
    [RECOLLECTION:2026-11-25 user statement].
13. **Withdrawal representation.** There is no separate `convoi_t::withdraw_to_depot()`. A
    Withdrawal triggers the existing maintenance-type `convoi_t::go_to_depot()` call for the chosen
    Convoy. Whether the Convoy is held on arrival is decided by the Target Fleet Size check.
    [RECOLLECTION:2026-11-25 user statement].
14. **Per-vehicle maintenance queue.** Depot maintenance capacity is counted per vehicle, not per
    Convoy. Each vehicle has its own wait time and queue position. This applies both to a
    decomposed Convoy and to a Convoy that stays together because its depot entry has a blank
    Depot Consist Order; a Convoy leaves when all of its vehicles have been released.
    [RECOLLECTION:2026-11-25 user statement].
15. **Stored vehicle work.** A vehicle in the Depot Vehicle Pool is maintained or overhauled as soon
    as the work becomes due, unless it is mothballed. The mothballing controls are missing from the
    interface and are deferred to a later session. [RECOLLECTION:2026-11-25 user statement].
16. **Priority order.** Work proceeds in the handoff order: load factors, per-vehicle work, the
    Depot Consist Order and Decomposition, New Convoys, Target Fleet Size and Withdrawal, automatic
    adjustment and the Fleet tab, replacement integration, pooling and diagnostics, then
    persistence and network testing. Each phase is committed only when its tests pass.
    [RECOLLECTION:2026-11-25 user statement].

## Decisions resolved on 2026-11-25

1. **Withdrawal depot.** The next depot on the schedule. Where the schedule has no depot entry,
   fall back to the existing nearest-suitable-depot search and then `home_depot`.
2. **Goods load factor.** `LINE_GOODS_CAPACITY`, booked for freight vehicles only, with
   `LINE_PAYLOAD_DISTANCE / LINE_GOODS_CAPACITY`. `LINE_CAPACITY` is already passenger capacity
   because it is booked only for passenger vehicles; no subtraction is needed.
3. **Mixed lines.** The higher of the passenger and goods load factors.
4. **Withdrawal representation.** Trigger the existing maintenance-type `convoi_t::go_to_depot()`
   call. There is no separate withdrawal operation.
5. **Maximum Consists calculation.** The greedy runtime count, not an optimal packing.
6. **Automatic policy window.** The last two complete months, fixed.

## Implementation order

The phases keep the standard Squirrel suite and the pending-runtime maintenance group passing.
The bug fixes from the design review are complete; the code changes in this list are new feature
work.

1. **Load factors and line statistics.** Implemented. The passenger guard is fixed; the goods
   capacity counter, the goods load factor, the last-two-months getters and the **Freight
   capacity** chart curve are in the tree [CODE].
2. **Per-vehicle work duration and queue.** Implemented. `vehicle_t` has the four job fields, the
   depot has `maintenance_queue`, and `depot_t::start_depot_jobs()`,
   `complete_depot_jobs()` and `reproject_depot_jobs()` run the queue [CODE].
3. **Depot order UI and Decomposition.** Implemented for the Original Convoy arrangement. Depot
   entries show a **Departure consist** tab; a non-blank order decomposes the Convoy and
   `depot_t::try_assemble_convoy()` re-forms it from the pool. `release_maintained_vehicles()` and
   its helpers are removed. The four former `maintenance-split` cases are replaced by
   `depot-order` and `depot-order-exclude` [CODE].
4. **New Convoy arrangement.** Implemented in `depot_t::try_form_new_convoy()`: the pool alone
   satisfies the order, the schedule is copied past the depot with `copy_schedule_past_depot()`,
   the line and livery are inherited, the vehicles are added, the route is checked and the Convoy
   starts through `convoi_t::start()`. The Original Convoy is held by
   `depot_t::hold_original_convoy()`. The runtime test is deferred until a two-locomotive fixture
   exists [CODE].
5. **Target Fleet Size, Held Convoys and Withdrawal.** Implemented.
   `simline_t::target_convoy_count`, `count_deployed_convoys()`, `dispatch_held_convoy()`,
   `withdraw_excess_convoys()` and `withdraw_one_convoy()` are in the tree;
   `depot_t::should_hold_convoy()` and `depot_t::hold_convoy()` hold a Convoy at the target.
   `TOOL_CHANGE_LINE` case `'n'` sets the target and case `'p'` sets the policy. The
   `fleet-target` runtime case covers raising the target, dispatching Held Convoys and lowering it
   to withdraw one [CODE].
6. **Automatic adjustment and the Fleet tab.** Implemented. The `fleet_*` fields,
   `simline_t::adjust_fleet_policy()`, the Fleet tab with target and policy controls, the summary,
   load factors, depot rows and the maximum-consists estimate are in the tree. The target and
   policy controls commit through `TOOL_CHANGE_LINE` [CODE].
7. **Replacement integration.** Build the one-shot order from `replace_data_t`, keep the existing
   replacement UI, and preserve the replacement's purchase, sale and upgrade rules. Test
   `on_maintenance`, `on_overhaul`, retained stock, existing stock and in-place upgrades.
8. **Pooling and diagnostics.** Add the depot pool view, Recipient editing, the "awaiting" report,
   the "target met, held as spare" report and the "withdrawal in progress" report. Add the help
   page.
9. **Persistence, network and performance.** Save and load mid-assembly and mid-withdrawal, join a
   loopback client mid-assembly, verify two-process determinism, and add the dirty-flag and
   wake-up scheduling.
10. **Interface design.** The Fleet tab, the Consists tab Maintenance column and the depot pool view
    are designed by a subagent after the data model and controls exist. The subagent works to the
    information architecture in [interface](interface.md) and [fleet-size](fleet-size.md) and to a
    high visual and interaction standard.

## Documentation impact

When the design is implemented, these documents need changes:

- [depot maintenance](../depot-maintenance.md): replace the automatic-release section with a link
  to this design, and update the per-line limit and resumption order.
- [schedule-and-consists](../schedule-and-consists.md): replace the "Depot-entry orders never
  execute" note; record the depot branch as active; add the depot formation status.
- [recombination-design](../recombination-design.md) and
  [consist recombination](../../consist-recombination.md): state that depot formation is a separate
  use of the same order data, not a stop recombination, and that it has no cargo continuity.
- [economy-and-vehicles](../economy-and-vehicles.md): record the replacement integration and the
  goods load factor.
- [scripting and tests](../../scripting-and-tests.md): add the depot formation and fleet cases, or
  create a dedicated feature test document at `ai/scripting-and-tests/depot-consist-formation.md`
  when the first cases exist.
- [gui](../../gui.md): record the schedule, depot and line controls, including the Fleet tab and the
  Consists tab Maintenance column.
- [ex-15](../../ex-15.md) and [index](../../index.md): update the status registry and retrieval
  keys.

## Open questions

- How should a Held Convoy in a depot that is not on its line's route be dispatched? The design
  checks a route to the next schedule entry and reports failure; the player must move or sell it.
- Does the path explorer's `schedule_t::parse_orders()` need to skip depot orders? The function
  derives carried-category tables from every order [CODE `dataobj/schedule.cc`]. A depot order
  describes a Consist that starts at the depot, not a through journey; treating it as a stop order
  could create invalid through connexions. The design expects a service boundary at the depot.
- How should the Target Fleet Size count a Convoy in `ENTERING_DEPOT`, `ROUTING_1`,
  `LEAVING_DEPOT` or `NO_ROUTE`? The design counts `ENTERING_DEPOT` as inside the depot (not
  Deployed) and `ROUTING_1`/`LEAVING_DEPOT` as Deployed (about to leave). `NO_ROUTE` is counted as
  Deployed but the player receives the existing problem report.
- What happens when a line is deleted while a Held Convoy or pool vehicles name it as recipient?
  The existing reference sweeps clear convoy and line recipients on deletion
  [CODE `simconvoi.cc`; `simline.cc`]; the depot pool must be included in that sweep.
- What happens when a depot is deleted while a Held Convoy or pool vehicles are inside?
  `depot_t::is_deletable()` must account for the pool and pending orders; it already refuses while
  `vehicles`, `convois` or `under_maintenance` contain stock [CODE `simdepot.cc`].
