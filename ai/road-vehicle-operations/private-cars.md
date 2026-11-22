---
status: draft
verified: ex-15 @ 806befea63
---
# Private cars

**Covers:** `descriptor/citycar_desc.*`, `vehicle/simroadtraffic.*` (`private_car_t`),
`simworld.cc` (`karte_t::generate_passengers_and_mail`), `simcity.*`
(`stadt_t::generate_private_cars` and `stadt_t::calc_traffic_level`), `gui/obj_info.*`,
`gui/components/gui_fixedwidth_textarea.*`, and the `citycar_level` setting in
`dataobj/settings.*`.

This is the private car part of [road-vehicle-operations](../road-vehicle-operations.md).
Both work packages in this document (occupancy and information links) are implemented on
ex-15. No maintained pakset yet supplies the new capacity and length fields, so the
pakset-data questions below remain open.

## Terms

- **Packet**: one group of `units_this_step` passengers generated together by
  `karte_t::generate_passengers_and_mail`. The packet size is drawn with
  `simrand(settings.get_passenger_routing_packet_size()) + 1`.
- **Car occupancy**: the number of passengers represented by one visible private car.
  The current model uses an average occupancy scaling; the proposed model gives each car
  a capacity.
- **Carunit**: the vehicle-length unit returned by `vehicle_desc_t::get_length()`;
  `CARUNITS_PER_TILE` carunits make one tile.

## Current generation

`karte_t::generate_passengers_and_mail` draws one packet size, then draws once whether the
packet has access to a private car (`car_ownership_draw`). It draws one car type for the
whole packet (`packet_car_type`) before the mode choice.
`stadt_t::generate_private_cars` converts the packet size into a number of cars:

```
number_of_trips = (number_of_passengers * traffic_level + round_up) / 1000
```

`traffic_level` is the `stadt_t` copy of the `citycar_level` setting, mapped from setting
values 0 to 16 onto 0 to 1000 in `stadt_t::calc_traffic_level`. At setting 11 the value is
613, documented in the code as approximately 1.6 passengers per car. `round_up` is 900 with
probability one half and 0 otherwise, making the conversion a stochastic rounding rather
than a fixed occupancy. Every car created for the packet uses the same `packet_car_type`.

The current design therefore produces groups of identical cars. A packet of 7 with
`traffic_level == 613` creates 4 or 5 cars of the same type at the same origin tile in the
same step; a packet of 1 creates 0 or 1 car. This is an occupancy scaling applied to an
arbitrarily grouped packet, not an explicit occupancy model for each car. The ownership
draw is also a property of the whole packet.

## Proposed explicit occupancy

Add two real properties to `citycar_desc_t`: a passenger capacity and a vehicle length in
the same carunit units as `vehicle_desc_t::get_length()`. Both need new pakset fields and a
new descriptor version. Existing paksets that do not provide them should keep the current
`traffic_level` formula, so that loading an existing pakset does not silently change the
traffic volume.

For a pakset that provides capacities, "literal capacity fill" needs a precise definition.
Let `N` be the packet size, `P` the era-filtered pool for the packet's wealth class, and
`C(T)` the capacity of car type `T`. The distinct rules are:

- **Homogeneous fill.** Draw one type `T` for the packet and create
  `ceil(N / C(T))` copies of `T`. This still produces identical cars and is not the
  intended change.
- **Per-car fill.** Set `remaining = N`. While `remaining > 0`, draw a type `T` from `P`,
  create one car of `T`, and set `remaining -= min(C(T), remaining)`. Each car is filled
  to capacity except possibly the last. This is the precise meaning of "spawn another
  randomly drawn car until all passengers have a car". It does not use `traffic_level`.
- **Traffic-level-constrained fill.** Compute the current number of trips from the
  `traffic_level` formula, draw that many cars with independent types, then add cars until
  total capacity covers `N`. If total capacity exceeds `N`, the spare seats remain empty.
  This keeps `traffic_level` as the primary density control.
- **Occupancy-distributed fill.** For each car, draw an actual occupancy `O` from a
  distribution over `1..C(T)`, create the car, and set `remaining -= O`. This models
  occupancy directly but needs a defensible distribution.

The expected number of cars for a uniform packet size of 1 to 7, with the current
stochastic rounding at `traffic_level == 613`, is about 2.43 cars per packet, or about 1.65
passengers per car. If every car has the same capacity `C` and per-car fill is used, the
expectations are:

| Capacity `C` | Cars per packet | Passengers per car |
|---|---|---|
| current (613) | 2.43 | 1.65 |
| 1 | 4.00 | 1.00 |
| 2 | 2.29 | 1.75 |
| 3 | 1.71 | 2.33 |
| 4 | 1.43 | 2.80 |

The table assumes independent uniform packets and a single capacity per pool. A mixed pool
changes the counts. Capacity 4 therefore reduces visible traffic by about 41% while raising
average occupancy to about 2.8; capacity 2 is close to the current density. The per-car mode
therefore makes average occupancy depend on the packet-size distribution and the capacity
distribution, which is the intended behaviour.

## Simuconf selection

Add a `settings_t` field `citycar_fill_mode`, read from the simuconf key
`citycar_fill_mode`:

- `0` = traffic-level-constrained fill. This is the default and preserves current
  behaviour: `traffic_level` controls the number of car trips.
- `1` = per-car fill. The car count is produced by the per-car loop above, and
  `traffic_level` does not control the number of cars.

The setting affects simulation, so it must be saved in the savegame and be
server-authoritative in network games, like `traffic_level`. That requires a
`settings_t::rdwr` entry behind a new `EX_SAVE_MINOR` revision and the corresponding
`simuconf.tab` comment. A dedicated `settings_stats.cc` control is optional because the
setting is intended as a simuconf option, but the saved value must still load correctly.

There is a consistency point outside car generation: `stadt_t::calc_congestion` currently
multiplies passenger car trips by `traffic_level / 1000` to approximate occupancy. In
per-car mode that adjustment no longer describes the actual cars. Congestion must then use
a measure that matches the spawned cars, such as a per-city count of cars actually created,
or a separately defined density parameter. This is an open question and should be settled
before per-car mode is enabled.

## Information window links

`private_car_t::info` writes the origin and destination as plain text.
`obj_t::show_info` creates an `obj_infowin_t`, which derives from `base_infowin_t`.
`base_infowin_t` displays its text through `gui_fixedwidth_textarea_t`, which draws the
buffer as plain text and does not parse hyperlinks. Writing `<a href="(x,y)">` in
`private_car_t::info` would therefore display the markup literally.

The reusable abstraction is a text-link span: a text buffer is parsed once into plain text
segments plus a list of `{start, end, target}` spans. A generic parser can expose that
structure to any text component. The existing `gui_flowtext_intern_t` already parses
`<a href="...">` tags and tracks clickable rectangles; the fixed-width textarea needs the
same span information without becoming a scroll pane.

`private_car_t::info` can emit `<a href="(x,y,z)">` around the origin and destination
names. Two implementation options exist:

- add the shared span parser to `gui_fixedwidth_textarea_t`, preserving the existing layout
  of every object information window, and have `obj_infowin_t` handle coordinate links by
  moving the viewport;
- add a dedicated private-car information window that uses `gui_flowtext_t`, which already
  parses hyperlinks, while leaving `obj_infowin_t` unchanged.

The first option makes links available to every information window that uses
`gui_fixedwidth_textarea_t` and keeps the embedded object view. It is recommended. Plain
text without `<a` remains byte-for-byte unchanged, so compatibility is preserved. The
parser runs only when the markup is present; the information window already rebuilds its
buffer every draw, so the extra cost is a search for `<a` plus the span list. The change is
roughly 100 to 150 lines in the component and the info-window listener. Control-character
delimiters are not recommended: they are hard to translate, hard to debug and easy to
corrupt in a saved translation file.

## Work packages

1. **Private car occupancy.** Add capacity and length to `citycar_desc_t`, add the
   `citycar_fill_mode` setting, implement both fill modes, draw the car type per car, and
   preserve the old formula for paksets without the new fields.
2. **Private car information links.** Add hyperlink handling and the origin and destination
   links.

## Serialisation, network and pakset impact

- The private car capacity and length are descriptor data, not savegame data. Changing the
  city-car descriptor requires a new descriptor version and a pakset rebuild.
- `citycar_fill_mode` is simulation-affecting settings data. It must be saved and loaded
  behind a new `EX_SAVE_MINOR` revision and be server-authoritative in network games.
- Changing the private car type draw from one per packet to one per car changes the synced
  random sequence. The draw order must be deterministic, and every peer must use the same
  capacity data. The pakset checksum covers descriptor changes.
- The occupancy calculation must use integers only.

## Open questions

- Should `traffic_level` remain as the traffic-density control in traffic-level-constrained
  mode only, and what should replace the occupancy adjustment in `calc_congestion` in
  per-car mode?
- Should private car ownership remain a packet-level draw or become a per-passenger draw?
- Are city-car capacity and length acceptable pakset additions for Pak128.Britain-Ex and
  the other maintained paksets?
- Should the information-window links extend `gui_fixedwidth_textarea_t` or use a dedicated
  `gui_flowtext_t` window?

## Implementation record

This section describes the code on branch `road-vehicle-operations` at
`7f911a42af`.

### City-car descriptor

`citycar_desc_t` gains `capacity` and `length` and the getters
`get_capacity()`, `has_capacity()` and `get_length()`. A length of zero is read
as one carunit. `descriptor/writer/citycar_writer.cc` writes node version 5 with
the two bytes after the class list; the makeobj keys are `capacity` and
`length`. `descriptor/reader/citycar_reader.cc` reads them for version 5 and
leaves both zero for older versions. Both fields enter
`citycar_desc_t::calc_checksum`.

`settings_t::citycar_fill_mode` is read from the simuconf key
`citycar_fill_mode` (0 or 1, default 0) and is saved by `settings_t::rdwr`
behind `loadsave_t::is_version_ex_atleast(15, 20)`; an older save loads 0. It
is server-authoritative because settings already travel from the server as
part of the savegame. `simutrans/config/simuconf.tab` documents both values.

### Generation

`stadt_t::generate_private_cars` takes the packet's passenger wealth class and
returns the number of cars it created. The decision between the per-car fill and
the historic formula is made from the wealth-class pool, not from the packet's
drawn car type: `private_car_t::class_has_capacity` reports whether any
era-filtered car with a non-zero draw weight in that pool defines a capacity. In
mode 0, and in mode 1 when the pool has no capacity data at all, the historic
formula is used unchanged and no extra random number is drawn. In mode 1 the
loop keeps a `remaining` passenger count, creates a car of the packet's type for
the first car, draws a type per further car with
`private_car_t::pick_citycar_desc`, and subtracts `min(capacity, remaining)`. A
descriptor with capacity zero in a mixed pool carries one passenger. The random
draws therefore depend on the packet size and the drawn capacities, which are
pakset data covered by the pakset checksum.

### Congestion

The previous `calc_congestion` multiplied the passenger trips in
`city_history_month[1][HIST_CITYCARS]` by `traffic_level / 1000` to estimate
occupancy. In mode 1 that estimate no longer describes the cars, so `stadt_t`
gains two saved counters, `private_cars_month` and
`incoming_private_cars_month`. They are incremented where the cars are
created and where an inter-city trip arrives, are saved at Extended save
revision 20, and are reset by `calc_congestion`. In mode 1 congestion uses
those car counts and does not apply the occupancy ratio; in mode 0 the
historic calculation is unchanged. `stadt_t::set_private_car_trip` takes the
created car count so that the destination town can record arrivals.

The return leg of an inter-city trip creates its cars through a call on the
town that they return to, so they are counted in that town's own
`private_cars_month` and are not added to `incoming_private_cars_month`.

### Information-window links

`parse_text_links` in `gui/components/gui_fixedwidth_textarea.cc` returns the
plain text of a buffer plus `gui_text_link_t` spans of `{start, end, target}`
byte offsets, or reports that the buffer contains no `<a` markup.
`gui_fixedwidth_textarea_t` lays out the parsed text and records one clickable
rectangle per link part per line while drawing, underlining the link. Each
rectangle is stored relative to the text area's own origin, because
`gui_container_t::infowin_event` subtracts the text area's
`gui_component_t::get_pos()` from the event position before calling the text
area's `infowin_event`. `gui_fixedwidth_textarea_t::infowin_event` reports a left
release over a rectangle to the component's listeners. A buffer with no markup
takes the original code path unchanged.

`obj_infowin_t` is an `action_listener_t` for its text area and handles
coordinate targets through `jump_to_coordinate_link` (`gui/base_info.cc`),
which parses `(x,y)` or `(x,y,z)` with `parse_coordinate_link`, moves the
viewport and the cursor, and falls back to the ground tile when the z is absent
or empty. `scenario_info_t::action_triggered` uses the same parser for its
scenario-square coordinates. `private_car_t::info` wraps the origin city and
destination building names in links to their world positions; the translated
format string is unchanged.

### Tests and gaps

The standard Squirrel suite passes (81 of 81). No scripted case exercises the
boundary table of packet sizes 1 to 7 and capacities 1 to 4: the fixture
pakset has no city-car capacity fields anywhere, so mode 1 falls back to mode 0
there, and the table remains an arithmetic expectation. A mixed pool (some cars
with capacity, some without) also has no scripted case, for the same reason;
`private_car_t::class_has_capacity` is the only code path that decides it. The
congestion inputs for both modes have no scripted case either. The link parser
is covered only indirectly: no automated test opens an information window.
