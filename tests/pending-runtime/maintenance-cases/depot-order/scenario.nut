//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Depot maintenance runtime case: a line-bound rail convoy visits a depot with
// a non-blank Depot Consist Order on the depot schedule entry. The order names
// the convoy's three vehicles in consist order. On arrival the convoy must
// decompose: the Lead Vehicle stays in the original convoy and the other
// vehicles wait in the Depot Vehicle Pool with their own maintenance jobs.
// When every vehicle has been released, Depot Assembly must re-form the
// original consist in the order's vehicle order and the convoy must leave the
// depot.
//
// See maintenance-harness.nut for the fixture and the assertions.
//
// Run through scripts/run-pending-runtime-tests.sh:
//
//   PENDING_RUNTIME_CASES=depot-order scripts/run-pending-runtime-tests.sh
//

case_kind <- "depot-order"
maintenance_rail_case <- true
maintenance_depot_order_case <- true
include("maintenance-harness")
