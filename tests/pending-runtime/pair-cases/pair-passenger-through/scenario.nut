//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// The target's loaded passenger carriage is transferred directly to the
// receiver in the paired exchange. Its passengers continue beyond the junction
// and the receiver delivers them at the delivery halt.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with pair-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "pair-passenger-through"
include("pair-harness")
