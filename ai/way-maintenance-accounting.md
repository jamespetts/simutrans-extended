---
status: draft
verified: master @ 8823186ac
---
# Way maintenance accounting

**Covers:** the way-side booking paths into the per-player infrastructure-maintenance array
`finance_t::maintenance[]` (`player/finance.h`, updated from `player/simplay.cc`):
`boden/wege/weg.{h,cc}`, `bauer/wegbauer.cc` (road and track builders), `boden/grund.cc`
(`grund_t::neuen_weg_bauen`), `obj/bruecke.cc` and `obj/tunnel.cc` (bridge/tunnel
suppression). The finance side of the array is documented in
[economy-and-passengers](economy-and-passengers.md).

## The maintenance array and the way contribution

- `finance_t::maintenance[]` is a per-player array of nominal monthly amounts, one entry per
  transport type plus a total. `player_t::add_maintenance()` adds or removes an amount; the
  month change charges the total (`finance_t::new_month()`, `player_t::new_month()`).
- The array is **not saved**. On load, `karte_t::plans_finish_rd()` calls `finish_rd()` on
  every map object, and each object re-adds its contribution, so the array is rebuilt from the
  map rather than loaded.
- A way tile contributes its desc's `get_maintenance()` (×10/14 when the tile is diagonal) and
  its way length (10, or 7 when diagonal).
- `weg_t::book_maintenance(player, way_desc, sign)` centralises that arithmetic, including the
  diagonal correction. It books the maintenance only if `charges_own_maintenance()`; the
  way-length statistic is always booked.

## Which object books a tile's way maintenance

- A way books its own maintenance only if it has an owner and its ground tile has no bridge or
  tunnel of the way's waytype; otherwise the bridge or tunnel books its own per-tile amount.
  `weg_t::charges_own_maintenance()` is the single test, consulted by every way booking path.
- The rule applies per waytype; piers do not suppress the way's charge. The way-length
  statistic is separate and always follows the way.

## Booking paths

- `weg_t::set_desc()`: removes the old desc's contribution and adds the new desc's, for the
  owner current at the time it runs. Used by renewals, upgrades and city road changes.
- `weg_t::finish_rd()`: adds the current desc's contribution for the current owner. Called by
  the load rebuild and by `grund_t::neuen_weg_bauen()` for new ways.
- `weg_t::~weg_t()`: removes the contribution. It uses the stored diagonal flag rather than
  recomputing it, because the way may already be detached from the map.
- `weg_t::change_owner(new_owner)`: removes the contribution from the old owner, sets the owner
  and adds it to the new owner. Use this for gameplay ownership changes; a bare
  `obj_t::set_owner()` moves no booking and leaves the contribution with the previous owner,
  because `set_desc()` books on the owner current when it runs.
- `weg_t::stop_charging_own_maintenance()` / `start_charging_own_maintenance()`: called by the
  bridge and tunnel lifecycle when a bridge or tunnel of the way's waytype appears on or is
  removed from the way's tile; removes or adds the way's maintenance contribution (the
  way-length statistic is not affected).
- `bruecke_t::change_owner()` / `tunnel_t::change_owner()`: move the bridge's or tunnel's own
  per-tile charge between owners.
- `obj_t::set_owner()` (or `set_owner_nr()` on load) remains correct where the booking must not
  move: the load path uses `set_owner_nr()`, and new ways are booked by
  `neuen_weg_bauen()`/`finish_rd()`.

## Invariant and special cases

- For each (tile, waytype), `finance_t::maintenance[]` contains exactly one amount for that
  tile: the way's, or, when a bridge or tunnel of that waytype occupies the tile, the bridge's
  or tunnel's. The load rebuild restores this; in-session operations maintain it through the
  entry points above.
- Only bridge and tunnel construction and removal change which object books a tile (the two
  transfer operations above). A way whose ground can no longer be found is treated as having no
  bridge or tunnel.

## Known problems

- `weg_t::renew()` bills a price computed before `replacement_way` is finalised, so a renewal
  can charge the cost of a different way type than the one installed.

## Open questions

- Is a derived rebuild of way contributions (instead of incremental add/remove) viable for
  large maps, and should the other object types that add to the same array follow?
