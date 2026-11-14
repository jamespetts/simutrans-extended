//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Both end-state orders equal the pre-exchange consists. The exchange must
// execute without moving a vehicle, without creating loose stock, and without
// leaving either convoy waiting.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with pair-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "already-at-end-state"
include("pair-harness")
