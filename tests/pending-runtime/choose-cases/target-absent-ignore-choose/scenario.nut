//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// No target is present and ignore_choose is set. The receiver must stay on its scheduled occupied main platform approach and must not be diverted to the free loop platform.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with choose-harness.nut and the map named by the case in the pakset scenario
// directory.
//

case_kind <- "target-absent-ignore-choose"
include("choose-harness")
