//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Target-order pool collection: a separate convoy releases one vehicle into
// the junction's laid-over pool before the source arrives. The source then
// uncouples at the junction, and target line P's order requires both the
// displaced vehicle and the prepared laid-over vehicle, so the promoted
// portion carries both.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "pool-collection"
include("harness")
