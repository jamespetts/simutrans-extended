//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Depot maintenance withdrawal runtime case: a rail convoy led by the
// locomotive whose starting availability is below 100 runs one lap and enters
// the MAINTENANCE state on its return to the depot, because the depot schedule
// entry carries the maintain_or_overhaul visit flag. The case then toggles the
// withdraw flag, which destroys the convoy while it is in the depot's
// maintenance queue. It checks that the game survives the following sync steps
// and that the depot drops the destroyed convoy from its convoy list. See
// maintenance-harness.nut for the fixture and the assertions.
//
// This case covers the null-handle path: convoi_t::~convoi_t() detaches the
// convoy's convoihandle_t, and depot_t::sync_step() and
// depot_t::is_awaiting_attention() must not dereference that handle after it has
// been detached.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file with
// maintenance-harness.nut and the fixture maps in the pakset scenario directory:
//
//   PENDING_RUNTIME_GROUPS=maintenance scripts/run-pending-runtime-tests.sh
//

case_kind <- "maintenance-withdraw"
maintenance_rail_case <- true
maintenance_withdraw_case <- true
include("maintenance-harness")
