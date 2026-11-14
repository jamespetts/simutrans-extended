//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Plain uncouple: the source entry carries the uncouple flag with target id
// zero. The vehicle displaced by the source order remains loose stock at the
// junction and no portion receives a schedule.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "plain-uncouple"
include("harness")
