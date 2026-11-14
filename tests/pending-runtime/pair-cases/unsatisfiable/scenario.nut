//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// The receiver's order names a vehicle that is in neither consist, so the
// exchange must be refused all-or-nothing: neither consist changes, no loose
// stock is created, and both convoys continue.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with pair-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "unsatisfiable"
include("pair-harness")
