//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// The same symmetric locomotive swap with the target started first. The target
// must wait rather than divide, and the receiver must execute the same
// exchange.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with pair-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "symmetric-swap-target-first"
include("pair-harness")
