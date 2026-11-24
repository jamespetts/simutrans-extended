//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Depot maintenance dwell runtime case: a rail convoy led by the locomotive
// whose starting availability is below 100 runs one lap and is maintained on
// its return to the depot, because the depot schedule entry carries the
// maintain_or_overhaul visit flag. The harness also appends a carriage, because
// the locomotive cannot be the last vehicle. See maintenance-harness.nut for the
// fixture and the assertions.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file with
// maintenance-harness.nut and the fixture maps in the pakset scenario directory:
//
//   PENDING_RUNTIME_GROUPS=maintenance scripts/run-pending-runtime-tests.sh
//

case_kind <- "maintenance-dwell-rail"
maintenance_rail_case <- true
include("maintenance-harness")
