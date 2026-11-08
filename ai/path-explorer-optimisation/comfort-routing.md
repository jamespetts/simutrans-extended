---
status: draft
verified: ex-15 @ f35b6fac7 working tree (core implementation, private-car mode choice, target-specific reboarding, effective fare class fallback, zero comfort-point handling and no fixed boarding cost; 2026-11-08)
---
# Comfort influenced routing (design)

**Covers:** the design for comfort influenced passenger routing: time-based comfort
measurement for vehicles and halts, the route-cost curve, stop comfort and
capacity, blended route storage, UI, the fare and give-up changes, and
private-car comfort. Parent: [path-explorer-optimisation](../path-explorer-optimisation.md).
Roadmap: [project-roadmap](../project-roadmap.md) ("Comfort influenced routing",
"Waiting time limits and stop facilities", "Different private cars for different
levels of passenger wealth").

Status: design and partial implementation. User decisions of 2026-11-08 are
tagged [RECOLLECTION:2026-11-08]; untagged code facts are [CODE] at the
frontmatter commit; derivations are [UNVERIFIED]. The implementation status is
recorded below.

Detail pages:

- [comfort-routing-model](comfort-routing-model.md) - the time-based comfort
  model, the cost curve, the split-incentive analysis, and stop comfort and
  capacity.
- [comfort-routing-integration](comfort-routing-integration.md) - edge weights,
  storage and savegame, settings, UI, fares, give-up and private cars, and the
  implementation sequence.

## Goal

Routing as realistic as possible: both route choice and the journey-time tolerance
test respond to comfort [RECOLLECTION:2026-11-05]. People pay per unit of distance;
they choose one line over another for comfort, pay to upgrade for more comfort, and
do not ride at all if comfort is too low [RECOLLECTION:2026-11-05].

The governing principle is that acceptable comfort depends on the duration of the
journey. A short ride in a bare vehicle is acceptable; the same conditions over a
long journey are not. Luxury above the acceptable level for the journey affects
class choice only, not the time cost [RECOLLECTION:2026-11-08].

## Mechanics before the change

This section records the baseline that comfort influenced routing replaced.

- `settings_t::tolerable_comfort`: integer piecewise-linear table, journey time
  (tenths of minutes) to required comfort points. `settings_t::max_tolerable_journey`
  is its inverse: comfort points to maximum tolerable journey seconds. Existing
  vehicle comfort is a 0-255 byte per class; 100 is the ordinary baseline.
- The old `convoi_t::get_comfort` was a capacity-weighted mean over the convoy's
  vehicles, including catering. The old `vehicle_t::get_comfort` lowered the value
  when passengers stand (standing comfort was capped at 10 points). These
  points-based getters have been removed; the time-based getters replace them.
- `haltestelle_t::connexion` stores waiting time, journey time and transfer time;
  it has no comfort field.
- The old `goods_desc_t::get_total_fare` applied a comfort bonus or penalty to
  fares. `fetch_goods` used comfort points for class choice and for waiting for a
  better class. Routing ignored comfort. Fares no longer use comfort, and class
  choice now compares time values.
- `haltestelle_t::step` discarded packets whose wait exceeded
  `passenger_max_wait / speed_bonus`; the stop-based give-up rule replaces it.
- Stop buildings had no comfort attribute. Extension buildings add capacity,
  layover and replenishing flags, but do not improve passenger comfort.

## Decisions of 2026-11-08

1. **Storage.** Blended storage (B) is approved: one 4-byte cost per matrix cell
   plus the 2-byte next stop. No separate per-cell actual-time or discomfort array.
2. **Comfort is measured as time, not as points.** A vehicle has a maximum
   comfortable journey time and a maximum tolerable journey time. A halt has a
   maximum comfortable waiting time and a maximum tolerable waiting time. At or
   below the comfortable time, actual time is used. Between the comfortable and
   tolerable times, a scaled increase factor applies; it is small for short
   durations or a small shortfall, and grows, possibly in an S-curve, as either
   increases. Above the tolerable time, the leg is refused. Comfort above the
   comfortable time is luxury and affects class choice only.
3. **Per-leg model with reboarding mitigation (approved).** Comfort is assessed
   per leg, not over the whole journey. A literal whole-journey threshold is not
   decomposable in the current one-route-per-pair design. To prevent splitting a
   long ride merely to reset the comfortable-time threshold, a change to a
   different transport with equal or lower comfort is allowed when the arriving
   transport does not serve the same target, or when the change's full perceived
   cost is less than the arriving transport's actual remaining ride to that
   target. A change to a higher-comfort transport remains allowed. Walking is
   exempt. No fixed boarding cost is charged; the actual stop times and the
   reboarding rule carry the mitigation. The analysis is in
   [comfort-routing-model](comfort-routing-model.md).
4. **Backward compatibility.** Old comfort points are preserved indefinitely, not
   deprecated. A vehicle's comfortable time defaults to
   `max_tolerable_journey(comfort[class])`, converted from seconds to tenths of
   minutes. Explicit new time fields override the conversion. The existing
   maximum-comfortable-journey-time display remains.
5. **Stops.** A halt's passenger comfort is the comfort of its highest-comfort
   passenger-enabled building. Comfort capacity is included: each building may
   have a `comfort_capacity`, and the halt's effective comfort is the
   passenger-weighted mean of the best comfort levels actually available to the
   waiting passengers. Zero or absent capacity means unlimited. The remainder gets
   the base or overcrowded comfort. This is the same principle as vehicle mean
   comfort. The default comfortable wait is 3 hours and the default tolerable
   wait is 9 hours (three times the comfortable wait).
6. **Overcrowding.** Use the mean comfort for the relevant class as now. Standing
   values must be used, for vehicles and for stops, or overcrowding has no
   realistic effect. The measured mean applies on the next path refresh.
7. **Boarding.** Approved as designed: compare convoys by comfort-adjusted cost, not
   only arrival time; stay aboard when the current convoy continues to the next
   stop.
8. **Settings and migration.** The global comfort-point multiplier table is
   replaced by the per-object comfortable and tolerable times plus a penalty curve
   whose points are set in simuconf.tab. Vehicle and waiting comfort have separate
   weighting switches, placed together with one comment block. All new settings
   are added to the shipped simuconf.tab with comments and defaults. Old saves
   load with comfort routing off; the advanced settings dialogue can turn it on,
   with no warning. New games start with it on.
9. **UI.** Comfort-adjusted times are shown in dark blue and are hidden entirely
   when comfort routing is disabled. There is no prominent "comfort routing
   enabled" indicator; the presence of the adjusted times is the indication. The
   stop mouseover toolstrip shows non-zero comfort capacities with the comfort
   they provide. The line/consist comfort history chart is replaced by a pair of
   time-based charts for mean maximum comfortable and maximum tolerable journey
   times; older points-based history is converted at load.
10. **Companion changes.** Remove comfort from fares directly. Discard passengers
    on the maximum tolerable waiting time. Private cars use the two time values,
    not comfort points. The car type is drawn once per packet that has access to
    a private car, before the mode choice, and the same type is used for the
    packet's return trip [RECOLLECTION:2026-11-08]. Different private-car pools
    for different wealth classes are a future feature requiring additional
    graphics and pakset work.

## Implementation status

Implemented in the working tree:

- time fields on vehicle, building and city-car descriptors, with readers,
  writers, pak versions and checksums;
- halt effective comfort from passenger-enabled buildings, including the
  comfort-capacity mean and the overcrowded defaults;
- the comfort settings, penalty-curve cache, old-save default off, the advanced
  settings toggle (Extended -> Passengers tab) and the working
  `simutrans/config/simuconf.tab` block;
- the blended routing cost, hard limits, the two weighting switches, the
  target-specific reboarding rule and the time-based class choice;
- no fixed boarding cost; the reboarding rule and the actual stop times are the
  split-incentive mitigation;
- the effective fare class fallback on vehicles and convoys, so a fare class
  without dedicated accommodation uses the highest fare class at or below it
  that the vehicle actually provides;
- zero old comfort points treated as absent (180-minute default) rather than as
  a zero tolerable journey;
- the stop-based give-up rule and removal of comfort from fares;
- the route-tab actual/adjusted display in dark blue, the stop info comfort and
  capacity display, and the vehicle max comfortable/tolerable times;
- catering comfort carried into the time values as the same percentage bonuses as
  the old comfort-point increases;
- private-car info windows show the car type's comfortable and tolerable journey
  times;
- private-car mode choice uses the drawn car type's comfortable and tolerable
  journey times; the type is drawn once per packet that has access to a private
  car, before the mode choice, and the same type is used for the packet's cars
  and return trip;
- the comfort cost curve is a `settings_t` method shared by the path explorer and
  the private-car mode choice;
- old-comfort-point times derived on demand from the current settings, so the
  depot and other descriptor displays no longer fall back to 3/9 hours;
- per-vehicle max comfortable and max tolerable journey time rows in the convoy
  payload detail, using the capacity-weighted mean over all passenger classes;
- the convoy and line comfort history charts are time-based and show both the
  maximum comfortable and maximum tolerable journey times; older points-based
  history and rolling averages are converted at load;
- the new English strings are in `simutrans/text/en.tab`; the other language
  tables still need one translation pass.

Remaining:

- the exact world mouseover toolstrip placement (the current display is in the
  stop info header);
- the full multi-leg route view; the direct route rows already show actual and
  adjusted times;
- the runtime wait-for-faster-convoy comparison in `fetch_goods`; the route-table
  deduplication and the class choice are comfort influenced, but that boarding check
  still compares arrival times only;
- a threading/performance review of the stop waiting-count read used for the
  comfort-capacity mean;
- a route-cost model marker so saved path-explorer tables built with different
  comfort weights are discarded and rebuilt after a simuconf change between
  sessions;
- Squirrel, network and performance tests;
- guarding a save written to a pre-15.10 target while comfort routing is on.

Future work:

- class-based private-car type selection, recorded as a TODO in
  `karte_t::generate_passengers_and_mail`; it needs separate car-type pools per
  class in the pakset before it is viable.

## Open questions

The detailed open questions are at the end of
[comfort-routing-integration](comfort-routing-integration.md). The main unresolved
choices are whether the conservative reboarding threshold should account for the
arriving leg's accumulated discomfort, and whether the stop hard limit uses the
mean wait or a sample-based statistic.
