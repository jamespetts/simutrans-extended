---
status: draft
verified: ex-15 @ bf157b7a7 plus working-tree pick-up goods package (2026-11-17, through-wagon model, collection credit and optional slots); ex-15 @ 9d2b75ecea (2026-11-20, line-level through-pair recording in `convoi_t::laden`); ex-15 @ 57435c9cb1 plus working-tree passenger loose-unload fix (2026-11-22, passenger and mail loose-stock next-transfer unload)
---
# ex-15: re-combination - cargo and passenger continuity

**Covers:** how passengers, mail and freight are handled when vehicles move between consists at a
split or join; the instance-identity decision (which physical vehicle enters which portion); and
fare/revenue consequences. Parent: [recombination-design](../recombination-design.md); passenger
and mail system: [passenger-and-goods-systems](passenger-and-goods-systems.md); journey-time
recording across boundaries: [yh-path-explorer](../yh-path-explorer.md). Untagged claims are [CODE]
at the frontmatter value. The reviews and the original proposals are retained as design constraints
in [remote-implications](remote-implications.md); those items do not establish later implementation.

## No unnecessary discharge

A recombination first determines which vehicles remain in a scheduled consist. `convoi_t::hat_gehalten`
defers its normal unloading pass when an entry may recombine, unless the entry is pick-up-only,
requires a layover or requires an explicit full discharge. It then runs
`convoi_t::process_recombination_at_stop`; delivery and route repair therefore use the final consists
rather than discarding a through packet merely because its vehicle changed convoy.

- Vehicles retained in the same scheduled consist, transferred directly between scheduled consists,
  or promoted to a Y division's target schedule keep their through cargo.
- A loaded goods wagon, or a mail van without a TPO (Travelling Post Office; defined in
  [passenger-and-goods-systems](passenger-and-goods-systems.md)), that becomes
  [loose stock](../../consist-recombination.md#terms) also keeps its through cargo. Cargo whose final destination is the stop is delivered there. An entry
  with `schedule_entry_t::discharge_payload` still discharges everything. Newly loose goods stock
  has no assigned onward carrier: `unload_cargo_for_stop` uses a destination-only pass for it, so a
  stale next transfer at the stop does not unload it. `vehicle_t::remove_stale_cargo` leaves the
  load of a vehicle in a laid-over convoy alone when the convoy has no schedule entries, because
  that convoy has no schedule to check the load against; a layover convoy that has a real schedule
  is still validated. The collecting convoy repairs the route. The load therefore survives a save
  and reload of the loose wagon.
- A passenger vehicle, or a mail vehicle on a train with a TPO, that becomes loose stock follows
  the through-coach rule ([consist-recombination](../../consist-recombination.md#terms)):
  `unload_newly_loose_cargo` unloads a packet whose next transfer or destination is the stop where
  it becomes loose in `convoi_t::commit_recombined_consist` and
  `convoi_t::commit_paired_exchange`, and keeps a packet whose next transfer is later aboard as a
  through coach. The passenger or mail packet becomes
  ordinary halt cargo at the stop, where the ordinary loading pass can rebook it.
- On collection, `convoi_t::repair_packet_route` examines the collecting convoy's remaining schedule.
  It finds the first later stop that serves the packet's existing `zwischenziel` (next transfer) or its
  `ziel` (ultimate destination), and writes that halt as the next transfer. A packet that can be
  delivered at the collection stop is left alone. The packet's `last_transfer`, used as the boarding
  halt for fare distance, is not changed.
- The scheduled post-recombination pass discharges only packets whose destination or next transfer
  is the stop, or whose continuation cannot be repaired. The latter is the ordinary halt-transfer
  fallback: the halt can reroute them from there. Compatible loose vehicles are filtered before
  collection by the same repaired-route predicate, so a service that cannot take the load cannot
  carry it past the stop.

For goods and non-TPO mail, the loaded left-over wagon keeps its load: this is the **through-wagon
model** [RECOLLECTION:2026-11-17 user decision]. The wagon is the unit of collection, and the cargo, its
`last_transfer` and the per-vehicle `through_departures` record stay with it. The collecting convoy
must serve every packet aboard (`convoi_t::vehicle_cargo_is_served`), and
`convoi_t::repair_packet_route` aligns the packet's next transfer with that convoy's schedule. The
model is a design decision, not a forum requirement: the 2018 discussion describes left-over vehicles
as dormant units available to other convoys and says they do not load while laid over, but it does not
say whether a vehicle that is already loaded is emptied
[FORUM:https://forum.simutrans.com/index.php/topic,17852.msg170134.html]. The 2013 goods example
describes an empty wagon whose goods wait at the stop, which is the flash-load case; it does not settle
the loaded wagon [FORUM:https://forum.simutrans.com/index.php/topic,11751.msg115948.html].

The through-wagon model has three consequences. First, the existing load receives no loading credit:
`convoi_t::hat_gehalten` applies the credit only to cargo loaded into the vehicle at the collection
stop, not to cargo that never left it. Second, the cargo is outside the halt: halt capacity,
overcrowding and waiting-time discard do not apply, and the wagon is the storage.
[remote-implications](remote-implications.md) A3 identifies this as penalty-free storage and favours
"unload unless a named recipient serves them" for loaded loose wagons. Third,
`schedule_entry_t::discharge_payload` is not the flash-load
alternative: it calls `vehicle_t::unload_cargo` with `discharge_all`, which delivers the packets to
the halt, books the fare for the completed leg and makes them ordinary halt cargo. The named-recipient
hybrid recorded as the follow-up would keep the load only when a named line or convoy is the expected
collector, and otherwise discharge the cargo without booking a fare and flash-load it with its
through record preserved.

The one-vehicle-at-a-time receiver-side changes apply equally to passengers, mail and goods.
Passenger division allocation and the loose-stock next-transfer rule are in
[passenger-and-goods-systems](passenger-and-goods-systems.md). Passenger portion grouping and
load-time accommodation-capacity reservation remain open design questions; this mechanism does not
supply a load-time grouping model.

## Through-leg revenue history

Each vehicle can carry a sparse `vehicle_t::through_departures` vector. An entry has the packet's
boarding halt identity, a `history_known` flag and a `departure_data_t` record containing elapsed
distance and per-player way distances. Only a loaded vehicle leaving a convoy needs these records.
`convoi_t::capture_through_departures` takes them from the existing departure data before removal,
including the distance on the current leg. If the convoy has no usable departure history for the
boarding halt, a valid zero-distance record can be created only while the vehicle is still at that
halt. Otherwise no record is added, rather than treating an incomplete history as complete.

`convoi_t::laden` advances the overall distance on each vehicle that has such a record. The ordinary
`increment_odometer` path advances its per-player distances in the same whole-kilometre increments as
the convoy's existing departure records. Records are pruned when the corresponding packets leave the
vehicle. They are serialised per vehicle at Extended 15 revision 16.

`convoi_t::calc_revenue` receives the unloading vehicle. It first tries a usable matching through
record; only when none is available does it search the delivering convoy's own schedule as before.
No through packet is booked as a completed leg merely because its vehicle detaches at an intermediate
stop or is collected, and collection itself does not change `last_transfer`. The packet is booked when
it reaches its destination or a genuine halt-transfer fallback. Per-player apportionment uses the
accumulated way distances, including travel before the recombination.

## Line-level journey-time recording

`convoi_t::laden` also reads the same per-vehicle `through_departures` record for the path explorer's
line-level journey time. At each scheduled arrival with a non-zero journey distance, while the convoy
is in `LOADING`, it adds one sample of the through pair (the boarding halt and the halt at the convoy's
position; the term is defined in [yh-path-explorer](../yh-path-explorer.md#definitions)) to the line's
`average_journey_times`, in tenths of minutes. Only a convoy on a line records, and it writes only to
the line's table; the convoy's own `average_journey_times` receives no through pair. A lineless convoy
therefore produces no line-level pair, and the path explorer times its through connexions from the
accumulated legs.

The record is not consumed and revenue is unchanged: `convoi_t::calc_revenue` reads the record's
accumulated distance and per-player way distances, `convoi_t::laden` advances that distance in the same
call, and `vehicle_t::prune_through_departures` removes the record only when no packet that boarded at
that halt remains aboard. One arrival contributes at most one sample per boarding halt. A record with
unknown history, a zero departure time, a boarding halt equal to the arrival halt, a non-positive
elapsed time, a converted time of zero, or a deleted boarding halt is skipped.

The path explorer reads the pair from the destination linkage's `average_journey_times` when it builds
a through connexion, so a recorded pair replaces the accumulated-leg fallback. The explorer's use of
the pair, and the recording's place in the design, are in [yh-path-explorer](../yh-path-explorer.md).

## Collection and flash-load credit

Physical preloading is deliberately absent: no new cargo is moved into a loose wagon while it waits.
The halt therefore continues to charge capacity and apply its ordinary overcrowding and waiting rules
to freight that will be loaded later. To collect an empty wagon, the targetless order uses optional
slots; `vehicle_description_element::load_requirement` can refuse loaded wagons, require loaded
wagons, or accept either state. The recipient setting governs only collection eligibility and does
not change loading treatment.

`collection_loose_since` records the tick at which each vehicle enters loose stock, either on a
recombination or on entry to a scheduled layover. The per-vehicle value remains correct when a wagon
is added to an existing laid-over convoy whose `arrival_time` is older. A pre-revision-17 save has no
such value; collection then falls back to the donor convoy's `arrival_time`.
The collection tick is captured separately when the vehicle moves into the collecting convoy.
Collection code handles Same, Any and named recipients alike. A vehicle's recipient is saved with the vehicle
(`vehicle_t::rdwr_from_convoi`, Extended 15 revision 3 and later): the stored line or convoy id and the flag byte
that distinguishes a convoy from a line, Same, and an explicit Any override.

For each ordinary loading call that adds freight to a collected empty wagon, the code records the
earliest `ware_t::arrival_time` among the packets actually added, without a separate scan of the
vehicle's pre-existing load. It computes:

- the load's ordinary time contribution as `calc_current_loading_time(amount) -
  calc_current_loading_time(0)`, using the existing convoy capacity interpolation and longest
  vehicle loading times;
- the available interval as `collection_time - max(wagon_loose_since, earliest_cargo_arrival_time)`,
  clamped at zero; and
- the credit as the smaller of that ordinary contribution and the available interval.

Credits are integer ticks only. They are subtracted once from `current_loading_time`, floored at zero,
then the pending collection marker is cleared so later loading calls cannot credit the same load
again. A partial load therefore receives only the credit for the amount added in that call. Several
collected wagons accumulate their respective contributions. Where a paired counterpart loads only
after its shunting interval, revision 17 saves the pending bool and the two tick values that this
later calculation needs.

`convoi_t::get_current_loading_time` and the vehicle snapshots expose the resulting loading state for
the scripted fixtures. There is no random choice and no service-specific adjustment of the credit.
The scripted cases in `tests/pending-runtime/pickup-cases/` supply the freight with
`halt_x.add_cargo` and declare its arrival time, so the expected credit is computed from a known
interval rather than from factory production. The API is in
[scripting-and-tests](../../scripting-and-tests.md); the fixture and assertions are in
[consist-recombination tests](../../scripting-and-tests/consist-recombination.md).

## Grouped loading

The goods system groups a wagon by its wagon requirement and the committed formation: a wagon is
loaded for one next transfer, or for the serving set of its committed portion, and a mixed wagon is
allowed only when the same portion serves every packet aboard
([passenger-and-goods-systems](passenger-and-goods-systems.md)). Passenger loading still fills
vehicles in consist order; portion-capacity boarding reservation and a gangway/inter-unit model
remain open. The original proposals and their capacity consequences are in the parent design and
[remote-implications](remote-implications.md). Free internal rearrangement is not claimed here.

## Open questions

- **Passenger and mail instance policy.** A division moves passenger packets and mail packets on a
  train with a TPO between vehicles to a serving portion, and a loose passenger vehicle or mail
  vehicle on a train with a TPO unloads packets whose next transfer or destination is the stop.
  Whether passenger
  loading also needs portion-outcome grouping, accommodation-capacity reservation and a
  gangway/inter-unit model remains open. A future choice must preserve the through fare history
  above.
- **Grouped freight loading.** The goods system groups a wagon by its wagon requirement and the
  committed formation, so a mixed wagon is allowed only when one portion serves every packet. The
  underfill and through-pool costs of that grouping should be measured in real saves; the fallback
  still guarantees that unservable packets are not carried onward.
- **Path feasibility of recipients.** No recipient-dependent path-explorer change is made. An authored
  through candidate might therefore name a later collector that rejects the vehicle; this needs an
  explicit feasibility decision, not an unexplained runtime transfer.
- **Loaded loose wagon storage.** The through-wagon model keeps the cargo outside the halt's capacity,
  overcrowding and waiting-time rules (see [remote-implications](remote-implications.md) A3). The
  recorded follow-up is a named-recipient hybrid: keep the load only when a named line or convoy is
  the expected collector, otherwise discharge the cargo without booking a fare and flash-load it with
  its through record preserved. Whether to implement that hybrid, and how the through record moves
  from the vehicle to the halt packet, remains open.
- **Old unrestricted loose stock.** The per-vehicle editor retains a resolved releasing-service
  identity for new stock so that Same can be restored after choosing Any. A legacy unrestricted item
  has no such saved identity. Whether compatibility code should invent one is undecided; currently it
  remains unrestricted.
- **Failure reporting.** The fallback is an ordinary halt transfer and can be visible in cargo waiting
  lists, but there is no separate bounded per-category recombination warning for it. Whether players
  need such a warning remains open.
- **`pick_up_only` with a recombination.** `convoi_t::hat_gehalten` suppresses the ordinary unloading
  pass for a `pick_up_only` entry, but the delivery pass at the end of
  `convoi_t::commit_recombined_consist` and the promoted-portion pass in
  `convoi_t::form_divided_portion` are not gated by that flag. Whether the flag should suppress those
  passes too, or whether a `pick_up_only` entry with a consist order is unsupported, needs a decision
  and a scripted case.
