// A goods wagon is loaded at A for D while the target portion's B-D schedule
// serves D. The target entry's consist order is then removed by a schedule
// edit, so no split-off portion can be formed and the source entry releases its
// surplus vehicles. The wagon must wait at B in the through pool with its load
// rather than being carried past its next transfer.
case_kind <- "schedule-edit-through-pool"
include("pickup-harness")
