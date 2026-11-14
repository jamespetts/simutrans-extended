---
status: draft
verified: ex-15 @ 684d570f9 plus working-tree max_tolerable_walk changes (2026-11-14)
---
# Comfort influenced routing: integration

**Covers:** edge weights and through journeys, blended storage, savegame and
network effects, settings, UI, fares, the runtime give-up rule, private cars, and
the implementation sequence. Parent: [comfort-routing](comfort-routing.md). Model:
[comfort-routing-model](comfort-routing-model.md).

Status: design and partial implementation. Statements made by the user on
2026-11-08 are tagged [RECOLLECTION:2026-11-08]; untagged code facts are [CODE] at
the frontmatter commit; derivations are [UNVERIFIED]. Implementation status:
[comfort-routing](comfort-routing.md).

## Edge weights and through journeys

Per connexion `h -> t`, per compartment:

```
waiting(h) comfort cost
+ transfer time(h)
+ ride time comfort cost
```

- Waiting uses the origin halt's effective `(maximum_comfortable_wait,
  maximum_tolerable_wait)` and the expected wait when
  `waiting_comfort_weighting` is on; otherwise the actual wait is used and the
  waiting hard limit is not applied.
- Riding uses the line's mean vehicle `(maximum_comfortable_journey,
  maximum_tolerable_journey)` for the class and the ride time when
  `vehicle_comfort_weighting` is on; otherwise the actual ride time is used and
  the ride hard limit is not applied.
- Walking uses the global `(maximum_comfortable_walk, maximum_tolerable_walk)`
  settings and the actual walk time when `walking_comfort_weighting` is on;
  otherwise the actual walk time is used and the walking hard limit is not
  applied. The two settings are defined in
  [comfort-routing-model](comfort-routing-model.md).
- Within a convoy, the vehicle mean is capacity-weighted. The mean across a line's
  convoys is unweighted.
- A vehicle's contribution for a fare class uses its effective fare class: the
  highest fare class at or below the requested class for which that vehicle has
  accommodation. A class with no usable accommodation in a vehicle contributes
  no capacity there, and its edges are removed only when no transport on the
  line can carry it.
- Each connexion is one continuous ride. Staying aboard is one edge and pays no
  extra cost. A change is subject to the reboarding rule; no fixed boarding cost
  is charged.
- Hard limits remove edges: wait above `maximum_tolerable_wait` when waiting
  comfort is weighted, ride above `maximum_tolerable_journey` when vehicle comfort
  is weighted, walk above `maximum_tolerable_walk` when walking comfort is
  weighted.
- Through edges across couplings use the comfort of the portion the passenger
  stays in and count as one leg; the coupling stop's waiting terms do not apply.
- The first walk from the origin to the first stop and the last walk from the
  last stop to the destination are per-packet costs outside the tables. The same
  walking curve applies to them where a route cost is compared: the choice of
  start halt, the choice of destination halt, and the comparison between public
  transport, the private car and walking all the way. Physical arrival times and
  the fallback walks used when no route exists keep the actual walking time.
- Deduplication compares the full comfort-adjusted cost, so a slower comfortable
  line can beat a faster uncomfortable one.

The routing search prevents a change of transport that would merely get a fresh
comfortable-time allowance for the next leg. The rule needs to know whether the
arriving transport also serves the candidate destination, and if so how long the
passenger would stay aboard it. That information belongs to the
state `(halt, arriving transport)`. Each edge already stores its actual ride time;
the graph build also stores, for each state, the stop-to-stop connections served
by the arriving transport and a small hash table with linear probing from
destination to actual ride time. Each check is therefore constant time on average
instead of searching the stop's connection list. The rule depends only on the
state and the edge being considered, not on the path taken to reach the state, so
the shortest-path search remains exact over the graph that remains after blocked
changes are removed. A separate, standalone implementation of the same search is
the reference for that graph's semantics, not for search optimality.

The `fetch_goods` boarding comparison adds each candidate convoy's ride comfort
surcharge (from the convoy's maximum comfortable and tolerable journey times for
the class) to its arrival time at the next transfer; a candidate whose ride
would exceed its tolerable journey is not waited for. The existing
waiting-tolerance check then compares the extra wait for the faster convoy with
the resulting perceived time saved. The passenger stays aboard when the current
convoy continues. The comparison uses the ride comfort cost only; waiting comfort
remains in the route-table cost and in the rule that discards passengers after
the maximum tolerable wait.

Class choice is time-based: the vehicle's `maximum_comfortable_journey` is
compared with the passenger's ideal comfort time. Paksets that provide comfort
points have those points converted to a time; the class-choice algorithm itself
does not operate on points.

Transfer time itself remains actual time. Overcrowding affects it through the
halt's transfer-time calculation, not through the stop comfort curve; the
separate `waiting_comfort_weighting` switch controls whether the waiting
component is comfort-weighted at all.

## Blended storage and actual-time reconstruction

The matrix stores blended data: one 4-byte cost per cell and the 2-byte next stop.
No separate per-cell actual-time or discomfort array is stored. The runtime
`haltestelle_t::connexion` keeps the actual waiting, journey and transfer
components and gains a comfort surcharge. Then:

- actual route time = sum of waiting + journey + transfer along the next-stop
  chain;
- comfort-adjusted route time = the matrix cell;
- the difference is the comfort surcharge.

The route tab can show both directly for direct connexions. For a route with
changes, a helper follows the stored next-stop chain and sums the components. This
is O(number of legs), typically one to four, and only on display.

The runtime edge record is 24 bytes; on the measured fixture that is about 2 MB
across passenger classes. No per-cell discomfort or actual-time array is added.

## Savegame, pakset and network

The following changes are implemented under the AGENTS.md rule-5 review (see
[savegame-versioning](../savegame-versioning.md) and [network](../network.md)):

- new vehicle fields for `maximum_comfortable_journey` and `maximum_tolerable_journey` per class;
- new building fields for `maximum_comfortable_wait`, `maximum_tolerable_wait` and `comfort_capacity`;
- new city-car fields for `maximum_comfortable_journey` and
  `maximum_tolerable_journey`;
- new settings, including the walking comfort weighting and the maximum
  comfortable and tolerable walk times;
- the extra `connexion` field.

The route tables do not store a route-cost model marker. Instead,
`karte_t::rdwr_gamestate` captures the saved comfort settings immediately after
`settings.rdwr(file)` and compares them with the settings after the simuconf
override passes. A difference sets
`path_explorer_t::set_comfort_settings_overridden_on_loading()`, and
`karte_t::load` rebuilds the saved tables after `path_explorer_t::rdwr(file)`. The
settings window's "Simuconf.tab" and "Default.sve" reset buttons perform the same
comparison. Saves created before the current walking comfort settings force
a route-table rebuild on load when `comfort_routing` is on, because their
stored tables have no walking surcharge or were built with the previous
walking comfort settings. A saved marker remains unimplemented; the
comparison and the version check cover the known cases.

The walking comfort settings are part of the saved settings, so the savegame
revision was raised; `simversion.h` is the authority for the value, and the
version arrays in `gui/settings_stats.cc` must match it. All saves created before
comfort routing itself load with `comfort_routing` off. That value is part of the
saved-game settings; the advanced settings dialogue (the one that changes
saved-game settings) can turn it on. Turning it on runs the normal full route
refresh; no warning is shown. New games start with it on. The measured complete
rebuild is about 57 s with comfort routing off and 69 s with it on (see
[comfort-routing](comfort-routing.md)).

The new vehicle and building fields accept `_minutes` and `_tenths` alternative
pakset keys; makeobj converts minutes to tenths before encoding and rejects an
object that gives both forms of the same field. The new fields are part of the pakset checksum, so a network client with
different pakset values is detected. Paksets without the new time fields remain
valid: vehicles derive `maximum_comfortable_journey` from comfort points, and
objects with neither points nor times use the defaults in the model page.

The line/consist comfort history is stored as the two time values. On load of an
older save, points-based history is converted to `maximum_comfortable_journey`
with
`settings_t::max_tolerable_journey(points)`, the same inverse table used to derive
a vehicle's comfortable time from comfort points; the maximum-tolerable chart
is filled with the current
`maximum_tolerable_journey` value for the whole history, because the older save
has no separate tolerable series. The in-month rolling averages for the convoy
and line comfort series are converted on the same basis, so the first samples
after loading stay on the new scale. This is part of the same savegame revision.

`connexion` is saved in the halt table. Saves without the field need a default and
a refresh; the version condition must read and write the field under the same
version number.

## Settings

The per-object times replace the global comfort-point multiplier table. Every new
key below must also be added to the shipped `simuconf.tab` with the explanatory
comments and default values shown. Block:

```
# Comfort influenced routing.
# All saves created before this feature start with this off; the advanced
# settings dialogue (which changes saved-game settings) can turn it on.
# New games start with it on. Turning it on runs the normal full route refresh.
comfort_routing = 1

# Vehicle ride comfort, waiting comfort and walking comfort can be weighted
# independently. Set any of them to 0 to leave that component out of route
# costs. They are deliberately adjacent so the balance can be tuned together.
vehicle_comfort_weighting = 1
waiting_comfort_weighting = 1
walking_comfort_weighting = 1

# Extra perceived time at 25%, 50%, 75% and 100% of the range from
# maximum_comfortable_journey to maximum_tolerable_journey (or the wait and
# walking equivalents). Values are percentages of the leg's actual time.
comfort_penalty_at_25_percent = 15
comfort_penalty_at_50_percent = 50
comfort_penalty_at_75_percent = 85
comfort_penalty_at_100_percent = 100

# Comfortable journey time assigned to a standing passenger, in minutes. When
# a vehicle or class carries more passengers than its seated capacity, its
# comfortable time becomes the capacity-weighted mean of the seated passengers'
# normal comfortable time and this value. Only the comfortable time is lowered;
# the tolerable journey time is unchanged.
max_comfortable_journey_overcrowded = 5

# Comfortable waiting time when the comfortable facility capacity is full.
# Overcrowding never changes maximum_tolerable_wait.
max_comfortable_wait_overcrowded = 15

# Walking comfort uses the same penalty curve as vehicle and waiting comfort.
# A walk of up to the maximum comfortable walking time counts as its actual
# time. Between the comfortable and the tolerable time, the curve adds extra
# perceived time. Above the maximum tolerable walking time, passengers will
# not walk: a walking link between stops whose walk exceeds it is not
# available, and passengers will not choose a whole journey on foot longer
# than it. Passengers also have a laziness threshold drawn at random between
# the maximum comfortable walking time and the shorter of the maximum
# tolerable walking time and the passenger's journey time tolerance: below the
# threshold, a passenger walks whenever walking is the quickest way to the
# destination; above it, the passenger prefers public transport or their
# private car even when walking would be quicker, unless that alternative is
# outside their journey time tolerance.
#
# Maximum comfortable walking time in minutes. The default of 10 is how long
# people readily walk: surveys find that most will walk 5-10 minutes to a stop
# and few will walk more than 15-20 minutes when another means of travel is
# available. It is also the lower end of the laziness threshold range.
max_comfortable_walk_minutes = 10

# Maximum tolerable walking time in minutes. This is the hard limit described
# above. The default of 210 (3.5 hours) allows the long journeys on foot that
# were common before public transport, such as walking several miles to market
# and back in a day. It also caps the laziness threshold range.
max_tolerable_walk_minutes = 210
```

`comfort_routing` is Extended-specific; its advanced-settings toggle is in the
Extended -> Passengers tab, not the base Routing tab ([gui](../gui.md)).

`comfort_boarding_minutes` is not read. The corresponding saved setting value is
retained only so that 15.10 saves keep their settings layout; it has no effect.

No fixed boarding cost is charged. The stop-side cost is the actual waiting and
transfer time, with the stop comfort adjustment applied to the waiting time; the
reboarding rule prevents a change that only gets a fresh comfortable-time allowance
for the next leg.

The 0% point of the curve is fixed at 100 (actual time); the four settings above
are the tunable points. The default per-object times are:
`maximum_comfortable_journey` 180 minutes where
neither comfort points nor explicit time fields are given;
`maximum_tolerable_journey` = `maximum_comfortable_journey * 3`;
`maximum_comfortable_wait` 180 minutes; `maximum_tolerable_wait` = 540 minutes.
The global walking times are `maximum_comfortable_walk` 10 minutes and
`maximum_tolerable_walk` 210 minutes.

The reboarding rule is part of the route search, not a setting. The
`tolerable_comfort_*` curve is used only to convert comfort points and for the
time-based class-choice comparison. The revenue comfort settings
(`base_comfort_revenue`, `comfort_derating`, the luxury and discomfort limits) are
read for savegame and pakset compatibility but are not used for fares. Settings
that change route weights must force a full refresh.

## UI

- Actual times keep the normal text colour. Comfort-adjusted times are dark blue.
- When comfort routing is disabled, adjusted times are not shown at all. The
  advanced settings dialogue shows the switch; there is no other prominent
  indicator.
- The stop route tab shows the total actual time and, when a comfort surcharge
  applies, the adjusted time in dark blue. Tooltips cover the travel time,
  waiting time and total time; the total tooltip states that the total is
  travel + waiting + transfer, and the comfort-adjusted value has its own
  tooltip.
- A full route window, opened by the open-window button in each by-category
  route row, follows the stored next-stop chain and presents it as a table with
  a compact route/category header, per-stop and per-leg rows and a totals row:
  waiting, travel and transfer times, actual and comfort-adjusted totals,
  vehicle comfort and stop comfort. It copies the route data when it opens, and
  shows the direct service if the chain is unavailable.
- The stop detail and the stop mouseover toolstrip show the stop's comfort values.
  Where a contributing building has a non-zero `comfort_capacity`, show that
  capacity alongside the comfort it provides in the mouseover toolstrip.
- The stop detail shows `maximum_comfortable_wait` and `maximum_tolerable_wait`,
  the contributing facility, and a warning when the expected wait exceeds
  `maximum_tolerable_wait`.
- Vehicle detail and the depot show the descriptor's comfortable and tolerable
  times. The convoy payload detail has a per-vehicle row for each of the two
  times, using the capacity-weighted mean over all passenger classes, plus a
  convoy total; the values include catering and standing. Comfort points are a
  legacy input, not the primary display: the Payload tab shows the vehicle/class
  `max. comfortable / max. tolerable` journey times as a pair in each passenger
  capacity row. The Spec tab's Payload mode also has the per-vehicle mean rows
  and the convoy total.
- The line/consist comfort history chart is time-based: the convoy chart has
  `Max. comfortable journey` and `Max. tolerable journey` series for the mean
  maximum comfortable and maximum tolerable journey times, and the line chart's
  comfort series is labelled `Comfort time`. Values are stored in seconds because
  `gui_chart_t::TIME` formats the axis labels, tooltip and current-value label with
  `sprintf_time_secs`. The maximum comfortable series accounts for overcrowding in
  the same way as the time-based vehicle mean. Points-based history is converted
  to time at load for the comfortable series; the tolerable series uses the
  current value for the whole history of an older save.
- Adjusted times are passenger-only; mail and goods keep actual times.
- The vehicle class manager shows the minimum and maximum comfortable journey
  times instead of comfort points. The goods fare calculator has no comfort input;
  the legacy `comfort` parameter remains in `get_total_fare` for caller
  compatibility but is ignored.
- Raw comfort points are used only as pakset input, in the older-save history
  migration, in the `tolerable_comfort` conversion table, and in the debug
  conversion chart. They are not used for routing, class choice, fares or the
  current player displays.

## Revenue and the runtime give-up rule

The fare calculation in `goods_desc_t::get_total_fare` does not use comfort. The
speed bonus is also not used for fares. The pakset fields and function parameters
remain readable for compatibility.

`haltestelle_t::step` discards a passenger who waits longer than the stop's
`maximum_tolerable_wait` (default 540 minutes); the passenger may walk instead.
This makes the runtime behaviour match the routing hard limit. Mail and goods keep
their separate waiting rule; per-category stop waiting limits are future roadmap
work. `passenger_max_wait` remains an absolute cap; the default 180-minute stop
limit ensures that a passenger cannot wait indefinitely.

## Private cars

Private cars have `maximum_comfortable_journey` and
`maximum_tolerable_journey`. Where neither is specified, they default to 180
minutes comfortable and `maximum_comfortable_journey * 3` tolerable. The car
journey uses the same curve; a journey above `maximum_tolerable_journey` makes the
car unavailable. The car type is drawn exactly once per packet that has access to
a private car (`has_private_car` in
`karte_t::generate_passengers_and_mail`), before the mode choice, from the
era-constrained pool. The draw must not depend on the mode outcome, so every peer
consumes the same random values. The chosen type is passed to every spawned car
and to the return trip. A packet without access does not draw a type.

Future work: separate car-type pools by wealth class, when the graphics and
pakset data exist. A TODO at the draw site records this. Do not add it until those
assets exist; it would be unplanned provision without them.

## Implementation status

- The time fields are in the vehicle, building and city-car descriptors; the
  `_minutes` and `_tenths` pakset forms are accepted; the comfort-point conversion
  is kept; readers, writers, checksums and pak versions are updated.
- All new `simuconf.tab` keys are present with the explanatory comments and
  defaults shown above; the saved `comfort_routing` control is in the advanced
  settings dialogue.
- The halt effective comfort, the comfort-capacity mean and the overcrowded
  defaults are implemented.
- The edge cost, the reboarding rule, the weighting switches (vehicle, waiting
  and walking) and the savegame/version changes, including the comfort-history
  migration, are implemented.
- The walking comfort cost covers the stored walking edges, the choice of start
  and destination halt and the all-walking mode comparison.
- Boarding, rerouting and the runtime give-up are updated.
- The stop tooltip and comfort-capacity display, the pair of line/consist charts
  and the dark-blue adjusted times are implemented; comfort is removed from fares.
- Test: unit and Squirrel scripting checks for the curve, conversion, capacity
  mean, hard limits and the weighting switches; network loopback; performance run;
  pakset calibration (checking the shipped time values against realistic journeys).
  The network loopback and the complete-rebuild performance run have passed; the
  Squirrel checks and pakset calibration remain.

## Open questions

- The reboarding comparison is deliberately conservative: it uses the arriving
  transport's actual rather than comfort-adjusted remaining ride, so a change may
  be blocked when the arriving leg's accumulated discomfort would have made it
  worthwhile. Whether to use a time band in the state or a less conservative
  threshold is open.
- Whether the stop hard limit uses the mean wait or a sample-based statistic.
- Whether `passenger_max_wait` should remain an absolute cap or become a fallback
  when the stop limit applies.
- A passenger who has waited too long and walks to the destination or the next
  transfer, and a passenger who walks because no public transport route exists,
  use the actual walking time. The walking comfort limit therefore does not
  remove those fallback walks. Whether those walks should be refused above
  `maximum_tolerable_walk` is open.
