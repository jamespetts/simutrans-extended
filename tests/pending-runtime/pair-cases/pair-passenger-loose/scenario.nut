//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// The receiver's loaded passenger carriage is left loose at the junction. A
// passenger whose next transfer is the junction leaves the carriage and is
// carried by the receiver's final consist or waits at the junction for a later
// service; a passenger whose next transfer is later stays aboard, and the
// receiver collects the carriage on its return visit.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with pair-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "pair-passenger-loose"
include("pair-harness")
