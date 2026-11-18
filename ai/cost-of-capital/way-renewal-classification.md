---
status: draft
verified: cost-of-capital @ d80af444a
---
# Way renewal classification

**Covers:** the split of a way replacement cost between Revenue Renewal and Capital
Improvement: `way_builder_t::split_way_replacement_cost` and
`way_builder_t::calc_way_restoration_cost` (`bauer/wegbauer.{h,cc}`), and their callers
`weg_t::renew` (`boden/wege/weg.cc`), `way_builder_t::build_road` and
`way_builder_t::build_track`. Terms are defined in
[cost-of-capital](../cost-of-capital.md#terms).

## Purpose

Revenue Renewal and Capital Improvement are defined in
[cost-of-capital](../cost-of-capital.md#terms). The split exists so that restoring a way's
existing capability is booked as an operating cost (infrastructure maintenance) and improving
it is booked as capital expenditure (construction cost), because the two carry different
consequences for operating profit, the Credit Rating and the Asset Ledger.

## The classifier

`way_builder_t::split_way_replacement_cost` takes the replaced Way Descriptor, the installed
Way Descriptor and the actual replacement cost, and returns the Capital Improvement part while
writing the Revenue Renewal part to its output parameter:

- the replacement is split only when the installed Way Descriptor is at least as good as the
  replaced Way Descriptor on every capability measure: way type, system type
  (`way_desc_t::get_styp`), maximum speed (`way_desc_t::get_topspeed`), maximum axle load
  (`way_desc_t::get_max_axle_load`) and wear capacity (`way_desc_t::get_wear_capacity`);
- otherwise (a downgrade, or better on one measure and worse on another) the whole cost is
  Capital Improvement;
- the Revenue Renewal part is the lower of the actual cost and
  `way_builder_t::calc_way_restoration_cost`, which searches the registered Way Descriptors
  for the least-cost one that is available in the current timeline, is not Mothballed and is no
  worse on every capability measure. The reference descriptor is charged by the same rule as
  the replacement itself: the way-only cost when its Upgrade Group matches the replaced
  descriptor, otherwise the full value including land;
- the Capital Improvement part is the remainder.

## Where the classifier runs

- automatic wear renewal in `weg_t::renew`;
- player-initiated road building and replacement in `way_builder_t::build_road`;
- player-initiated building and replacement of every other way type in
  `way_builder_t::build_track`.

It does not run on tunnels, powerlines, elevated ways, bridges or stations, and it compares the
way cost only, not the land value that a replacement may also incur.

## Booking

Revenue Renewal is booked with `player_t::book_way_renewal` to
`ATV_INFRASTRUCTURE_MAINTENANCE`; Capital Improvement is booked with
`player_t::book_construction_costs` to `ATV_CONSTRUCTION_COST`. The second booking also adds
the amount to the Asset Ledger and the Tax Written-Down Pool.

`player_t::book_way_renewal` raises no Money Message, so `way_builder_t::build_road` and
`way_builder_t::build_track` raise one for the Revenue Renewal part themselves; the message
coalesces with the construction-cost message and shows the whole amount paid.

No part of the interface discloses the classification: the player sees one quotation, not two
lines with a reason.

## Open questions

- The exact scope of the comparison when a project mixes new construction with replacement
  has not been decided; the classifier is applied tile by tile within each builder.
- Whether the classifier should also cover bridges, tunnels and stations depends on whether
  comparable component data can be defined for them.
