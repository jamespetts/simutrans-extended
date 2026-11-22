//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Two convoys leave the depot in the same step and follow one another on the straight road; the following vehicle must not pass the leading vehicle''s rear.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with road-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "queue"
include("road-harness")
