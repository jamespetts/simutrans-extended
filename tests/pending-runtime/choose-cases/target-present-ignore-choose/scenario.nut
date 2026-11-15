//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// As target-present, but the receiver entry also sets ignore_choose. The flag must suppress only free-platform selection, so target routing and call-on still happen.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with choose-harness.nut and the map named by the case in the pakset scenario
// directory.
//

case_kind <- "target-present-ignore-choose"
include("choose-harness")
