//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Depot maintenance runtime case: two convoys of the same rail line are in one
// depot. Train A's locomotive is due for overhaul and its tender and carriage
// are only maintained; train B's carriage is due for overhaul and its
// locomotive and tender are only maintained. depot_t::convoi_arrived() must
// combine the maintained vehicles of both convoys into one new convoy on the
// line, start it, and leave the overhauled vehicles in the two original
// convoys.
//
// The first convoy is started, and the second is started from the depot once
// the first is in overhaul. This keeps the two trains from meeting head-on on
// the fixture's single-track line while still putting both in the depot.
//
// The depot entries in this fixture are plain entries, not conditionally
// skipped, so both convoys can enter with the default
// simline_t::max_convoys_at_depot of 1. A normal line with conditional depot
// entries needs max_convoys_at_depot >= 2 for two convoys of the same line to
// hold maintenance places at the same depot.
//
// See maintenance-harness.nut for the fixture and the assertions.
//
// Run through scripts/run-pending-runtime-tests.sh:
//
//   PENDING_RUNTIME_CASES=maintenance-cross-convoy scripts/run-pending-runtime-tests.sh
//

case_kind <- "maintenance-cross-convoy"
maintenance_rail_case <- true
maintenance_cross_convoy_case <- true
include("maintenance-harness")
