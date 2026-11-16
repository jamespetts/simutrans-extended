//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Facing: a targetless consist order reverses a two-vehicle consist. The two
// vehicles couple in one order only; one of them is bidirectional, so the
// runtime must turn it to make the reversed order valid. The case asserts the
// reversed sequence and that a vehicle was turned.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "facing"
include("harness")
