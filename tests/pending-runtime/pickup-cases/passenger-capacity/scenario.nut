// Two passenger railcars load at A for P while the source schedule serves P.
// Both fill to their ordinary capacity. The target portion is then edited to a
// destination that does not serve P, so only the retained railcar serves the
// passengers at the division: it fills its standing room and the split
// railcar's passengers are discharged at B instead of being carried onward.
case_kind <- "passenger-capacity"
include("pickup-harness")
