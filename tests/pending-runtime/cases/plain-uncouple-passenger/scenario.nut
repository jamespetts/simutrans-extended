//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Plain uncouple with a loaded passenger carriage: the source entry carries
// the uncouple flag with target id zero, so the carriage displaced by the
// source order remains loose stock at the junction. The carriage holds two
// passenger packets: one whose next transfer is the junction stop, which must
// leave at the stop, and one whose next transfer is the east stop, which must
// stay aboard as a through load.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "plain-uncouple-passenger"
include("harness")
