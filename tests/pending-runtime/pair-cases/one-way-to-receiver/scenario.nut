//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// The receiver takes the target's coach while the target keeps its locomotive.
// The receiver's own coach becomes loose stock at the junction.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with pair-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "one-way-to-receiver"
include("pair-harness")
