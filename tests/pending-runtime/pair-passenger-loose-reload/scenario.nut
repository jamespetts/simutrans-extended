//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Paired H exchange with a loaded loose passenger carriage, followed by
// save/reload: the carriage that the receiver brings to the junction holds two
// passenger packets. The packet whose next transfer is the junction leaves the
// carriage there; the packet bound beyond the junction stays aboard. The
// exchange releases that carriage to the junction as loose stock, so the
// receiver's final consist holds the target's carriage instead. The case prints
// "RUNTIME CASE pair-passenger-loose-reload READY" and holds while a network
// client joins the server, so that the server saves its state, sends that save
// to the client and loads it again. After the load the case re-asserts the two
// final consists, the laid-over carriage, and that the carriage still holds only
// the packet bound beyond the junction. The reload runner sets
// shunting_time_seconds to 300, so the receiver waits at the junction longer
// than the junction-transfer passenger's maximum tolerable wait; that passenger
// can leave the junction before the receiver departs, and the case then asserts
// the collection and delivery of the carriage's through packet only. It passes
// when the delivery of that packet books a through fare.
//
// Run through scripts/run-pending-runtime-reload-tests.ps1 or .sh with the
// pair-passenger-loose-reload selector, harness and scenario name; see those
// runners.
//

case_kind <- "pair-passenger-loose-reload"
include("pair-harness")
