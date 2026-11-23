//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Five convoys cross an unsignalised crossroads from four arms. Two of them
// leave the same depot in the same step and must be able to share the junction
// tile; all five must reach their destinations without deadlock.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with road-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "crossroads"
include("road-harness")
