//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Paired H exchange with a loaded loose goods wagon, followed by save/reload:
// the wagon that the receiver brings to the junction holds goods bound for
// the junction halt and goods bound beyond it. The exchange releases that wagon
// to the junction as loose stock; the goods bound for the junction are delivered
// there and book their fare, and the rest stays aboard. The case prints
// "RUNTIME CASE pair-freight-loose-reload READY" and holds while a network
// client joins the server, so that the server saves its state, sends that save
// to the client and loads it again. After the load the case re-asserts the two
// final consists, the laid-over wagon, the fare the wagon booked at the junction,
// and that the wagon still holds only the goods bound beyond the junction. It
// then lets the receiver collect the wagon at the junction and passes when the
// delivery of the remaining goods books a further fare.
//
// Run through scripts/run-pending-runtime-reload-tests.ps1 or .sh with the
// pair-freight-loose-reload selector, harness and scenario name; see those
// runners.
//

case_kind <- "pair-freight-loose-reload"
include("pair-harness")
