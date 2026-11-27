---
status: draft
verified: ex-15 @ 6c404918c3 (2026-11-27)
---
# Depot consist formation: fleet and schedule-spacing synchronisation

Parent: [depot consist formation](../depot-consist-formation.md). This document defines how the
line's schedule spacing drives the number of Convoys the line deploys, the Fleet tab controls
that edit the spacing shift at individual stops, and the cross-schedule trace used when Consists
split or join. The schedule spacing mechanism itself is in
[schedule-spacing](../../routing-and-scheduling/schedule-spacing.md).

## Terms used here

The parent document defines [Consist, Convoy, Deployed Convoy, Held Convoy, Target Fleet Size,
Depot Consist Order and Depot Vehicle Pool](../depot-consist-formation.md#terms). In this
document:

- **Frequency** is the schedule spacing: the number of departures per year, where 12 means one
  departure per month. It is stored in `schedule_t::spacing`.
- **Round-trip time** is the time a Convoy takes to complete one cycle of the line's schedule,
  excluding the time it spends waiting for spacing slots. The implementation measures the cycle
  with `convoi_t::get_average_round_trip_time()`, subtracts the spacing wait that the current
  Convoy count can explain, and never goes below the schedule's running-time estimate. When no
  Convoy has completed a cycle, the schedule estimate is the only value.
- **Required Convoys** is the smallest number of Deployed Convoys that can sustain a given
  frequency over that round-trip time. The Fleet tab's controls and labels use "consists" for
  this number; it is a count of `convoi_t` objects, not of vehicle formations.
- **Frequency synchronisation** is the optional mode in which the frequency, not a manually
  entered Target Fleet Size, determines the target.

## The formula

All values are integers; the time unit is ticks.

- `Y = ticks_per_world_month * 12` is the number of ticks in a game year.
- `T` is the round-trip time.
- `F` is the frequency (`schedule_t::spacing`).
- The interval between the line's departures at a stop is `Y / F`.
- With `N` evenly spaced Convoys, the interval is `T / N`.
- The frequency therefore requires `N = ceil(T * F / Y)` Convoys.
- The inverse is `F = Y * N / T`. The Fleet tab shows the required `N`; it does not
  change `F` from the Convoy count.

The frequency is a maximum service level, not a minimum. If the line has more Convoys than
`ceil(T * F / Y)`, the extra Convoys have slack and the spacing mechanism holds them to the
departure slots; the schedule does not become more frequent than `F`. This is why the
synchronisation is one-way: the frequency determines the target, and the target does not
silently change the frequency.

The measured cycle time cannot be used unadjusted. A line with excess Convoys waits longer at
its slots, so its measured cycle time grows; using that value directly would compute the same
excess count again and the line could never shed Convoys. The implementation removes the
spacing wait that `N - 1` intervals can explain (`N` is the current Deployed count) and uses the
larger of that adjusted value and the schedule running-time estimate. The schedule estimate is
the sum of the line's recorded leg times, with the same distance-and-speed fallback as the
service-frequency calculation. It excludes stop dwell, so the adjusted measured value normally
supplies the dwell allowance.

## Fleet tab controls

The Fleet tab adds:

1. **Spacing (departures/year).** The line's `schedule_t::spacing`, with the same step of 12 as
   the schedule window. A clock label shows the interval. The input is enabled only when the
   schedule contains at least one timed stop.
2. **Frequency sets the number of consists.** The synchronisation checkbox. When it is set:
   - the required number of Convoys is computed from the spacing and the measured round-trip
     time;
   - the Target Fleet Size is set to that number;
   - Held Convoys are dispatched and excess Deployed Convoys are withdrawn until the count
     matches, as far as the available Held Convoys and Depot Vehicle Pool allow;
   - the automatic fleet policy is turned off, because that policy would otherwise change the
     target for demand reasons and contradict the frequency.
   The checkbox is enabled only when the schedule has a timed stop and the spacing is non-zero.
3. **Use the same shift at every stop.** The schedule's `same_spacing_shift` setting. When it is
   set, the single **Shift** input applies to every schedule entry. When it is clear, the list
   below shows one Shift input for each timed stop, and each input edits that entry's
   `spacing_shift`.
4. **Set spacing.** One commit through `TOOL_CHANGE_LINE` case `'f'`. The command carries the
   frequency, the same-shift setting, the line shift, the synchronisation flag and the per-stop
   shifts. The commit writes the line schedule, propagates it to the line's Convoys through
   `simlinemgmt_t::update_line()`, and then runs the synchronisation.

The shift is a phase offset within the departure-slot grid. It does not change the required
number of Convoys; only the frequency and the round-trip time do. Editing a shift changes when
a stop's departures fall within the interval, not how many departures there are.

The `spacing_shift_mode` setting still controls whether the shift is available at all:
`SPACING_SHIFT_DISABLED` hides the value, `SPACING_SHIFT_PER_LINE` forces the same-shift
behaviour, and `SPACING_SHIFT_PER_STOP` allows individual shifts.

## When the synchronisation runs

`simline_t::sync_spacing_to_fleet()` recomputes the required count and adjusts the target. It
runs on:

- the monthly line update, after the automatic policy;
- a Convoy joining or leaving the line (`simline_t::add_convoy` / `remove_convoy`), which covers
  a promoted split portion and a Convoy that is destroyed after a join;
- a Convoy entering a depot (`depot_t::convoi_arrived`), where the Deployed count falls;
- the Fleet tab's Set spacing command;
- a line schedule change that is committed through the line tool, after the new schedule has
  been propagated to the Convoys.

It is not called unconditionally from `convoi_t::step()`, `convoi_t::sync_step()` or
`haltestelle_t::step()`. A Convoy joining or leaving a line can be reached while a Convoy is
stepping, so those hooks can run the synchronisation during a step; they are event-driven, not
per-step. The round-trip sample changes only as Convoys complete cycles; the monthly run
incorporates it.

## Cross-schedule trace

`simline_t::collect_coupled_lines()` walks the line's schedule and every schedule reachable
through `couple` and `uncouple` entries, up to the finite set of lines. It reads only saved
schedule data:

- a `couple` entry contributes `target_id_couple`, interpreted as a Convoy when
  `couple_target_is_line_or_cnv` is set and as a line otherwise;
- an `uncouple` entry contributes `target_id_uncouple` with the matching
  `uncouple_target_is_line_or_cnv` flag;
- a target Convoy contributes its line; a lineless target Convoy contributes nothing, because a
  lineless Convoy does not use spacing;
- the line itself is excluded, and a visited set prevents cycles.

The trace answers "which other lines share this line's Consists through splitting or joining?".
It is used after a spacing or schedule commit to re-run the synchronisation on every connected
line that has it enabled, so a change to one schedule cannot leave a coupled line's target based
on an obsolete flow. It is not a count estimator: the runtime is the authority on how many
Convoys a line has.

A portion promoted to a line calls `convoi_t::set_line()`, which copies the target line's
schedule and adds the portion to that line's `line_managed_convoys`. Every line-bound Convoy
therefore runs the line's full cyclic schedule, including a portion that joined at a mid-route
stop. The required-count formula uses `simline_t::count_deployed_convoys()`, which counts those
portions while they are on the line. The trace is needed for invalidation across the schedules,
not to reconstruct the count.

A portion that later splits away again serves only the arc between its join and leave points
until it leaves the line. During that arc its presence is counted, so the formula is
conservative at stops outside the arc: it may keep one more Convoy than the stop's own service
strictly needs. This is safe for the timetable and avoids a second, time-weighted count model.

## Persistence

The synchronisation flag is stored in bit 1 of the existing `simline_t::fleet_policy` byte
(`fleet_policy_sync_spacing`); bit 0 remains the automatic policy. The two bits are mutually
exclusive: enabling either clears the other. This adds no datum to the savegame and therefore
does not change `EX_SAVE_MINOR`. Old saves load the byte as 0 or 1, which leaves the new bit
clear. The schedule spacing and the per-entry shifts were already saved.

## Performance

The synchronisation is event-driven, not a per-step calculation:

- `count_deployed_convoys()` is O(number of the line's Convoys).
- `calc_required_convoys_for_spacing()` is O(number of the line's Convoys + number of schedule
  entries), because it also builds the schedule running-time estimate.
- `dispatch_held_convoy()` and `withdraw_excess_convoys()` are the existing operations; a large
  change to the target can dispatch or withdraw many Convoys, each with its existing route
  search.

The monthly run adds this work once per line with synchronisation enabled. Lines without the
flag, without spacing, or without a timed stop return before the convoy scan. On the gargantuan
fixture the monthly line update is already a measured cost; the synchronisation must remain
gated on the flag and must not add a per-Convoy per-step call.

The cross-schedule trace is O(number of reachable lines × schedule entries). It runs only on a
spacing or schedule commit, not per step. A pathological chain of coupled lines can make one
commit proportionally expensive, but it is bounded by the world's line count and by the visited
set.

## Open questions

- Should the required count use the measured round-trip time of Deployed Convoys only, or also
  include a Held Convoy's last measured time when no Deployed Convoy has completed a cycle? The
  current fallback is the schedule estimate.
- The spacing-wait correction subtracts `(N - 1)` intervals from the measured cycle. Is that a
  close enough model of the waits at a line whose stops have different shifts and whose Convoys
  are not evenly loaded, or should the convoy record the actual spacing wait per cycle?
- Should a line whose timed stops are all served by portions that run different arcs use a
  time-weighted Convoy count instead of `count_deployed_convoys()`? The current conservative
  count is documented above.
- Should the Fleet tab offer a separate "dynamic allocation" mode that raises the target above
  the frequency requirement for demand, or is the frequency the sole determinant while the
  checkbox is set? The current implementation makes the two modes mutually exclusive.
- Should the spacing synchronisation run when a line is loaded with the flag set and no
  month has yet elapsed? The current implementation waits for the next monthly update or an
  explicit Set spacing.
