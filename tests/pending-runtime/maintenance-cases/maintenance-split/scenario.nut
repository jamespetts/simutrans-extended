//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Depot maintenance runtime case: a lined convoy arrives at a depot with its
// last vehicle due for overhaul and the locomotive and tender due only for
// routine maintenance. depot_t::convoi_arrived() must form the maintained
// vehicles into a new convoy, keep it on the line, advance its schedule past
// the depot entry and start it, while the overhauled vehicle stays in the depot
// in the OVERHAUL state. See maintenance-harness.nut for the fixture and the
// assertions.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file with
// maintenance-harness.nut and the fixture maps in the pakset scenario directory:
//
//   PENDING_RUNTIME_CASES=maintenance-split scripts/run-pending-runtime-tests.sh
//

case_kind <- "maintenance-split"
maintenance_rail_case <- true
maintenance_split_case <- true
include("maintenance-harness")
