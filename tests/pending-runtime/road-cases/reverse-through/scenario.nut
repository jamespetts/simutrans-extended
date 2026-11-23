//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// A vehicle loads at a through stop whose schedule entry reverses, keeps its
// stand through the reversal wait, and leaves the stop heading west.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with road-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "reverse-through"
include("road-harness")
