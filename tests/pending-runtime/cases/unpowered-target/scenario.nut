//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Unpowered completed target order: the displaced vehicle is the only vehicle
// named by target line P's order at entry 21, so the order is complete, but
// the resulting portion has no power. The portion is not promoted and remains
// loose stock.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "unpowered-target"
include("harness")
