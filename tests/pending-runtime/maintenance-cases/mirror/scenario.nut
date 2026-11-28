//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Mirrored conditional depot runtime case: a mirrored schedule with a
// conditional depot at its first entry is first skipped, then the vehicle is
// made due for routine maintenance on the outbound leg. The convoy must visit
// the depot on a later approach instead of reusing the skip decision taken
// before the early turn. See maintenance-harness.nut for the fixture and the
// assertions.
//
// Run through scripts/run-pending-runtime-tests.sh:
//
//   PENDING_RUNTIME_CASES=mirror scripts/run-pending-runtime-tests.sh
//

case_kind <- "mirror"
maintenance_mirror_case <- true
maintenance_rail_case <- true
include("maintenance-harness")
