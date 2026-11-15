//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// The target is waiting on the loop platform, and an unrelated convoy stands on the approach to that platform. The choose signal must remain at danger and the receiver must not enter under call-on.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with choose-harness.nut and the map named by the case in the pakset scenario
// directory.
//

case_kind <- "target-platform-blocked"
include("choose-harness")
