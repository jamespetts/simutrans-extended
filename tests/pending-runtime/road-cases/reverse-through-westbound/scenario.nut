//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// A vehicle approaches a through stop heading west, loads, reverses, keeps its
// stand through the reversal wait, and leaves the stop heading east.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with road-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "reverse-through-westbound"
include("road-harness")
