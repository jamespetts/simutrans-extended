//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Depot maintenance runtime case: a lined convoy arrives at a depot with a
// valid maintained group, but its next schedule entry after the depot is an
// isolated halt that cannot be reached. split_maintained_from_overhaul() fails
// its route check after installing the schedule and line, so it must undo the
// split, restore the original consist and leave no new convoy behind.
// See maintenance-harness.nut for the fixture and the assertions.
//
// Run through scripts/run-pending-runtime-tests.sh:
//
//   PENDING_RUNTIME_CASES=maintenance-split-unreachable scripts/run-pending-runtime-tests.sh
//

case_kind <- "maintenance-split-unreachable"
maintenance_rail_case <- true
maintenance_split_unreachable_case <- true
include("maintenance-harness")
