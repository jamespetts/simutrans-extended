//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Nested split: the source convoy divides at the junction and the split-off
// portion, already on target line P, divides again at the north stop into a
// retained second vehicle and a new portion on target line Q. Both divisions
// must form valid portions, and the three vehicles must end in three distinct
// consists without duplication or loss.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "nested-split"
include("harness")
