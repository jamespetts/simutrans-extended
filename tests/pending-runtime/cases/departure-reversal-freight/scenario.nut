//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Freight departure reversal: a powered leader and a brake van with no
// head-end cab arrive eastbound but their target line leaves west from the
// junction. convoi_t::get_terminal_shunt_mode uses a turning or shunting mode.
// The two-vehicle array order stays the same, every vehicle is turned, and the
// locomotive still leads with the brake van still trailing.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "departure-reversal-freight"
include("harness")
