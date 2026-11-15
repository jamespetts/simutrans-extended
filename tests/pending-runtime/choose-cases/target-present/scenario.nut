//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// A target convoy is loading at the loop platform. The receiver names it and is scheduled for the main platform. The choose signal must route the receiver to the loop platform and admit it under call-on.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with choose-harness.nut and the map named by the case in the pakset scenario
// directory.
//

case_kind <- "target-present"
include("choose-harness")
