//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Reversed target schedule: the source entry requests reverse traversal of
// target line P's schedule. The portion adopts target entry 21 at schedule
// index 1 and, after the shunting delay, advances in reverse traversal to
// entry 20 at index 0.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "reversed-target"
include("harness")
