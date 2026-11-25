//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Two adjacent junctions: a convoy enters the first junction from the south and
// turns east into the second, whose east arm leads into a bend. A second convoy
// approaches through the bend in the opposite direction and continues west
// through both junctions. Each junction entry is checked against the other
// convoy's tile, so both must be able to proceed without a deadlock.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with road-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "junction-curve"
include("road-harness")
