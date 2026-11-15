//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// A solvable paired exchange reached through the choose signal. The target waits at the loop platform with an uncouple entry naming the receiver; the receiver couples to it under call-on and the joint commit lays out both final consists and the loose stock on the pre-exchange slots.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with choose-harness.nut and the map named by the case in the pakset scenario
// directory.
//

case_kind <- "pair-layout"
include("choose-harness")
