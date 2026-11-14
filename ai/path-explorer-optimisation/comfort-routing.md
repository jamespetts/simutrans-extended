---
status: draft
verified: ex-15 @ 684d570f9 plus working-tree max_tolerable_walk changes (2026-11-14)
---
# Comfort influenced routing (design)

**Covers:** the design for comfort influenced passenger routing: time-based
comfort measurement for vehicles, halts and walking, the route-cost curve, stop
comfort and capacity, blended route storage, UI, the fare and give-up changes, and
private-car comfort. Parent: [path-explorer-optimisation](../path-explorer-optimisation.md).
Roadmap: [project-roadmap](../project-roadmap.md) ("Comfort influenced routing",
"Waiting time limits and stop facilities", "Different private cars for different
levels of passenger wealth").

Status: design and partial implementation. Statements made by the user on
2026-11-08 are tagged [RECOLLECTION:2026-11-08]; untagged code facts are [CODE] at
the frontmatter commit; derivations are [UNVERIFIED]. The implementation status
is recorded below.

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

## Design choices

The design uses:

- **Storage.** The matrix stores one 4-byte cost per cell plus the 2-byte next
  stop. There is no separate per-cell actual-time or discomfort array.
- **Comfort is measured as time, not as points.** A vehicle has a maximum
  comfortable journey time and a maximum tolerable journey time. A halt has a
  maximum comfortable waiting time and a maximum tolerable waiting time. A walk
  has a global maximum comfortable time and a global maximum tolerable time,
  both settings. At or below the comfortable time, actual time
  is used. Between the comfortable and tolerable times, a scaled increase factor
  applies; it is small for short durations or a small shortfall, and grows,
  possibly in an S-curve, as either increases. Above the tolerable time, the leg
  is refused. Comfort above the comfortable time is luxury and affects class
  choice only.
- **Comfort is assessed per leg, with a reboarding rule.** The route table stores
  one route per stop pair, so a threshold on the whole journey would require more
  information per route than the table stores. To prevent splitting a long ride
  merely to reset the comfortable-time threshold, a change to a different transport
  with equal or lower comfort is allowed when the arriving transport does not serve
  the same destination, or when the change's full cost (waiting, transfer and
  comfort-adjusted ride) is less than the arriving transport's actual remaining
  ride to that destination. A change to a higher-comfort transport remains allowed.
  Walking is exempt. No fixed boarding cost is charged; the actual stop times and
  the reboarding rule are the mitigation for the incentive to split a journey. The
  analysis is in [comfort-routing-model](comfort-routing-model.md).
- **Backward compatibility.** Comfort points are preserved indefinitely, not
  deprecated. A vehicle's comfortable time defaults to
  `max_tolerable_journey(comfort[class])`, converted from seconds to tenths of
  minutes. Explicit new time fields override the conversion. The existing
  maximum-comfortable-journey-time display remains.
- **Stops.** A halt's passenger comfort is the comfort of its highest-comfort
  passenger-enabled building. Comfort capacity is included: each building may
  have a `comfort_capacity`, and the halt's effective comfort is the
  passenger-weighted mean of the best comfort levels actually available to the
  waiting passengers. Zero or absent capacity means unlimited. The remainder gets
  the base or overcrowded comfort. This is the same principle as vehicle mean
  comfort. The default comfortable wait is 3 hours and the default tolerable
  wait is 9 hours (three times the comfortable wait).
- **Overcrowding.** The mean comfort for the relevant class is used. Standing
  values are used for vehicles and for stops, or overcrowding has no realistic
  effect. The measured mean applies on the next path refresh.
- **Boarding.** Convoys are compared by comfort-adjusted cost, not only arrival
  time; the passenger stays aboard when the current convoy continues to the next
  stop.
- **Settings and migration.** The global comfort-point multiplier table is
  replaced by the per-object comfortable and tolerable times plus a penalty curve
  whose points are set in simuconf.tab. Vehicle, waiting and walking comfort have
  separate weighting switches, placed together with one comment block. All new
  settings are added to the shipped simuconf.tab with comments and defaults. Saves
  without the setting load with comfort routing off; the advanced settings dialogue
  can turn it on, with no warning. New games start with it on.
- **UI.** Comfort-adjusted times are shown in dark blue and are hidden entirely
  when comfort routing is disabled. There is no prominent "comfort routing
  enabled" indicator; the presence of the adjusted times is the indication. The
  stop information shows non-zero comfort capacities with the comfort they
  provide; moving that display to the exact world mouseover toolstrip position
  remains. The line/consist comfort history chart is replaced by a pair of
  time-based charts for mean maximum comfortable and maximum tolerable journey
  times; older points-based history is converted at load.
- **Related behaviour changes.** Comfort is removed from fares directly.
  Passengers are discarded on the maximum tolerable waiting time. Private cars use
  the two time values, not comfort points. The car type is drawn once per packet that has
  access to a private car, before the mode choice, and the same type is used for
  the packet's return trip [RECOLLECTION:2026-11-08]. Separate private-car pools
  by wealth class are future work requiring more graphics and pakset data.

## Implementation status

Implemented:

- time fields on vehicle, building and city-car descriptors, with readers,
  writers, pak versions and checksums;
- halt effective comfort from passenger-enabled buildings, including the
  comfort-capacity mean and the overcrowded defaults;
- the comfort settings, penalty-curve cache, the default-off behaviour for saves
  without the setting, the advanced settings toggle (Extended -> Passengers tab)
  and the working `simutrans/config/simuconf.tab` block;
- the blended routing cost, hard limits, the vehicle, waiting and walking
  weighting switches, the target-specific reboarding rule and the time-based
  class choice;
- walking comfort, including the walking edges, the start and destination halt
  choices and the all-walking mode comparison;
- no fixed boarding cost; the reboarding rule and the actual stop times are the
  split-incentive mitigation;
- the effective fare class fallback on vehicles and convoys, so a fare class
  without dedicated accommodation uses the highest fare class at or below it
  that the vehicle actually provides;
- zero comfort points treated as absent (180-minute default) rather than as a zero
  tolerable journey;
- the stop-based give-up rule and removal of comfort from fares;
- the route-tab actual/adjusted display in dark blue, the stop info comfort and
  capacity display, and the vehicle max comfortable/tolerable times;
- a full route window opened from the route tab's by-category listing follows the
  stored next-stop chain and presents it as a table with a summary header and a
  totals row: each leg's waiting, travel and transfer times, actual total,
  comfort-adjusted total, vehicle comfort and stop comfort; the route data is
  copied when the window opens, and the direct service is shown if the chain is
  unavailable;
- the runtime boarding comparison in `fetch_goods` adds the ride comfort
  surcharge of each candidate convoy to its arrival time (and does not wait for
  a convoy whose ride would exceed its tolerable journey), so a later but more
  comfortable convoy can be worth waiting for and a faster but less comfortable
  one may not be; the existing waiting-tolerance check compares the extra wait
  with the resulting perceived time saved;
- catering comfort carried into the time values as the same percentage bonuses as
  the comfort-point increases;
- private-car info windows show the car type's comfortable and tolerable journey
  times;
- private-car mode choice uses the drawn car type's comfortable and tolerable
  journey times; the type is drawn once per packet that has access to a private
  car, before the mode choice, and the same type is used for the packet's cars
  and return trip;
- the comfort cost curve is a `settings_t` method shared by the path explorer and
  the private-car mode choice;
- comfort-point times derived on demand from the current settings, so the depot
  and other descriptor displays use the derived times rather than a fixed 3/9
  hours;
- per-vehicle max comfortable and max tolerable journey time rows in the convoy
  payload detail, using the capacity-weighted mean over all passenger classes;
- the convoy and line comfort history charts are time-based and show both the
  maximum comfortable and maximum tolerable journey times; older points-based
  history and rolling averages are converted at load;
- the path explorer and the stop UI ask for a stop's comfortable and tolerable
  waiting times together, in one `get_wait_comfort` call, instead of recomputing
  the same stop data twice; the through-journey candidate build makes the call
  once per origin stop, not once per destination;
- the routing search caches the information needed by the rule that prevents
  changing transport only to get a fresh comfortable-time allowance for the next
  leg: for each search state it stores the connections served by the transport the
  passenger arrived on, and a small hash table mapping each destination to the
  actual ride time. This replaces a search and scan of the stop's connection list
  for every candidate change;
- the new English strings are in `simutrans/text/en.tab`; the other language
  tables still need one translation pass.

## Threading review of the waiting-count read

`haltestelle_t::get_wait_comfort` reads the stop's tiles and, through
`get_ware_summe(passengers, g_class)`, the list of passengers waiting at the stop
(the `cargo[passengers]` vector). It is called from the path-explorer worker
thread (`compartment_t::rebuild_connexions` and the through-journey candidate
build), from the main-thread waiting-limit check in `haltestelle_t::step`, and
from the stop UI. The read needs no mutex:

- Every main-thread change to the waiting lists (`add_ware_to_halt`,
  `reroute_goods`, `fetch_goods`, `recall_ware`) runs inside `karte_t::step`.
  `karte_t::step` awaits the path explorer at the start of the step, before
  `new_month` and before the convoy, factory and halt steps; `start_path_explorer`
  runs at the end of the step, after those changes.
- `karte_t::sync_step` does not load or unload passengers or goods; the loading
  path (`convoi_t::laden` -> `haltestelle_t::request_loading` -> `hat_gehalten`)
  is reached from `convoi_t::step`.
- `karte_t::pause_step` starts the explorer at its end and does not run the halt
  and factory steps that change the waiting lists.
- Halt tile changes (`add_grund`, `rem_grund` and the other tile-list
  mutators) await the path explorer before changing the tile list.

The value is therefore a consistent snapshot of the last completed step. It is
advisory and is used only to choose the stop's effective comfort for the next
refresh. Taking `step_passengers_and_mail_mutex` would add no protection beyond
the step-boundary awaits, and would constrain any caller that already holds it.

The only concurrent write in the explorer window is
`haltestelle_t::prepare_goods_list`, called from the explorer when a category's
waiting list is null; it publishes a new empty list, and a UI read can race with
that one-time pointer publication. The write is harmless in practice on the
supported platforms because the pointer store is aligned and the published list is
empty, but it is a data race in the C++ memory-model sense and predates comfort
routing. It is not introduced by the waiting-count read and remains an open
question rather than a change in this feature.

## Performance measurements

The added cost comes from the rule that prevents a passenger from changing to a
different transport merely to get a fresh comfortable-time allowance for the next
leg. The search, which runs one shortest-path search per origin, must
compare each possible change with the time the passenger would have spent staying
on the current transport to the same destination. A search and scan of the stop's
stop-to-stop connection list for every candidate change would require 1.3 billion
lookups on the `pe-without-paths.sve` rebuild. The search graph stores, for each
state, the contiguous range of stop-to-stop connections served by
the arriving transport and a small hash table (linear probing) mapping each
destination it serves to the actual ride time. The lookup was checked against the
linear-scan formulation on all 1.3 billion calls with no difference
[EXECUTION-VERIFIED:2026-11-09].

Measurements [EXECUTION-VERIFIED:2026-11-09, Profile (server) x64, ex-15 @
c1e914343 plus the working-tree comfort changes, four threads, using a temporary
TEST harness that is not part of the shipped code]:

- `bb-10-sep-2023.sve` (the gargantuan Bridgewater-Brunel save, 5,375 stops in
  the route search for the tested passenger class): a complete rebuild run
  immediately after load with no per-step time limit, two runs per setting.
  Comfort routing off: 59.2 s and 55.0 s (median 57.1 s); on: 69.8 s and 67.3 s
  (median 68.6 s). Added rebuild cost 11.4 s (20%). Wall time for the whole
  process (load plus rebuild) 69.3-73.6 s off and 81.8-84.3 s on.
- `pe-without-paths.sve` (the month-boundary save, 1,828 stops in the route
  search, a complete rebuild during load): whole-process wall time 33.19 s and
  33.34 s off, 40.33 s and 40.05 s on. The rebuild is the difference (about 15 s off,
  22 s on); the relative cost is larger here because the rebuild is smaller.

The network loopback smoke passed with 1,049 identical per-step route hashes on
the final search code [EXECUTION-VERIFIED:2026-11-09].

The remaining cost is one lookup for each connection the search examines. The
rule implements the chosen route-choice policy, so reducing the cost further
would mean changing the rule (for example, tracking the accumulated ride time)
rather than the data structure. The measurements are complete rebuilds with no
per-step time limit, not a network server running under its normal time budget.

Remaining:

- the exact world mouseover toolstrip placement (the current display is in the
  stop info header);
- a marker saved with the route tables recording the comfort weights used to
  build them, so tables built with different weights are discarded and rebuilt
  after a `simuconf.tab` change between sessions;
- Squirrel scripting tests and pakset calibration (checking the shipped time
  values against realistic journeys);
- preventing a save to the older 15.10 format while comfort routing is on.

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

- Is the measured ~20% cost of a complete rebuild acceptable, or should the rule
  track accumulated discomfort in the search state to reduce the number of
  destination lookups?
- The path explorer can publish an empty waiting list for a goods category while
  the UI reads the same list pointer. Is it worth making that publication atomic,
  or is the pointer store that is harmless in practice accepted?
