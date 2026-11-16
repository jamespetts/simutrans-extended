---
status: draft
verified: ex-15 @ 03bf5306e (2026-11-16, handoff branches and approach-link lifetime)
---
# Shared-contact reservation handoff

**Covers:** vehicle/rail_vehicle.cc (`rail_vehicle_t::leave_tile`,
`rail_vehicle_t::hand_over_contact_reservation`), simconvoi.cc
(`convoi_t::recombination_approach_partner`, `convoi_t::recombination_approach_is_mover`,
`convoi_t::is_recombination_contact_tile`, `convoi_t::process_recombination_at_stop`,
`convoi_t::clear_recombination_approach`), boden/wege/schiene.cc (`schiene_t::unreserve`,
`schiene_t::change_reservation_holder`). Parent:
[consist-recombination](../consist-recombination.md). Scripted coverage:
[scripting-and-tests/consist-recombination](../scripting-and-tests/consist-recombination.md).

## Terms

The vocabulary used here — target, mover, contact tile, call-on, reservation holder and approach
link — is defined in [consist-recombination](../consist-recombination.md#terms).

## The shared tile

The target reserved the contact tile when it arrived. The mover enters that tile under call-on
without reserving it. Two convoys therefore occupy the tile physically, but only the target holds
its reservation. `rail_vehicle_t::hand_over_contact_reservation` keeps that single-holder state
consistent at the moment either convoy's last vehicle leaves the tile.

## The two branches

`rail_vehicle_t::hand_over_contact_reservation` is called from `rail_vehicle_t::leave_tile` when the
last vehicle of a convoy leaves a tile. It has two branches.

### Release suppression

The ordinary release, `schiene_t::unreserve(vehicle_t*)`, clears whatever holder the tile has,
without checking who it is. If the mover leaves the contact tile while the target still stands
there, that would clear the target's reservation. The branch detects that the holder is another
convoy which still occupies the tile and suppresses the release. The `contact-reservation` case
scripts this branch.

### Transfer

If the convoy leaving *is* the holder, and its reciprocal partner is the mover, and the mover's route
still ends at this tile, and the mover is physically in it, the tile's holder passes to the mover
with `schiene_t::change_reservation_holder` instead of being cleared. This branch needs the approach
link to still exist when the target's last vehicle leaves.

## The approach link's lifetime

The approach link is set when the choose signal adopts the route, and it is what authorises the
mover to enter the contact tile and stop at the coupling point
(`convoi_t::is_recombination_contact_tile`).

It is not permanent. The convoy that carries out the coupling operation does so in
`convoi_t::process_recombination_at_stop`, on its first recombination pass at the stop. That pass
uses the link to commit the new consists (`process_current_entry_consist_order` or
`execute_paired_exchange`), then calls `convoi_t::clear_recombination_approach()`, which clears both
sides. In a one-sided receive the mover is the executing convoy; in a paired exchange either convoy
can execute.

## The normal sequence

While the target is still loading, the mover's first recombination pass finds it through
`schedule_t::get_couple_target`. That function searches the halt's `loading_here` list (the convoys
that requested loading, exposed as `haltestelle_t::get_loading_convois()`). The mover commits the
new consists and clears the link. The target's vehicles then move on later simulation steps, when
there is no link left. The transfer branch is not taken.

## The transfer window

The transfer can run only in a narrower ordering:

1. The choose signal adopts the route while the target is still loading, so the link is set and the
   mover is authorised to enter the contact tile.
2. The target starts departing and its state advances beyond loading. At the start of a later
   loading pass, `haltestelle_t::request_loading` removes it from `loading_here` because it is no
   longer in a loading, reversing or waiting-for-clearance state.
3. The mover enters the contact tile, because the link and its route still point there, but its
   recombination pass does not find the target in `loading_here`. `schedule_t::get_couple_target`
   returns absent, so the mover waits in `AWAITING_RECOMBINATION` and keeps the link.
4. The target's last vehicle leaves the contact tile while the mover is in it. The transfer branch
   now finds the link and the mover in the tile, and passes the reservation to the mover.

The window is narrow for two reasons. The target must still hold the contact tile when the mover
enters, because `convoi_t::is_recombination_contact_tile` compares the tile with the target's current
contact position. It must also already have started departing, because otherwise the mover's pass
would find it in `loading_here` and commit. A target that is already departing before the route is
adopted is not an available coupling target
(`convoi_t::get_recombination_target_state`), so the choose signal does not create the window itself.

The current fixture does not produce that ordering deterministically; the coverage note is in
[scripting-and-tests/consist-recombination](../scripting-and-tests/consist-recombination.md#open-questions).

## Open questions

- Is the transfer branch a defensive case for a target that starts departing after the approach is
  adopted, or should it be treated as unreachable and removed? No fixture exercises it.
- If the transfer is to become a normal, testable case, should the approach link survive the mover's
  commit until the holder leaves instead of being cleared at the commit? That is a design change, not
  a test change.

## Provenance

The two branches and the shared-tile invariant were checked against ex-15 @ 71f192a65 and
`03bf5306e` (2026-11-15 and 2026-11-16). The link lifetime and the transfer window are a reading of
`convoi_t::process_recombination_at_stop`, `convoi_t::clear_recombination_approach`,
`haltestelle_t::request_loading` and `schedule_t::get_couple_target` at ex-15 @ 03bf5306e
[CODE ex-15 @ 03bf5306e]. The release-suppression half is execution-verified by the
`contact-reservation` case [EXECUTION-VERIFIED:2026-11-16, release and DEBUG builds].
