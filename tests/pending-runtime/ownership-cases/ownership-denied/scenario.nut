//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Ownership case: player 2 leaves a vehicle in the junction's laid-over pool
// and player 0's collector must not take it, because player 0 has not granted
// player 2 access to its ways. The consist order must fail and leave the
// vehicle in the pool.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with ownership-harness.nut and empty-16x16.sve in the pakset scenario
// directory.
//

case_kind <- "ownership-denied"
include("ownership-harness")
