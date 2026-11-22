//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// The receiver's loaded goods wagon is left loose at the junction. Goods bound
// for the junction are delivered there; the rest stays aboard under the
// through-wagon model, and the receiver collects the wagon on its return visit
// and delivers the load.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with pair-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "pair-freight-loose"
include("pair-harness")
