//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// No target is present and an unrelated convoy occupies the scheduled main
// platform. The choose signal must divert the receiver to the free loop
// platform without issuing call-on.
//

case_kind <- "target-absent-occupied"
include("choose-harness")
