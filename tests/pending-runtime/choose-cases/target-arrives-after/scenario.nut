//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// The receiver arrives first with no target present, selects a platform and waits. The target starts later and must become the mover, routed to the receiver under call-on.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with choose-harness.nut and the map named by the case in the pakset scenario
// directory.
//

case_kind <- "target-arrives-after"
include("choose-harness")
