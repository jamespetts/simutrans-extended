---
status: draft
verified: master @ ec5e97fc7
---
# Way maintenance accounting

**Covers:** the way-side booking paths into the per-player infrastructure-maintenance stock:
`boden/wege/weg.{h,cc}`, `bauer/wegbauer.cc` (road and track builders), `boden/grund.cc`
(`grund_t::neuen_weg_bauen`), `obj/bruecke.cc` and `obj/tunnel.cc` (bridge/tunnel
suppression). The stock itself lives in `player/finance.h` / `player/simplay.cc`
(→ [economy-and-passengers](economy-and-passengers.md)).

## The stock and the way contribution

- `finance_t::maintenance[]` is a per-player stock of nominal monthly amounts, one entry per
  transport type plus a total. `player_t::add_maintenance()` adds or removes an amount; the
  month change charges the total (`finance_t::new_month()`, `player_t::new_month()`).
- The stock is **not saved**. On load, `karte_t::plans_finish_rd()` calls `finish_rd()` on
  every map object, and each object re-adds its contribution, so the stock is rebuilt from the
  map rather than loaded.
- A way tile contributes its desc's `get_maintenance()` (×10/14 when the tile is diagonal) and
  its way length (10, or 7 when diagonal).
- `weg_t::book_maintenance(player, way_desc, sign)` centralises that arithmetic, including the
  diagonal correction.

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
- `obj_t::set_owner()` (or `set_owner_nr()` on load) remains correct where the booking must not
  move: the load path uses `set_owner_nr()`, and new ways are booked by
  `neuen_weg_bauen()`/`finish_rd()`.

## Invariant and special cases

- For a way that carries its own maintenance, the stock holds exactly one contribution per tile
  for its current desc and owner. The load rebuild restores this; in-session operations must
  maintain it. Not all existing ownership-change sites maintain it yet — see Known problems.
- Ways on bridges and tunnels do **not** carry their own maintenance: the bridge or tunnel
  charge replaces it (`bruecke_t::finish_rd()`, `tunnel_t::finish_rd()` remove the way charge;
  `tunnel_t::cleanup()` restores it when the tunnel is removed). `change_owner()` must not be
  used for them.

## Known problems

- `weg_t::renew()` bills a price computed before `replacement_way` is finalised, so a renewal
  can charge the cost of a different way type than the one installed.
- `weg_t::degrade()` sets the owner to NULL before `set_desc()`, so a player-owned public right
  of way keeps being charged after it becomes unowned.
- Ways on bridges and tunnels: `weg_t::set_desc()` books the way's own maintenance on a desc
  change although the bridge or tunnel carries it.
- Ownership changes outside the way builders (`player/simplay.cc`, `simcity.cc`, `simtool.cc`)
  adjust finance manually with raw, non-diagonal-adjusted maintenance and are not routed
  through `weg_t::change_owner()`.

## Open questions

- Should the bridge/tunnel suppression become a `weg_t` predicate consulted by every booking
  path, or an explicit per-way booked state maintained by the bridge/tunnel classes?
- Is a derived rebuild of way contributions (instead of incremental add/remove) viable for
  large maps, and should the other object types that feed the same stock follow?
