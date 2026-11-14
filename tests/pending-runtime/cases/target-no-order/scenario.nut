//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Target entry without a consist order: target line P exists and has an entry
// at the junction, but that entry carries no order. The target entry cannot
// define a split-off portion, so the displaced vehicle remains loose stock.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "target-no-order"
include("harness")
