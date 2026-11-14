//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Receiver [loco A, coach A] and target [loco B, coach B] swap locomotives. The
// receiver is started first and waits; the target arrives and executes the
// exchange. Both final consists must be exact and no vehicle may become loose
// stock.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with pair-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "symmetric-swap"
include("pair-harness")
