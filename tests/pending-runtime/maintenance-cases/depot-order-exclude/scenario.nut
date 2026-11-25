//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Depot maintenance runtime case: a line-bound rail convoy visits a depot with
// a Depot Consist Order that names only its first two vehicles. The last
// vehicle is due for overhaul and is not named by the order, so it must stay in
// the Depot Vehicle Pool while the original Convoy departs with the Lead
// Vehicle and the other named vehicle.
//
// See maintenance-harness.nut for the fixture and the assertions.
//
// Run through scripts/run-pending-runtime-tests.sh:
//
//   PENDING_RUNTIME_CASES=depot-order-exclude scripts/run-pending-runtime-tests.sh
//

case_kind <- "depot-order-exclude"
maintenance_rail_case <- true
maintenance_depot_order_exclude_case <- true
include("maintenance-harness")
