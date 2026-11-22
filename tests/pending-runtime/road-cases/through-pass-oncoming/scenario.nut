//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// A vehicle pulled over at a through stop must be passed by a same-direction
// vehicle while a continuous stream of oncoming traffic shares the stop tile.
// The passer must stay out of the opposite carriageway (overtaking false), the
// holder must remain loading and pulled over, and at least three distinct
// oncoming convoys must use the stop tile.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with road-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "through-pass-oncoming"
include("road-harness")
