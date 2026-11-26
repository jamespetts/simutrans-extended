//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Fleet Target Fleet Size runtime case: two line-bound Convoys start held in
// the depot. The line target is raised to 1, then 2, and each step must
// dispatch the lowest-id Held Convoy. Lowering the target to 1 must withdraw
// one Deployed Convoy to the next depot on its schedule and hold it there.
//
// See maintenance-harness.nut for the fixture and the assertions.
//
// Run through scripts/run-pending-runtime-tests.sh:
//
//   PENDING_RUNTIME_CASES=fleet-target scripts/run-pending-runtime-tests.sh
//

case_kind <- "fleet-target"
maintenance_rail_case <- true
maintenance_target_case <- true
include("maintenance-harness")
