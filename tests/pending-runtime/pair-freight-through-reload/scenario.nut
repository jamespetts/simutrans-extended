//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Paired H exchange with a loaded through goods wagon, followed by save/reload:
// the target brings the loaded wagon to the junction and the exchange transfers
// that wagon to the receiver. The case prints "RUNTIME CASE
// pair-freight-through-reload READY" and holds while a network client joins the
// server, so that the server saves its state, sends that save to the client and
// loads it again. After the load the case re-asserts the two final consists and
// that the wagon still holds its whole load. It then lets the receiver deliver
// the goods at the delivery halt and passes when the delivery books a through
// fare.
//
// Run through scripts/run-pending-runtime-reload-tests.ps1 or .sh with the
// pair-freight-through-reload selector, harness and scenario name; see those
// runners.
//

case_kind <- "pair-freight-through-reload"
include("pair-harness")
