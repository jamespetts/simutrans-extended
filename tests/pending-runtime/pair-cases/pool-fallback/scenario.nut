//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// The receiver's order needs a coach that neither consist holds. A separate
// convoy first releases that coach to the junction's laid-over pool; the
// receiver must collect it and the emptied donor must be removed.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with pair-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "pool-fallback"
include("pair-harness")
