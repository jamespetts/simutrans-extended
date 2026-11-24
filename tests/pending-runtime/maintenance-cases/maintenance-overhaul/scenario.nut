//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Depot maintenance runtime case: the scenario-only test hook sets the leading
// vehicle's distance since the last overhaul above the descriptor's overhaul
// interval, so the scheduled depot visit performs an overhaul. See
// maintenance-harness.nut for the fixture and the assertions.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file with
// maintenance-harness.nut and the fixture maps in the pakset scenario directory:
//
//   PENDING_RUNTIME_GROUPS=maintenance scripts/run-pending-runtime-tests.sh
//

case_kind <- "maintenance-overhaul"
maintenance_rail_case <- true
maintenance_overhaul_case <- true
include("maintenance-harness")
