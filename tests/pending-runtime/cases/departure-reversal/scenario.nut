//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Departure reversal: the source arrives eastbound and its split-off portion
// stands in the same order, but the target line's next stop is the trunk stop
// to the west. The portion must reverse its order at the junction. The case
// uses a multiple unit whose rear car has a driving cab, so
// convoi_t::get_terminal_shunt_mode selects change_direction and the train is
// driven from the rear cab.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "departure-reversal"
include("harness")
