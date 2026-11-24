//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Depot maintenance runtime case: the convoy runs one lap and is maintained on
// its return to the depot, because the depot schedule entry carries the
// maintain_or_overhaul visit flag. See maintenance-harness.nut for the fixture
// and the assertions.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file with
// maintenance-harness.nut and the fixture maps in the pakset scenario directory:
//
//   PENDING_RUNTIME_GROUPS=maintenance scripts/run-pending-runtime-tests.sh
//

case_kind <- "maintenance-dwell"
include("maintenance-harness")
