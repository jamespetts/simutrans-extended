//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// The target waits at the loop platform. The receiver reaches the contact tile
// and its one-sided receive takes both target vehicles. The emptied target is
// destroyed and the receiver's former vehicles become loose stock, which must be
// laid out behind the receiver rather than overlapping the target's former
// slots.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with choose-harness.nut and the map named by the case in the pakset scenario
// directory.
//

case_kind <- "one-sided-layout"
include("choose-harness")
