//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// A road vehicle loading at a through stop is pulled over, so a passing road vehicle can share the stop tile in the travel lane.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with road-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "through-pass"
include("road-harness")
