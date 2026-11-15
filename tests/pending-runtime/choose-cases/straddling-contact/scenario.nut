//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// The target is one long vehicle whose rear end lies in the tile behind its front tile. The receiver must stop at the coupling point in that behind tile and must not enter the target's front tile.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with choose-harness.nut and the map named by the case in the pakset scenario
// directory.
//

case_kind <- "straddling-contact"
include("choose-harness")
