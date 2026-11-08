---
status: draft
verified: ex-15 @ 8109f6dab (core implementation, private-car mode choice, target-specific reboarding, effective fare class fallback, no fixed boarding cost, route-tab time tooltips and settings-tab placement; 2026-11-08)
---
# Comfort influenced routing: integration

**Covers:** edge weights and through journeys, blended storage, savegame and
network effects, settings, UI, fares, the runtime give-up rule, private cars, and
the implementation sequence. Parent: [comfort-routing](comfort-routing.md). Model:
[comfort-routing-model](comfort-routing-model.md).

Status: design and partial implementation. User decisions of 2026-11-08 are
tagged [RECOLLECTION:2026-11-08]; untagged code facts are [CODE] at the
frontmatter commit; derivations are [UNVERIFIED]. Implementation status:
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
- Within a convoy, the vehicle mean is capacity-weighted as now. The mean across a
  line's convoys is unweighted.
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
  is weighted.
- Through edges across couplings use the comfort of the portion the passenger
  stays in and count as one leg; the coupling stop's waiting terms do not apply.
- Walking edges use actual time in the first implementation; first and last walks
  remain per-packet costs outside the tables.
- Deduplication compares the full comfort-adjusted cost, so a slower comfortable
  line can beat a faster uncomfortable one.

The routing search adds the Decision 3 reboarding filter. The filter depends on
the arriving transport's comfort and on the arriving transport's edge to the
candidate target, so it changes [routing-search-kernel](routing-search-kernel.md)
semantics and must be verified against the standalone oracle. The expanded state
`(halt, arriving transport)` already contains the arriving transport. Each edge
also stores its actual journey time, and each halt's out-edges are sorted by
transport and target so the arriving transport's edge to the same target can be
found efficiently. The filter is a static predicate on the state and edge, so the
Dijkstra remains exact over the filtered graph; the oracle check is for the
filtered graph's semantics, not for search optimality.

Boarding at runtime must compare convoys by comfort-adjusted cost to the next
stop, not only by earliest arrival, and the passenger must stay aboard when the
current convoy continues. This is Decision 7.

Class choice is time-based. The current points algorithm compares
`max_tolerable_journey(comfort)` with the passenger's ideal comfort time; the
equivalent time value is the vehicle's `maximum_comfortable_journey`, which is
what the old conversion produces from the comfort points. The old comfort points
are converted to a time for old paksets, but the class-choice algorithm itself
does not operate on points.

Transfer time itself remains actual time. Overcrowding affects it through the
halt's transfer-time calculation, not through the stop comfort curve; the
separate `waiting_comfort_weighting` switch controls whether the waiting
component is comfort-weighted at all.

## Blended storage and actual-time reconstruction

Blended storage B is approved. The matrix keeps one 4-byte cost per cell and the
2-byte next stop. The runtime `haltestelle_t::connexion` keeps the actual waiting,
journey and transfer components and gains a comfort surcharge. Then:

- actual route time = sum of waiting + journey + transfer along the next-stop
  chain;
- comfort-adjusted route time = the matrix cell;
- the difference is the comfort surcharge.

The route tab can show both directly for direct connexions. For a route with
changes, a helper follows the stored next-stop chain and sums the components. This
is O(number of legs), typically one to four, and only on display.

The matrix layout and memory are unchanged. The runtime edge record grows from 20
to 24 bytes; on the measured fixture that is about 2 MB across passenger classes.
No per-cell discomfort or actual-time array is added.

## Savegame, pakset and network

The changes below require the AGENTS.md rule-5 review before implementation
(see [savegame-versioning](../savegame-versioning.md) and
[network](../network.md)):

- new vehicle fields for `maximum_comfortable_journey` and `maximum_tolerable_journey` per class;
- new building fields for `maximum_comfortable_wait`, `maximum_tolerable_wait` and `comfort_capacity`;
- new city-car fields for `maximum_comfortable_journey` and
  `maximum_tolerable_journey`;
- new settings;
- the extra `connexion` field;
- a route-cost model marker so a table built with different weights is discarded
  and rebuilt.

This is a savegame-format change. `EX_SAVE_MINOR` must be stepped and the
`gui/settings_stats.cc` version arrays updated together. All saves created before
this feature load with `comfort_routing` off. The value is part of the saved-game
settings; the advanced settings dialogue (the one that changes saved-game
settings) can turn it on. Turning it on runs the normal full route refresh; no
warning is shown. New games start with it on. The measured full refresh is about
68 seconds on the gargantuan fixture.

The new vehicle and building fields accept `_minutes` and `_tenths` alternative
pakset keys; makeobj converts minutes to tenths before encoding and rejects an
object that gives both forms of the same field. The new fields are part of the pakset checksum, so a network client with
different pakset values is detected. Old paksets remain valid: vehicles derive
`maximum_comfortable_journey` from comfort points, and objects without either old
points or new times use the defaults in the model page.

The line/consist comfort history changes from comfort points to the two time
values. On load of an older save, the existing points-based history is converted
to `maximum_comfortable_journey` with
`settings_t::max_tolerable_journey(points)`, the same inverse table used by
Decision 4; the maximum-tolerable chart is filled with the current
`maximum_tolerable_journey` value for the whole history, because the older save
has no separate tolerable series. The in-month rolling averages for the convoy
and line comfort series are converted on the same basis, so the first samples
after loading stay on the new scale. This is part of the same savegame revision.

`connexion` is saved in the halt table. Old saves need a default for the new
field and a refresh; the version condition must read and write the field under the
same version number.

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

# Vehicle ride comfort and waiting comfort can be weighted independently.
# Set either to 0 to leave that component out of route costs. These two
# settings are deliberately adjacent so the balance can be tuned together.
vehicle_comfort_weighting = 1
waiting_comfort_weighting = 1

# Extra perceived time at 25%, 50%, 75% and 100% of the range from
# maximum_comfortable_journey to maximum_tolerable_journey (or the wait
# equivalents). Values are percentages of the leg's actual time.
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
```

`comfort_routing` is Extended-specific; its advanced-settings toggle is in the
Extended -> Passengers tab, not the base Routing tab ([gui](../gui.md)).

The former `comfort_boarding_minutes` key is no longer read. The corresponding
saved setting value is retained only so that 15.10 saves keep their settings
layout; it has no effect.

No fixed boarding cost is charged. The stop-side cost is the actual waiting and
transfer time, with the stop comfort adjustment applied to the waiting time; the
reboarding rule prevents a change that only resets the comfort clock.

The 0% point of the curve is fixed at 100 (actual time); the four settings above
are the tunable points. The default per-object times are:
`maximum_comfortable_journey` 180 minutes where
neither old comfort points nor explicit time fields are given;
`maximum_tolerable_journey` = `maximum_comfortable_journey * 3`;
`maximum_comfortable_wait` 180 minutes; `maximum_tolerable_wait` = 540 minutes.

The reboarding rule is part of the route search, not a setting. The old
`tolerable_comfort_*` curve remains for converting comfort points and for the
time-based class-choice comparison. The old revenue comfort settings
(`base_comfort_revenue`, `comfort_derating`, the luxury and discomfort limits)
remain readable for savegame and pakset compatibility but are no longer used for
fares. Settings that change route weights must force a full refresh.

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
- A full route view follows the next-stop chain and lists each leg with actual
  time, adjusted time, vehicle comfort and stop comfort.
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
- The line/consist comfort history chart becomes time-based: the convoy chart has
  `Max. comfortable journey` and `Max. tolerable journey` series for the mean
  maximum comfortable and maximum tolerable journey times, and the line chart's
  comfort series is relabelled `Comfort time`. Values are stored in seconds
  because `gui_chart_t::TIME` formats the axis labels, tooltip and
  current-value label with `sprintf_time_secs`. The maximum comfortable
  series accounts for overcrowding in the same way as the old chart. Existing
  points-based history is converted to time at load for the comfortable series;
  the tolerable series uses the current value for the whole history of an older
  save.
- Adjusted times are passenger-only; mail and goods keep actual times.
- The vehicle class manager shows the minimum and maximum comfortable journey
  times instead of comfort points. The goods fare calculator no longer has a
  comfort input; the legacy `comfort` parameter remains in `get_total_fare` for
  caller compatibility but is ignored.
- Raw comfort points remain only as pakset input, in the old-save history
  migration, in the legacy `tolerable_comfort` conversion table, and in the
  debug conversion chart. They are not used for routing, class choice, fares or
  the current player displays.

## Revenue and the runtime give-up rule

- Remove the comfort block from `goods_desc_t::get_total_fare`. The speed bonus
  remains removed from fares as previously decided. Keep the pakset fields and
  function parameters readable for compatibility; mark them deprecated in the
  code, not removed.
- Replace the speed-bonus give-up rule in `haltestelle_t::step` with the stop's
  `maximum_tolerable_wait` (default 540 minutes). A passenger who waits longer is
  discarded or walks. This makes the runtime behaviour match the routing hard
  limit. Mail and goods keep their current behaviour; per-category stop waiting
  limits are the separate roadmap item.
- `passenger_max_wait` can remain as an absolute cap or be reduced to a fallback;
  the default 180-minute stop limit ensures there is no infinite wait.

## Private cars

Private cars get `maximum_comfortable_journey` and
`maximum_tolerable_journey`. Where neither is specified, they default to 180
minutes comfortable and `maximum_comfortable_journey * 3` tolerable. The car
journey uses the same curve; a journey above `maximum_tolerable_journey` makes the
car unavailable. The car type is drawn exactly once per packet that has access to
a private car (`has_private_car` in
`karte_t::generate_passengers_and_mail`), before the mode choice, from the
era-constrained pool. The draw must not depend on the mode outcome, so every peer
consumes the same random values. The chosen type is passed to every spawned car
and to the return trip. A packet without access does not draw a type.

Future: separate car-type pools by wealth class, when the graphics and pakset
data exist. A TODO at the draw site records this. Do not add it now; it would be
unplanned provision without the assets.

## Implementation sequence

1. Add the time fields to vehicle, building and city-car descriptors; accept the
   `_minutes` and `_tenths` pakset forms; keep the comfort-point conversion;
   update readers, writers, checksums and pak versions.
2. Add every new `simuconf.tab` key with the explanatory comments and defaults
   shown above, and add the saved `comfort_routing` control to the advanced
   settings dialogue.
3. Implement the halt effective comfort, the comfort-capacity mean and the
   overcrowded defaults.
4. Add the edge cost, the reboarding rule, the two weighting switches and the
   savegame/version changes, including the comfort-history migration.
5. Update boarding, rerouting and the runtime give-up.
6. Add the stop tooltip and comfort-capacity display, the pair of line/consist
   charts, and the dark-blue adjusted times; then remove comfort from fares.
7. Test: unit and Squirrel checks for the curve, conversion, capacity mean, hard
   limits and the weighting switches; network loopback; performance run; pakset
   calibration.

## Open questions

- The reboarding comparison is deliberately conservative: it uses the arriving
  transport's actual rather than comfort-adjusted remaining ride, so a change may
  be blocked when the arriving leg's accumulated discomfort would have made it
  worthwhile. Whether to use a time band in the state or a less conservative
  threshold is open.
- Whether the stop hard limit uses the mean wait or a sample-based statistic.
