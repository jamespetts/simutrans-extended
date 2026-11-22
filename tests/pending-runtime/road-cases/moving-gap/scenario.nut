//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Two convoys follow on a straight road at speed; the follower must keep a reaction distance of about 20 m at 50 km/h behind the moving leader, not a standstill gap.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with road-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "moving-gap"
include("road-harness")
