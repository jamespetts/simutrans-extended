//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// The symmetric locomotive swap with a departure trigger on the target's pair
// entry. The receiver arrives first and waits in AWAITING_RECOMBINATION. The
// target arrives and is held in AWAITING_TRIGGER. The receiver must recognise
// the trigger-held target as its pair counterpart and carry out the exchange;
// the exchange must leave both final consists exact and must not clear the
// target's trigger wait. The receiver then reaches its next stop and sends the
// trigger, and the target departs.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with pair-harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "pair-trigger-wait"
include("pair-harness")
