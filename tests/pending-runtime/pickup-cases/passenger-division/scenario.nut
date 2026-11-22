// A passenger railcar loaded at A for P is carried by the split-off portion of
// a division at B. The source schedule only runs A-B; the target line's B-P
// schedule is the railcar's committed serving portion. P is twelve tiles from
// A, beyond the pakset's eight-tile walking radius, so the halt cannot deliver
// the passengers on foot before the source loads them.
case_kind <- "passenger-division"
include("pickup-harness")
