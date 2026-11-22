// A goods wagon loaded at A for two next transfers, D and P, is carried by one
// split-off portion. The target line's B-D-P schedule serves both destinations,
// so the committed portion places both packets in the same wagon and the
// portion takes the wagon through both stops.
case_kind <- "multi-destination-same-portion"
include("pickup-harness")
