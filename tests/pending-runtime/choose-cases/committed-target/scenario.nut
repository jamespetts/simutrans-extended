//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Committed target: the target leads out of the depot and the receiver follows
// immediately. The receiver reaches the choose signal while the target is still
// running between that signal and the platform, so the signal must hold the
// receiver at danger and record the reciprocal approach with the receiver as the
// mover. When the target starts loading, the signal routes the receiver to the
// contact tile under call-on.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with choose-harness.nut and the map named by the case in the pakset scenario
// directory.
//

case_kind <- "committed-target"
include("choose-harness")
