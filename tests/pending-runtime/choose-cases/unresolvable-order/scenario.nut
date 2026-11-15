//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// A target convoy is loading at the loop platform. The receiver names it, but its consist order cannot be completed from the two consists. The choose signal must hold the receiver at the signal, at danger and with no call-on, and report why.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with choose-harness.nut and the map named by the case in the pakset scenario
// directory.
//

case_kind <- "unresolvable-order"
include("choose-harness")
