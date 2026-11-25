//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Depot maintenance runtime case: a lined convoy arrives at a depot with its
// locomotive due for overhaul and only its tender and carriage in the
// maintained group. That group is unpowered, so
// split_maintained_from_overhaul() must refuse the split, restore the original
// consist and leave the convoy in OVERHAUL without asserting.
// See maintenance-harness.nut for the fixture and the assertions.
//
// Run through scripts/run-pending-runtime-tests.sh:
//
//   PENDING_RUNTIME_CASES=maintenance-split-invalid scripts/run-pending-runtime-tests.sh
//

case_kind <- "maintenance-split-invalid"
maintenance_rail_case <- true
maintenance_split_invalid_case <- true
include("maintenance-harness")
