//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Rear-portion order: the source consist is [retained, split, split_tail].
// The source order retains only the leader, so the split-off group is the rear
// two vehicles. The target order is two slots whose alternatives are
// wildcards, so the order matches the group in either array order and a
// reversed collection order reaches convoi_t::match_consist_order unchanged. The case asserts that the portion's leading
// vehicle is the one that stood immediately behind the retained leader, not
// the rear vehicle of the group.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with harness.nut and empty-16x16.sve in the pakset scenario directory.
//

case_kind <- "rear-portion-order"
include("harness")
