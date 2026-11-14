//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Broken target: the source entry names the id of a line that was created and
// then destroyed. The target does not resolve, so the displaced vehicle
// remains loose stock at the junction.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "broken-target"
include("harness")
