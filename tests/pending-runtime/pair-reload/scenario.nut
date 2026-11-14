//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Paired H exchange followed by save/reload: the receiver takes the target's
// coach while the target keeps its locomotive, and the receiver's own coach
// becomes loose stock at the junction. The committed exchange is asserted as in
// the one-way-to-receiver case. The case then prints "RUNTIME CASE pair-reload
// READY" and holds before PASS while a network client joins the server, so that
// the server saves its state, sends that save to the client and loads it again.
// After the load the case re-asserts both final consists and the laid-over
// vehicle from the saved fingerprints, prints "RUNTIME CASE pair-reload
// RESTORED" and passes only when both convoys advance past their pair entries.
//
// Run through scripts/run-pending-runtime-reload-tests.ps1 or .sh with the
// pair-reload selector, harness and scenario name; see those runners.
//

case_kind <- "pair-reload"
include("pair-harness")
