//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// A convoy waits at a stop for a departure trigger that is never sent. It must
// remain in AWAITING_TRIGGER without aborting in convoi_t::sync_step().
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with road-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "trigger-wait"
include("road-harness")
