//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Reversed source traversal: the source runs a mirrored schedule and starts at
// its last entry. It reaches that entry in forward traversal, then traverses
// the schedule in reverse to the junction uncouple entry. The division must
// form the portion while the source reports reverse schedule traversal.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "reversed-source"
include("harness")
