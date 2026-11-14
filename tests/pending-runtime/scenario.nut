//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Forward Y division: the source entry names target line P and target entry
// 21. The displaced vehicle becomes the split-off portion, adopts line P and
// advances from target entry 21 to entry 22.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "forward"
include("harness")
