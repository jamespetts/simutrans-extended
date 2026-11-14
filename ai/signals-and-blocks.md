---
status: draft
verified: ex-15 @ 684d570f9 plus working-tree recombination runtime (2026-11-13); gameplay model and reservation audit 2026-11-14; call-on, reservation-index and signalling-refactor changes (2026-11-14)
---
# Signals, signalboxes and block working

**Covers:** obj/signal.* and obj/roadsign.* (signal objects, aspects), descriptor/roadsign_desc.*,
simsignalbox.*, and the signal windows in gui/ (→ [gui](gui.md)). Consist recombination is in
[consist-recombination](consist-recombination.md). Reservation state, the block reserver, releasing
and the route-unreserve worker pool are in
[block-reservations](signals-and-blocks/block-reservations.md). Signal objects are
inventoried in [objects](objects.md); convoy movement in [vehicles-and-convoys](vehicles-and-convoys.md);
routing in [routing-and-scheduling](routing-and-scheduling.md).

Sub-document: [block-reservations](signals-and-blocks/block-reservations.md) — the per-tile
reservation record, the block reserver, releasing, the threaded unreserver, threading and saved
reservation state; read when touching reservation code, block-working implementation or reservation
determinism.

## Terms

- **Way, reservation, block, convoy.** A **way** is the track object on a tile (`weg_t`); every rail-family way and every runway is a
  `schiene_t`, where reservation state lives. A **reservation** is the per-tile record naming the convoy permitted to use the tile
  (`schiene_t::reserved`, a `convoihandle_t`); there is no per-vehicle or per-portion record. A **convoy** is the set of vehicles that
  moves as one unit (`convoi_t`). A **block** is the section a train must have reserved in full before entering, bounded by signals;
  blocks are not objects but the run of consecutive way tiles one convoy has reserved between signals. **Consist recombination**
  changes the vehicles in one or more consists at a stop (see
  [consist-recombination](consist-recombination.md)); **coupling** joins vehicles to a convoy and **division** splits a convoy into
  two portions.
- **Working method, aspect, call-on.** A **working method** is the signalling rule in force for a train (`working_method_t`, simtypes.h);
  a signal's descriptor carries the method it imposes on a passing train. An **aspect** is the displayed signal state
  (`roadsign_t::signal_aspects`: `danger`, `clear`, `caution`, `preliminary_caution`, `advance_caution`, their `_no_choose` variants,
  `call_on`). In play `danger` means stop, `clear` means proceed, the caution aspects warn of the next signal's aspect, and
  **call-on** is the `roadsign_t::call_on` aspect. It covers two distinct authorities: ordinary permissive working over plain track
  to the next signal, and a choose signal's target-specific admission of a coupling approach to an occupied platform. The
  choose-signal form does not require `get_permissive()` or `no_junctions_to_next_signal()` and always switches the train to
  `drive_by_sight`; `get_has_call_on()` only selects the image.
- **Signalbox, signal group, stale reservation.** A **signalbox** is a player-built building object (`signalbox_t`) that owns a
  list of signal positions and constrains where signals may be built. Its **signal group** support is the pakset cluster mask
  (`building_desc_t::get_clusters()`) that must intersect a signal type's own group (`roadsign_desc_t::get_signal_group()`);
  a signal type with group 0 needs no box. A box also has a **coverage radius** (`building_desc_t::get_radius()`, 0 meaning
  unlimited) and a **capacity** (`building_desc_t::get_capacity()`, the maximum number of signals it controls). A **stale
  reservation** is a `stale_block` reservation kept behind a train under token block or one train staff working methods until
  the train reaches the exit signal.

## Gameplay model

The player signals a railway by placing signals on track. For signal types that declare a signal
group, the player must first select or build a signalbox: the box controls the signals within its
coverage radius, up to its capacity, and the signal build menu offers only the types it supports. A
signal divides the track into blocks and displays an aspect that tells a train whether it may enter
the block beyond it. The signal type also selects the **working method**, the signalling regime that
decides how far ahead a train reserves. As trains run they reserve the track ahead; the reservations
and the signal aspects together decide admission. The player never authors a block or a reservation
directly. The code sections below describe the implementation.

- **Signals and working methods.** The pakset authors signal types with a working method and optional
  roles: distant (pre-) signal, combined stop-and-distant signal, choose signal, longblock or station
  signal. The player chooses the working method by choosing a signal type. A choose signal selects an
  alternative route beyond itself; when the current schedule entry has a coupling target, it selects
  the target's platform track and can issue target-specific call-on. The signal build tool can
  place one signal or a run of signals at a chosen spacing, with options to remove intermediate
  signals, replace other signals, or place the run backwards.
- **Signalboxes.** A signal type whose descriptor declares a **signal group** can only be built while
  one of the player's signalboxes is selected. The box type must support the group, the signal must
  lie within the box's coverage radius and within the signal type's own maximum distance from the
  box, and the box must have a free signal slot. The build menu is filtered accordingly. Group-0
  signal types need no box. The Signalboxes section states the full player workflow and constraints.
- **Reservations.** A reservation is the invisible per-tile claim a train holds on the track ahead.
  The clear-reservation tool draws reserved tiles with a colour per reservation type and can clear a
  stuck claim; otherwise reservations are invisible in play. Implementation:
  [block-reservations](signals-and-blocks/block-reservations.md).
- **Consist recombination.** The player authors consist orders and couple/uncouple targets on
  schedule entries; the runtime changes the consists at a stop. This covers locomotive changes,
  through coaches, multiple-unit splits and joins, and loose wagons dropped or collected by a
  pick-up goods train, not only the two-portion and two-convoy patterns. See
  [consist-recombination](consist-recombination.md); the reservation and platform consequences are
  covered below.

## Signal objects and descriptors

In play these are the objects the player builds and the pakset types that define them. A signal type
selects the working method and the optional roles listed below; the object on the map holds the
current aspect and its box.

`signal_t` derives from `roadsign_t`, which derives from `obj_t` and `sync_steppable`. A signal is an object on the way tile;
`weg_t::get_signal(direction_of_travel)` locates it and applies the way's direction mask (`ribi_maske`, the direction bits that signals
and signs mask out of the way's connection mask); a one train staff cabinet (the trackside cabinet that holds the single staff) is
returned for both travel directions. `weg_t::has_signal()`
and `weg_t::count_sign()` record that a sign is present. `roadsign_desc_t` holds the behaviour the object does not: the working method,
aspect count, maximum speed (`get_max_speed()`), pre-signal, combined-signal, choose-signal, longblock and station-signal flags, `get_permissive()`, `get_has_call_on()`,
`get_double_block()`, `get_normal_danger()`, `get_signal_group()` and `get_max_distance_to_signalbox()`. The aspect is saved by
`roadsign_t::rdwr` (the `state` and `dir` bytes); `signal_t::rdwr_signal` additionally saves the signalbox position,
`no_junctions_to_next_signal` and `train_last_passed`, and defines the conditions under which each field it saves is written.

The descriptor's optional roles map to player-visible behaviour. A **pre-signal** (distant signal)
warns of the next signal's aspect and does not display danger. A **combined** signal is a pre-signal
with three aspects under absolute block, acting as both the stop signal and the distant signal on one
post. A **choose** signal can select an alternative route beyond itself. A
**longblock** signal (`is_longblock_signal()`) affects how `rail_vehicle_t::block_reserver` treats
the block; with time interval, time interval with telegraph or absolute block working it is also a
**station signal**, registered at the stop and used by the platform-starter logic in
`rail_vehicle_t::block_reserver`. Other descriptor flags decide whether the signal is permissive,
has a call-on aspect, rests at danger, and clears only when the next signal on the route is clear
(`get_permissive()`, `get_has_call_on()`, `get_normal_danger()`, `get_double_block()`). The descriptor
also carries the working method and the `get_max_distance_to_signalbox()` limit described under
Signalboxes.

## Working methods

Each value below is a player-selectable signalling regime, chosen by selecting a signal type whose
descriptor carries that method. The text below states how the method governs reservation and signal
clearing; the player sees the result as trains stopping at signals, following at a distance, or
entering an occupied section only under call-on.

The `working_method_t` values (simtypes.h) and the reservation behaviour under each are as follows. `drive_by_sight`: no signal governs
entry, so the train reserves only to the sighting or braking distance, whichever binds first (the sighting-distance check in
`block_reserver`). `absolute_block` and `token_block`: entry requires the whole block to the next signal to be reserved, and token block
keeps the tiles behind the train as `stale_block` until the exit signal is passed (`rail_vehicle_t::clear_token_reservation`).
`track_circuit_block` and `cab_signalling` behave as absolute block for entry and restore signals behind the train to permissive aspects
in `rail_vehicle_t::leave_tile`. `moving_block`: reserving stops at the braking distance rather than at a signal. `time_interval` and
`time_interval_with_telegraph`: entry needs no block reservation; the signal clears a fixed time after the train last passed
(`karte_t::step_time_interval_signals`, using `signal_t::train_last_passed` with `time_interval_seconds_to_caution` and
`time_interval_seconds_to_clear`), and only when `signal_t::no_junctions_to_next_signal` is true, while telegraph working removes the
sighting-distance limit on reserving. `one_train_staff`: one cabinet authorises the single-line section, and the staff's presence is
inferred from a reservation held by a convoy whose front vehicle is in one train staff mode
(`roadsign_t::check_one_tran_staff_reservation`).

### Speed limits by working method

Every signal type carries a maximum speed (`roadsign_desc_t::get_max_speed()`), applied with
`convoi_t::set_maximum_signal_speed()`; `convoi_t::calc_acceleration()` caps the target speed
`akt_speed_soll` at `convoi_t::max_signal_speed`. The working method changes how the cap is derived:
drive-by-sight working applies the `max_speed_drive_by_sight` setting, or the tram variant
`max_speed_drive_by_sight_tram`, when that setting is non-zero; time-interval caution and junction
working, including a time-interval station signal, apply half the lower of the line speed and the
signal's maximum; a clear time-interval signal and the ordinary block methods apply the signal's
maximum. A signal type may therefore be authored with a lower maximum speed to impose a permanent
restriction at that signal.

## Signalboxes

In play the signalbox is the infrastructure that makes grouped signals buildable. The player selects
an owned box, or builds one, which selects it; the signal build
menu then offers only the signal types that box can control. A signal must lie within the box's
coverage radius (0 means unlimited, shown as "infinite range") and within the signal type's own
maximum distance from the box (0 means no limit; moving block uses that field for a different rule),
and the box must have a free signal slot unless the signal replaces one already on that tile. The box
type's cluster mask must include the signal's group. These rules make the box a real planning
constraint: its radius and capacity determine how many compatible signals it can concentrate in an
area, so a player who wants dense signalling must place several boxes or a box type with a larger
radius or capacity, and a box of the wrong type cannot host the intended signal group. Deleting a box
deletes the signals it controls; transferring them to another box first keeps them.

The code rules are as follows.

`signalbox_t` derives from `gebaeude_t`, stores `signals` (a list of `koord3d`, saved in `signalbox_t::rdwr`) and maintains
`signalbox_t::all_signalboxes`. `can_add_signal(const roadsign_desc_t*)` requires the signal descriptor's `get_signal_group()` mask to
intersect the building descriptor's cluster mask (a signal type with group 0 never uses a box); `can_add_signal(const signal_t*)` also requires the same owner
and, for a building with a non-zero radius, that the signal lies within that radius (metres, compared with the tile distance times
`meters_per_tile`); `can_add_more_signals()` compares `signals.get_count()` with `get_capacity()`. `add_signal`, `remove_signal`,
`transfer_signal` and `transfer_all_signals` maintain both the box's list and `signal_t::signalbox`. A `signal_t` destructor removes
itself from its box; the `signalbox_t` destructor deletes every signal it controls, invalidates each signal's signalbox coordinate and
calls `weg_t::count_sign()` on the tile.

A signalbox does not compute aspects. Aspects are set by signal construction, `schiene_t::reserve` (a reserved-direction change forces a
bidirectional signal to `danger`), `rail_vehicle_t::block_reserver`, `rail_vehicle_t::can_enter_tile`, `rail_vehicle_t::leave_tile` and
`karte_t::step_time_interval_signals`. `roadsign_desc_t::get_max_distance_to_signalbox()` is a limit on the signal, not the box: it is
enforced when a signal is built (`tool_build_roadsign_t::check_pos_intern`) and when a signal is transferred between boxes
(`signalbox_t::transfer_signal`, `signalbox_t::transfer_all_signals`). It also governs reservation: a train keeps `moving_block` working
only within that distance of the last moving-block signal and otherwise reverts to `drive_by_sight` (`rail_vehicle_t::block_reserver`).
Station signals (`is_station_signal()`) are registered at the stop as well (`haltestelle_t::add_station_signal`,
`haltestelle_t::remove_station_signal`), and `haltestelle_t::get_station_signals_count()` feeds the platform-starter handling in
`block_reserver`.

## Consist recombination and reservations

Consist recombination — locomotive changes, through coaches, multiple-unit splits and joins, loose
wagons dropped or collected by a pick-up goods train, pool assembly, and the two-portion and
two-convoy patterns — is described in [consist-recombination](consist-recombination.md). This article
covers only its reservation and platform consequences:

- Reservations are per way tile and per convoy handle, with no per-portion identity. Every commit
  releases and re-reserves the changed convoy's tiles with `convoi_t::unreserve_route()` and
  `convoi_t::reserve_own_tiles()`. A new or re-formed portion acquires its own reservations the same
  way, and the retained portion releases the tiles it no longer occupies.
- A train cannot reserve through a tile held by a standing convoy, so a joining portion can enter an
  occupied block or platform only up to the standing train's tile, under call-on and only from a stand.
  The choose-signal coupling implementation stops the mover in the tile before the target's rear tile;
  sharing the contact tile is part of the unimplemented shared-contact package.
- Call-on has two forms. Ordinary permissive working applies at a stand at a permissive signal under
  absolute block, track circuit block or cab signalling with `no_junctions_to_next_signal`: a failed
  reservation sets `roadsign_t::call_on`, switches the train to `drive_by_sight` and re-runs
  `rail_vehicle_t::block_reserver` with `from_call_on = true`, admitting it to the occupied block up
  to the standing train's tile. Choose-signal call-on is target-specific and intrinsic to every choose
  signal: when the current entry has `couple` (or the reciprocal `uncouple`), the target is stationary
  at the stop and only the target obstructs the route, the choose signal routes to the target's rear
  tile and issues call-on. It does not require `get_permissive()` or `no_junctions_to_next_signal()`,
  and `has_call_on` is display-only. Choose-signal call-on covers the choose-signal decision, the
  target-platform route and admission; the shared-contact coupling, exact stop and approach-link
  persistence remain outstanding.
- A laid-over convoy keeps the reservations on its tiles; the halt-deletion path releases them with
  `convoi_t::reserve_own_tiles(true)`.
- The platform a portion occupies must be long enough: `convoi_t::get_tile_length` is the station
  length figure and `convoi_t::get_true_tile_length` the physical length used in route search.

## Performance hotspots

No current measured hotspot for this subsystem. The per-holder reservation index replaced the former
map-wide clearing scan; the new clearing path and its ordinary reserve/unreserve bookkeeping have not been
profiled.

## Open questions

(None.)

## Provenance

Code-verified against ex-15 @ 684d570f9 for every claim above. Reservation mechanics, their determinism
record and the reservation open questions are in
[block-reservations](signals-and-blocks/block-reservations.md). Signalboxes and the working-method
system are Extended-era developments on the Standard Simutrans way and signal base; block reservation's
provenance is in the nested document. Coarse provenance only, per [conventions](conventions.md).
