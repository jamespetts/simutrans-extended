---
status: draft
verified: ex-15 @ 9f0c02daf0 plus working tree (2026-11-27, consist terminology)
---
# Glossary

**Covers:** terminology project-wide.

## Vehicles and operations

- **Consist**: The coupled formation of vehicles that moves as one unit, together with its
  schedule, line membership, name and state. A Consist may be a single vehicle. The term covers
  railway trains, road combinations, ships and aircraft. The object is represented in the code by
  `convoi_t`. The ordered list of its vehicles is its **vehicle sequence**. A **Consist Order** is
  the ordered list of vehicle slots that a schedule entry requires, stored in `schedule_t::orders`.
  The user interface uses "consist" for the `convoi_t` object. Mechanics:
  [vehicles-and-convoys](vehicles-and-convoys.md), [consist-recombination](consist-recombination.md).
- **Convoy**: The historic name for a Consist, retained in C++ identifiers (`convoi_t`,
  `convoy_t`, `convoihandle_t`), file names (`simconvoi.cc`, `convoy.cc`), Squirrel API names
  (`convoy_x`, `convoy_list_x`) and developer documentation. A Convoy and a Consist are the same
  object.

## Money

- **Simucredit** (plural **Simucredits**), abbreviated **Cr**: the game's displayed currency
  unit. The UI appends `$` as the money symbol.
  [RECOLLECTION:2026-11-17 user statement]
- **Simucent** (plural **Simucents**): 1/100 of a Simucredit. Engine money is stored as a
  signed integer in Simucents. The name has the traditional "cent" meaning of a subdivision,
  not the currency unit itself. [RECOLLECTION:2026-11-17 user statement] Details and conversion
  traps: [economy-and-passengers](economy-and-passengers.md#money-and-currency).

## Planned entries

Each term gets a one-paragraph definition plus a `[CODE]` anchor (file + symbol) where the
concept is implemented. Terms to define (inventory from conversation and code so far, definitions
deliberately deferred to the breadth pass — priors are not evidence):

- halt / haltestelle (stop) · line · schedule / schedule entry
- pak / pakset · pak version · makeobj
- waytype · besch / desc (descriptor) · grund (tile/ground) · koord / ribi
- climate · relief / heightfield
- factory / industry chain · ware (goods) · catchment
- reservation · block · signalbox · checkpoint
- checklist · desync
- Extended vs. Standard · ex-15 · Experimental (historical name)
- kat (if used) · transshipment · journey time · comfort · classes (passenger/vehicle)

## Open questions

- Which Extended-specific terms deserve entries vs. coverage inside domain docs.
