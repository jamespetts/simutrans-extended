---
status: draft
verified: ex-15 @ 684d570f9 plus working-tree max_tolerable_walk changes (2026-11-14)
---
# Comfort influenced routing: time-based model

**Covers:** the time-based comfort measurement for vehicles, halts and walking,
the cost curve between the comfortable and tolerable limits, the
split-incentive analysis, and stop comfort and comfort capacity. Parent:
[comfort-routing](comfort-routing.md). Integration
(edge weights, savegame, settings, UI, companion changes):
[comfort-routing-integration](comfort-routing-integration.md).

Status: design and partial implementation. Statements made by the user on
2026-11-08 are tagged [RECOLLECTION:2026-11-08]; derivations are [UNVERIFIED].
Implementation status: [comfort-routing](comfort-routing.md).

## Definitions

The values are per passenger class where the pakset distinguishes classes, matching
the existing per-class comfort array; a pakset that does not distinguish classes
uses one value for all classes.

For each passenger class:

- `maximum_comfortable_journey`: maximum comfortable journey time of a vehicle. The longest ride for
  which the accommodation is fully acceptable. At or below `maximum_comfortable_journey`, route cost is
  actual time.
- `maximum_tolerable_journey`: maximum tolerable journey time. Above `maximum_tolerable_journey`, passengers refuse the
  vehicle. `maximum_comfortable_journey <= maximum_tolerable_journey`.
- `maximum_comfortable_wait`: maximum comfortable waiting time of a halt.
- `maximum_tolerable_wait`: maximum tolerable waiting time of a halt. Above `maximum_tolerable_wait`, passengers refuse
  to wait and the packet is discarded or walks.
- `maximum_comfortable_walk`: maximum comfortable walking time. The setting is
  `settings_t::max_comfortable_walk`, held in tenths of minutes and read from the
  `max_comfortable_walk_minutes` key in `simuconf.tab` (default 10 minutes).
- `maximum_tolerable_walk`: maximum tolerable walking time. The setting is
  `settings_t::max_tolerable_walk`, held in tenths of minutes and read from the
  `max_tolerable_walk_minutes` key in `simuconf.tab` (default 210 minutes). The
  long default allows the long journeys on foot that were common before public
  transport. Above `maximum_tolerable_walk`, passengers will not walk: a
  walking edge whose walk exceeds it is removed, and passengers will not choose
  a whole journey on foot longer than it. The forced walks made when no route
  exists are exempt; whether they should also be refused above
  `maximum_tolerable_walk` is an open question in
  [comfort-routing-integration](comfort-routing-integration.md).
  `karte_t::get_maximum_tolerable_walk_tenths()` returns the greater of the two
  walking settings, so the comfortable time never exceeds the tolerable time.
  `karte_t::get_walking_perceived_time()` applies the curve: it returns
  the actual time when `comfort_routing` or `walking_comfort_weighting` is off,
  the actual time plus the surcharge up to `maximum_tolerable_walk`, and
  `UINT32_MAX_VALUE` above it.

All times are stored internally in tenths of minutes.

### Pakset fields

The new time fields accept either whole minutes or tenths of minutes as
alternative pakset keys. Makeobj converts the minutes form into tenths before
encoding, so the pak always stores one internal representation. The keys are:

- `maximum_comfortable_journey_minutes` /
  `maximum_comfortable_journey_tenths`
- `maximum_tolerable_journey_minutes` / `maximum_tolerable_journey_tenths`
- `maximum_comfortable_wait_minutes` / `maximum_comfortable_wait_tenths`
- `maximum_tolerable_wait_minutes` / `maximum_tolerable_wait_tenths`

If both forms of the same field are given, makeobj rejects the object. The code
variable names are the fully expressed `maximum_comfortable_journey`,
`maximum_tolerable_journey`, `maximum_comfortable_wait` and
`maximum_tolerable_wait`. No `MC_v`/`MT_v`/`MC_s`/`MT_s` abbreviations are used.

### Comfort points and backward compatibility

Existing vehicle comfort is a 0-255 byte per class. It remains in the pakset
format indefinitely and is not deprecated.

Where a vehicle gives no explicit `maximum_comfortable_journey`, derive it from
comfort points:

```
maximum_comfortable_journey = max_tolerable_journey(comfort[class]) / 6
```

`max_tolerable_journey` returns seconds; division by 6 gives tenths of minutes.
Where a vehicle gives an explicit `maximum_comfortable_journey`, the explicit
value overrides the conversion. Comfort-point and time fields may coexist; the
comfort-point field remains the fallback.

A comfort-point value of 0 is treated as absent data rather than as a comfort
rating. It falls back to the 180-minute default, in the same way as a vehicle
with no comfort points at all. Explicit time values still take precedence.

### Fare classes without dedicated accommodation

The values are per fare class, but not every vehicle provides accommodation for
every fare class. Passengers may board accommodation of the same or a lower
class, never a higher class (`convoi_t::carries_this_or_lower_class`). A
vehicle's comfort values for fare class `g` are therefore those of the highest
fare class at or below `g` for which the vehicle actually has accommodation.
`vehicle_t::get_effective_fare_class` returns that class, and `vehicle_t` and
`convoi_t` use it for both time values and for the capacity weighting.

For example, a class-4 passenger on a train with first- and third-class
accommodation uses first-class comfort in the first-class vehicles and
third-class comfort in third-class-only vehicles; the convoy value is their
capacity-weighted mean. A class-1 passenger on a bus uses the bus's class-0
accommodation. A class with no usable accommodation on a vehicle contributes
no capacity to it: a very-low-class passenger cannot use a train with no
class-0 accommodation, while a higher-class passenger can use the lower
accommodation.

A zero time value must not be read as a zero-length tolerable journey. With
the effective-class rule, zero is returned only when no accommodation of the
requested class or below exists, and the vehicle cannot carry that class at
all. The routing hard limit then correctly removes the edge.

The two existing curves are views of the same relation: `comfort >=
tolerable_comfort(T)` is equivalent to `max_tolerable_journey(comfort) >= T`.
Comparing the vehicle's `maximum_comfortable_journey` with the ride time is
therefore the existing reasonable-comfort test expressed in time, not a new
comfort scale.

The inverse table is the existing bridge from comfort points to time. Its top end
needs auditing before it drives hard limits: [known-bugs](../known-bugs.md)
records a reported calculation problem at comfort 240 or more.

The existing maximum-comfortable-journey-time display remains. It is the
player-facing expression of `maximum_comfortable_journey` and is not replaced by a
comfort-point display. It reads the explicit `maximum_comfortable_journey` when
present and the converted value otherwise.

### Defaults

- If a vehicle or private car gives neither comfort points nor explicit time
  values, `maximum_comfortable_journey` defaults to 180 minutes.
- If `maximum_tolerable_journey` is not given, it defaults to
  `maximum_comfortable_journey * 3`.
- `maximum_comfortable_wait` defaults to 180 minutes.
- If `maximum_tolerable_wait` is not given, it defaults to
  `maximum_comfortable_wait * 3`.

The default tolerable wait is therefore 9 hours.

`maximum_comfortable_walk` defaults to 10 minutes and `maximum_tolerable_walk`
to 210 minutes.

## Cost curve

For a ride of `t` tenths in a vehicle with
`(maximum_comfortable_journey, maximum_tolerable_journey)`:

- `t <= maximum_comfortable_journey`: cost is `t` (actual time).
- `maximum_comfortable_journey < t <= maximum_tolerable_journey`: let
  `x = (t - maximum_comfortable_journey) * 100 /
  (maximum_tolerable_journey - maximum_comfortable_journey)`.
  Cost is `t * F(x) / 100`, where `F` is a non-decreasing piecewise curve from
  100 at `x = 0` to `100 + P` at `x = 100`. The curve is S-shaped: close to 100
  for small `x`, steep in the middle, flattening at `100 + P`.
- `t > maximum_tolerable_journey`: the edge is removed.

A wait of `w` at a stop with
`(maximum_comfortable_wait, maximum_tolerable_wait)` uses the same function. A
walk of `w` with `(maximum_comfortable_walk, maximum_tolerable_walk)` also uses
it. The smooth factor applies only between the comfortable and tolerable times;
comfort above the comfortable time gives no discount. Luxury therefore does not
make a route faster, only more attractive at the class-choice stage.

All arithmetic is integer: times in tenths, factors in percent, 64-bit
intermediates, round to nearest. No floating point in synced code.

### Why the penalty is length-dependent

The principle is that a short ride in a bare vehicle is acceptable, while the same
conditions on a long journey are not. A lift is the extreme short case: a bare box,
people stand, and a 12-second ride is not perceived as uncomfortable. An ocean
liner is the extreme long case: days or weeks in lift conditions would be refused.
The curve between the maximum comfortable and maximum tolerable times gives a
small increase near the comfortable limit and a large increase near the tolerable
limit.

This is different from a flat per-minute discomfort rate based only on the
vehicle's comfort. A flat rate would penalise a short ride in a basic vehicle even
when the ride is entirely acceptable for its duration. The length-dependent curve
matches the lift/ocean-liner principle directly.

### Comfort above the comfortable time

A vehicle whose `maximum_comfortable_journey` greatly exceeds the journey duration
is luxurious for that journey. Its route cost is still the actual time. The
existing `comfort_preference_percentage` per passenger and the class-choice logic
decide whether the passenger waits for that luxury.

Class choice is time-based:

1. `journey_time_seconds` is the expected arrival time minus the current time.
2. `ideal_comfort_time = journey_time_seconds *
   comfort_preference_percentage / 100`, where
   `comfort_preference_percentage` is the passenger's preference for comfort.
3. For each class from the passenger's class down to 0, if the convoy's
   `maximum_comfortable_journey` for that class is at least
   `ideal_comfort_time`, that class becomes the best class. For a class with
   no dedicated accommodation, this is the effective-class value defined above,
   so the passenger considers the accommodation they can actually use.
4. If the best class differs from the passenger's class, the passenger waits for
   a better class under the existing wait rules.

The algorithm operates on time values only. A pakset that provides comfort points
has them converted to `maximum_comfortable_journey`; the class-choice algorithm
does not use points.

## Split-incentive analysis

Comfort is assessed per leg, with a reboarding rule that prevents changing
transport only to get a fresh comfortable-time allowance. This section records the
analysis behind that rule.

The lift/ocean-liner principle described above refers to the total time spent in
the conditions. The per-leg curve applies it to each continuous ride; that is an
approximation, and the split incentive is the difference between the approximation
and the principle.

The cost curve uses the leg duration `t` against the vehicle's comfortable time
`maximum_comfortable_journey`. That is the natural per-leg reading of that
principle, and it is decomposable: every edge has one non-negative cost. It also
creates the split incentive.

For a journey of total duration `T` in vehicles of the same comfort:

- If `T <= maximum_comfortable_journey`, the unsplit cost is `T`. Splitting adds
  waiting and transfer time at the change, so there is no benefit.
- If `T > maximum_comfortable_journey`, the unsplit cost is `T * F(T) > T`. If
  the journey is split into legs each at or below
  `maximum_comfortable_journey`, every leg costs its actual time, so the total is
  `T` plus the waiting and transfer time at the change. The saving is
  `T * (F(T) - 1)` minus that additional time.

The saving grows with `T` and with the penalty. With
`maximum_comfortable_journey = 60` minutes,
`maximum_tolerable_journey = 180` minutes and a factor of about 1.5 at 120
minutes, the saving is about 30 minutes; ordinary waiting and transfer times at a
change do not reliably offset it. Splitting a 180-minute ride into three 60-minute
legs can remove the penalty almost entirely. A fixed boarding cost would have to
be large to offset it, and would distort short journeys, so the implemented rule
below is used instead.

A literal whole-journey threshold, where the penalty is assessed on the total time
spent in discomfort rather than on each leg, would not have this incentive. It is
not compatible with the current design:

- The route table stores one route per halt pair. A whole-journey penalty can make
  a slower, more comfortable route better for a passenger even though the stored
  route is best under per-leg costs. The stored route can then fail the tolerance
  test while a different route would pass, breaking single-route sufficiency.
- Representing the accumulated time in the routing-search state would require a
  time band in the state key. That multiplies the expanded state space and the
  matrix work; it is a much larger change than the current plan.
- Storing the whole-journey actual time in the matrix and applying the penalty
  after the search still leaves the route-choice/tolerance mismatch; it also costs
  the extra 2 bytes per cell that the implemented storage avoids.

The approaches considered were:

- **Rejected: accept the split incentive and add a fixed boarding cost to offset
  it.** The required cost is large and varies with journey length, and a fixed
  cost does not correspond to a real cost. Charged on every boarding, it also
  makes short journeys appear disproportionately worse.
- **Implemented: forbid a change that does not improve comfort or save actual
  time.** When the arriving transport continues to the same target, an outgoing
  edge on a different transport with equal or lower comfort is allowed only if its
  full perceived cost (waiting, transfer and comfort-adjusted ride) is less than
  the arriving transport's actual remaining ride time to that target. If the
  arriving transport does not serve that target, the change is allowed because it
  is necessary. A change to a higher-comfort transport remains allowed. Walking
  is exempt. This is compatible with the existing expanded state
  `(halt, arriving transport)` because the arriving transport is already part of
  the state and its comfort can be looked up. It changes the search's edge
  filter, not the graph size.
- **Rejected: apply a whole-journey penalty after the search.** Rejected for the
  reasons above.

The implemented rule is: no fixed boarding cost is charged. The rule removes the
artificial incentive to change to an identical or lower-comfort vehicle solely to
reset the comfortable-time threshold. A change to a genuinely better vehicle
remains allowed and is realistic; the first leg's discomfort is still counted, and
only the better vehicle gets its own comfortable time. A change to a genuinely
faster vehicle is also allowed when its full perceived cost beats the arriving
transport's remaining actual ride time; this keeps the case where a slow service
continues to the same destination but a much faster service is available at an
interchange, even when the fast service is equally or less comfortable.

The rule is implemented in `path_explorer_t::compartment_t::reboarding_blocked`:
the search keeps the arriving transport in the state, and the list of outgoing
connections from each halt is sorted by transport and target so the arriving
transport's connection to the candidate destination can be found without scanning
the whole list. Walking remains
exempt, and through edges use the source transport's comfort and journey time.

## Stop comfort and capacity

Each stop or extension building may define:

- `maximum_comfortable_wait_minutes` or `maximum_comfortable_wait_tenths`;
- `maximum_tolerable_wait_minutes` or `maximum_tolerable_wait_tenths`;
- `comfort_capacity`: the number of waiting passengers to whom the building's
  comfort applies. Zero or absent means unlimited, which is the default.

The base rule is that a halt's comfort is the comfort of its highest-comfort
passenger-enabled building; the base stop building and extensions all participate,
and the highest wins. The capacity refinement is a passenger-weighted mean:

1. For the relevant passenger class, take the number of waiting passengers `N`.
2. Sort the passenger-enabled buildings by comfort, highest first.
3. Allocate passengers to the best building up to its comfort capacity, then to
   the next, and so on.
4. The unallocated remainder gets the base stop's comfort, or a separate standing
   value if one is later added.
5. The halt's effective `maximum_comfortable_wait` and `maximum_tolerable_wait` are the passenger-weighted means of the
   allocated comfort levels.

With unlimited capacity, or `N` within the best building's capacity, this reduces
to the highest-comfort rule. With `N` above the comfortable capacity, it falls
towards the base or standing comfort, exactly as the vehicle mean falls when
passengers stand. The mean depends on the current waiting count, so it is computed
on each path refresh.

The `maximum_tolerable_wait` hard limit removes edges from the stop when the
expected wait exceeds the stop's tolerable waiting time. The default
`maximum_comfortable_wait` is 180 minutes and the default
`maximum_tolerable_wait` is 540 minutes (three times the comfortable wait). Zero
or absent means the default, not unlimited.

Transfer time is not part of the comfort curve [RECOLLECTION:2026-11-08]. It
remains actual time, and overcrowding raises it through the halt transfer-time
calculation (`haltestelle_t::recalc_transfer_time`); the wait comfort values
apply to waiting only.

The capacity concept is practical: the halt already knows its waiting counts per
class and its own tiles, so the allocation is a small loop over buildings. It does
not need a new per-passenger field. The data cost is one uint16 per building.

### Overcrowding and standing

The measured mean comfort, including standing, is used for vehicles. The
time-based equivalent lowers `maximum_comfortable_journey` when passengers stand. It does not change `maximum_tolerable_journey`; the reduced
comfortable time makes the comfort-adjusted journey longer and can therefore take
the journey above the passenger's time budget.

The value is controlled by the simuconf setting
`max_comfortable_journey_overcrowded`, in minutes. It is the comfortable journey
time assigned to a standing passenger. When a vehicle or class carries more
passengers than its seated capacity, its comfortable time becomes the
capacity-weighted mean of the seated passengers' normal comfortable time and
this value. Only the comfortable time is lowered; `maximum_tolerable_journey` is
unchanged, so standing makes the comfort penalty start earlier and reach the
tolerable limit sooner. The default is 5 minutes; paksets can raise or lower it
globally.

For stops, the capacity-weighted mean provides the same effect: waiting passengers
beyond the comfortable capacity receive `max_comfortable_wait_overcrowded`, whose
default is 15 minutes and which can be set in simuconf.tab. As for vehicles, this
lowers only the comfortable time; `maximum_tolerable_wait` is unchanged.

### Catering

Catering uses the existing bonus scale, expressed as a percentage of the time
values: levels 1 to 5 raise both the comfortable and the tolerable time by 5, 10,
16, 20 and 25 percent. The percentage form preserves the ordering and relative
size of the bonus without reintroducing points into the routing model. A vehicle with self-contained
catering uses the higher of its own catering level and the convoy's level.

The comfort-capacity concept is part of the model: it is the stop-side
implementation of the same overcrowding principle.

## Open questions

- The reboarding comparison is deliberately conservative: it uses the arriving
  transport's actual rather than comfort-adjusted remaining ride, so a change may
  be blocked when the arriving leg's accumulated discomfort would have made it
  worthwhile. Whether to use a time band in the state or a less conservative
  threshold is open.
- Whether the stop hard limit uses the mean wait or a sample-based statistic.
