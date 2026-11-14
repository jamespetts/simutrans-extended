//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Unsatisfiable target order: target line P's order at entry 21 requires the
// displaced vehicle and a second vehicle that is not present at the junction.
// The order cannot be completed, so the displaced vehicle remains loose stock.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "unsatisfiable-target"
include("harness")
