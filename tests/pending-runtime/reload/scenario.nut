//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Forward Y division followed by save/reload: the source entry names target
// line P and target entry 21. The forward handover is asserted as in the
// forward case. The case then prints "RUNTIME CASE reload READY" and holds
// before PASS while a network client joins the server, so that the server
// saves its state, sends that save to the client and loads it again. After the
// load the case re-asserts the divided state through the public script API,
// prints "RUNTIME CASE reload RESTORED" and passes only when the existing
// split-off portion advances from target entry 21 to entry 22.
//
// Run through scripts/run-pending-runtime-reload-tests.sh, which stages this
// file with harness.nut and empty-16x16.sve in the pakset scenario directory
// and runs the loopback server, the joining client and a fresh offline load of
// the server's network save.
//

case_kind <- "reload"
include("harness")
