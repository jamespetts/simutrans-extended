//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Service-frequency runtime case: the convoy runs two laps of the maintenance
// fixture line, so it leaves a stop, visits the depot for routine maintenance
// and leaves the depot again. The depot departure must not discard the line's
// measured round-trip sample; after the second stop departure the line's
// service frequency must be a positive lap time. See maintenance-harness.nut
// for the fixture and the assertions.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file with
// maintenance-harness.nut and the fixture maps in the pakset scenario directory:
//
//   PENDING_RUNTIME_CASES=service-frequency scripts/run-pending-runtime-tests.sh
//

case_kind <- "service-frequency"
maintenance_service_frequency_case <- true
include("maintenance-harness")
