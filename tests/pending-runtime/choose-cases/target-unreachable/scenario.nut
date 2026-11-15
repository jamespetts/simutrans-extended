//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// The target waits on a third platform. After it arrives, a one-way sign makes
// that platform unreachable from the receiver's choose signal. The receiver
// must stay at the signal without call-on and report the unusable geometry.
//

case_kind <- "target-unreachable"
include("choose-harness")
