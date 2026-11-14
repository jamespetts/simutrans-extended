//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// The receiver's first slot is a rule matching the target's two powered
// vehicles, and the target's first slot names its own leading locomotive.
// Independent solving would claim the same vehicle; the joint solver must place
// the other powered vehicle in the receiver.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with pair-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "conflict-backtrack"
include("pair-harness")
