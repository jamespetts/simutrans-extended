//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Shared contact tile: the receiver stops at the target's coupling point and its
// consist order restates its own vehicles, so the receive changes nothing. The
// receiver then departs. When its last vehicle leaves the contact tile, the
// target still occupies and holds that tile, so the release must be suppressed
// and the target must remain the reservation holder.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with choose-harness.nut and the map named by the case in the pakset scenario
// directory.
//

case_kind <- "contact-reservation"
include("choose-harness")
