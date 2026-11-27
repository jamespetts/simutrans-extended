---
status: draft
verified: ex-15 @ 6c404918c3 (2026-11-27); save conversion @ ex-15 @ 55c91fcd7b (2026-11-27)
---
# Schedule spacing

**Covers:** dataobj/schedule.{cc,h}, dataobj/schedule_entry.h, dataobj/settings.{cc,h}
(`spacing_shift_mode`, `spacing_shift_divisor`), gui/schedule_gui.cc, simconvoi.cc
(`convoi_t::check_departure`, `convoi_t::book_departure_time`), simhalt.cc
(`haltestelle_t::get_queue_pos`, `haltestelle_t::calc_line_interval`), simline.cc
(`simline_t::calc_departures_scheduled`, `simline_t::get_service_frequency`).

Schedule spacing is the timetable frequency of a line: it makes convoys wait for a departure
slot at selected stops instead of leaving as soon as they are ready. This document records the
current implementation. It is the reference for work that couples the spacing setting to the
line's number of convoys. The Fleet-tab synchronisation that derives the Target Fleet Size from
the spacing and the measured round-trip time is in
[fleet and schedule-spacing synchronisation](../ex-15/depot-consist-formation/fleet-spacing-sync.md).

## Terms

- **Timed stop.** A schedule entry at which spacing can take effect: an entry with
  `minimum_loading > 0` or the `wait_for_time` flag. Spacing has no effect at other entries.
- **Schedule spacing.** `schedule_t::spacing`, an integer count of departures per year. A value
  of 12 means one departure per month and a value of 144 means twelve departures per month
  (one every two and a half days in a 30-day month). The value is therefore in twelfths of a
  departure per month; the GUI step is 12. Zero disables spacing. The field is `sint16`.
- **Spacing interval.** The number of ticks between successive departure slots at a timed stop:
  `ticks_per_world_month * 12 / spacing`. The multiplication by 12 converts the monthly tick
  count to a yearly tick count, matching the departures-per-year unit of `spacing`.
- **Departure slot.** One instant on the grid `spacing_shift + k * spacing_interval`, where
  `k` is an integer and `spacing_shift` is the entry's shift in ticks. A convoy that is subject
  to spacing may not leave before its assigned slot.
- **Spacing shift.** `schedule_entry_t::spacing_shift`, an offset within the spacing interval.
  It is expressed in units of `1 / settings_t::get_spacing_shift_divisor()` of a month; the
  default divisor is `24 * 60`, so one unit is one minute of the 24-hour clock that a game month
  displays. It is used to stagger departures at different stops so that they do not all fall at
  the same time.
- **Same-shift setting.** `schedule_t::same_spacing_shift`, a boolean, default true. When true,
  an edit to one entry's spacing shift is written to every entry of that schedule. When false,
  the edit applies only to the selected entry.
- **Spacing shift mode.** `settings_t::spacing_shift_mode`, an enum with the values
  `SPACING_SHIFT_DISABLED`, `SPACING_SHIFT_PER_LINE` and `SPACING_SHIFT_PER_STOP`; the default is
  `SPACING_SHIFT_PER_STOP`. In `SPACING_SHIFT_DISABLED` mode the GUI does not offer a shift
  control. In `SPACING_SHIFT_PER_LINE` mode the shift control is offered but the same-shift
  setting is not exposed; the schedule keeps its stored value, which is true unless an older
  schedule cleared it. In `SPACING_SHIFT_PER_STOP` mode the same-shift setting is shown and can
  be turned off for individual entries.

## Data model and GUI

- `schedule_t` owns one `spacing` value. It is copied by `schedule_t::copy_from` and compared by
  `schedule_t::matches`. Every `schedule_entry_t` owns one `spacing_shift` and one
  `unique_entry_id`; the shift is copied with the entry.
- The schedule editing window (`schedule_gui_t`) exposes spacing only for a line schedule, not
  for a convoy schedule: the controls are built when `cnv.is_bound()` is false. The label is
  "Spacing (departures/year)" and the number input uses a step of 12, which is one departure per
  month; the adjacent clock label shows `ticks_per_world_month * 12 / spacing` converted to a
  clock time.
- The shift control is built when `spacing_shift_mode` is not `SPACING_SHIFT_DISABLED`. The
  same-shift button is built only when the mode is `SPACING_SHIFT_PER_STOP`. The handlers are
  the `numimp_spacing` and `numimp_spacing_shift` cases in `schedule_gui_t::action_triggered`.
- Setting `wait_for_time` on an entry that has no spacing forces `spacing` to 12 (one departure
  per month) in `schedule_gui_t::update_selection`, because a timed wait without spacing would
  have no slot to wait for.
- On load, `schedule_t::rdwr` clears `minimum_loading` when `wait_for_time` is set: the two
  waiting modes are mutually exclusive.
- Schedule strings (`schedule_t::sprintf_schedule` / `sscanf_schedule`) carry `spacing`,
  `same_spacing_shift` and each entry's `spacing_shift`. The string format is also the network
  format for the line and convoy tools.

## Runtime at a stop

Spacing is enforced in `convoi_t::check_departure(halt)`, which runs while the convoy is at a
stop and before the schedule is advanced.

1. `wait_for_time` is true when the convoy is in `LAYOVER`, or when the current entry has the
   `wait_for_time` flag and the schedule has a non-zero spacing. The second condition exists
   because a convoy's copied schedule can diverge from its line's schedule.
2. `earliest_departure_time` is `arrival_time + current_loading_time - reversing_time`, where
   `reversing_time` is the entry's reverse delay when the entry reverses.
3. If the convoy is not waiting for a load threshold or for a time, it leaves at
   `max(earliest_departure_time, arrival_time)` and spacing does not apply.
4. Otherwise, if the convoy is line-bound, the schedule has non-zero spacing and the line has at
   least one convoy, the spacing slot is computed:
   - `spacing_ticks = ticks_per_world_month * 12 / spacing`;
   - `spacing_shift = entry.spacing_shift * ticks_per_world_month / spacing_shift_divisor`;
   - `wait_from_ticks = ((now + reversing_time - spacing_shift) / spacing_ticks) * spacing_ticks
     + spacing_shift`, using integer division, so it is the start of the current slot grid;
   - `queue_pos = halt->get_queue_pos(self)` when the halt is bound, otherwise 1;
   - `go_on_ticks_spacing = wait_from_ticks + spacing_ticks * queue_pos - reversing_time`.
5. `go_on_ticks_waiting` is the entry's maximum wait when `waiting_time_shift > 0`, otherwise
   `WAIT_INFINITE`. `go_on_ticks` is `max(earliest_departure_time, min(go_on_ticks_spacing,
   go_on_ticks_waiting))`. If the maximum wait is earlier than the spacing slot, the convoy is
   `running_late` and leaves at `earliest_departure_time` when it is a timed wait.
6. If spacing is set but the convoy has no line, the spacing slot is replaced by
   `earliest_departure_time` so that the convoy cannot wait forever.

The departure condition is `can_go = loading_level >= loading_limit && (now >= go_on_ticks ||
!wait_for_time)`, with additional alternatives for `now >= go_on_ticks && !wait_for_time`,
`running_late` and `no_load`, followed by `now > earliest_departure_time`.

The practical effect differs by waiting mode:

- At a `minimum_loading` stop without `wait_for_time`, a convoy that reaches its load threshold
  leaves immediately, even before the spacing slot. A convoy that does not reach the threshold
  waits until the earlier of the spacing slot and the maximum wait, then leaves. Spacing is
  therefore a fallback departure time at a load-limited stop, not a hard timetable.
- At a `wait_for_time` stop (including `LAYOVER`), the convoy waits for the spacing slot and
  leaves at it even if it is not full. This is a hard timetable.

### Queue position

`haltestelle_t::get_queue_pos(cnv)` returns a one-based position among convoys of the same line
that are loading at the same stop and the same schedule entry in the same direction. It counts
a convoy when all of the following hold: it is bound and at this halt; its line equals the
querying convoy's line; and either its current entry and reverse-schedule flag equal the
querying convoy's, or (on a non-road waytype) it is `REVERSING` and the querying convoy still
has more than 100 units of loading time and its entry is adjacent in the direction implied by
the reverse flag. For road vehicles the loading test is also required. A convoy that is not in
the list
gets position 1, so the first convoy uses the first slot and later arrivals use later slots.

## Timetable projection

`convoi_t::book_departure_time(time)` records the actual departure time at the stop just left and
then projects arrival and departure times for the following stops. At each future timed stop it
counts earlier estimated departures of the same line at that halt and multiplies the spacing
interval by that count, so the projected departure uses the next free slot rather than the
nearest one. The projection reads the future entry's own `spacing_shift` and
`waiting_time_shift`, so each stop's shift applies to the projection of that stop. The
projection also increments `LINE_DEPARTURES` when the entry being left has a load threshold or a
timed wait and the schedule has spacing.

## Service frequency and statistics

- `simline_t::calc_departures_scheduled()` returns the number of timed stops in the schedule
  multiplied by `spacing`, divided by 12. It is the line's scheduled departures per month across
  all timed stops and is stored in the line's monthly financial history as
  `LINE_DEPARTURES_SCHEDULED`. The finance-history comparison with the actual monthly
  `LINE_DEPARTURES` therefore uses the same unit.
- `simline_t::get_service_frequency()` averages `convoi_t::get_average_round_trip_time()` over
  the line's convoys that are not in a depot, divides by the total number of line convoys, and
  raises the result to the spacing interval when spacing is non-zero. The spacing interval in
  this method is computed as `ticks_per_world_month * 12 / spacing`, matching
  `check_departure` and `haltestelle_t::calc_line_interval`.
- `haltestelle_t::calc_line_interval(line, timing, calls)` divides the line's round-trip time
  by `line->count_convoys()` and by the number of calls at the halt, then raises the interval to
  `ticks_per_world_month * 12 / spacing`. `count_convoys()` includes convoys in depots and held
  convoys, so the count is the line's fleet, not its deployed fleet.

## Savegame and network

- `schedule_t::rdwr` writes `spacing` as a `sint16` for Extended version 15 and later. Extended
  versions 9 to 14 stored the value divided by 12 (whole departures per month); loading such a
  save multiplies the stored value by 12 and clamps it to the `sint16` range. Saving to a 9-14
  target divides by 12, so sub-monthly precision is truncated in that direction. `spacing_shift`
  is written per entry for Extended version 9 and later, and `same_spacing_shift` after the
  entry list.
- The schedule string carries the same values through `sprintf_schedule` and
  `sscanf_schedule`; the line and convoy schedule tools use that string, so a change to the
  string format is a network-visible change.
- The values are simulation state: they affect when convoys depart, so they must be saved and
  must be identical on every peer. Changes to the serialisation or string format are
  change-restricted under [savegame-versioning](../savegame-versioning.md) and
  [sync-and-determinism](../sync-and-determinism.md).

## Current limits

- Spacing is line-wide. A schedule has one `spacing` value, and every timed stop uses it. The
  per-entry field is a time shift, not a separate frequency.
- Spacing is enforced only for convoys that are bound to a line. A convoy's schedule copy can
  carry a spacing value, but the runtime ignores the slot when the convoy has no line.
- Spacing does not reserve a platform or a path; queue position orders the wait within the
  convoys that are already at the stop. A convoy that is still travelling consumes no slot.
- The GUI does not show which entries have a non-zero shift on the schedule list, and the line
  schedule's shift control edits the selected entry only when the same-shift setting is off.

## Open questions

- The departure slot uses only the configured spacing. The Fleet-tab synchronisation derives
  the Target Fleet Size from the configured spacing and the measured round-trip time, but it
  does not adjust the spacing itself. Whether the slot interval should also follow the measured
  round-trip time is open.
- Should `count_convoys()` in `haltestelle_t::calc_line_interval` be replaced with
  `count_deployed_convoys()` so that held and depot convoys do not shorten the advertised
  service interval? The same question applies to `simline_t::get_service_frequency()`.
