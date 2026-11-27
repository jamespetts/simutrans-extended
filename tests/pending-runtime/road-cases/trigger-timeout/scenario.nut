//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// A convoy waits at a stop for a departure trigger that is never sent, with a
// maximum waiting time set on the entry. It must remain in AWAITING_TRIGGER
// until that wait, measured from its arrival, has expired, then leave and
// continue to the road end.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with road-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "trigger-timeout"
include("road-harness")
