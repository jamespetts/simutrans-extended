//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// The target's loaded goods wagon is transferred directly to the receiver in
// the paired exchange. It keeps its load, and the receiver delivers it at the
// delivery halt and books the fare.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with pair-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "pair-freight-through"
include("pair-harness")
