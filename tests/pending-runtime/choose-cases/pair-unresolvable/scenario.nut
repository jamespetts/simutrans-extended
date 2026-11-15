//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// A paired exchange whose two end-state orders cannot be solved jointly. The target waits at the loop platform; the receiver must be held at the choose signal instead of being routed to couple.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with choose-harness.nut and the map named by the case in the pakset scenario
// directory.
//

case_kind <- "pair-unresolvable"
include("choose-harness")
