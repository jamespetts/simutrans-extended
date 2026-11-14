//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Lineless convoy target: the source entry names a convoy that is not on a
// line. The promoted portion receives a copy of that convoy's schedule, is not
// assigned to a line, keeps its own identity and advances normally. The target
// convoy remains in the depot with its original vehicle and schedule.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "lineless-target"
include("harness")
