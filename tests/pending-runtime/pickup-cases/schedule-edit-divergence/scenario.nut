// The source service runs A-B-P and the target service initially runs B-D-P.
// A trailing wagon loads for both D and P. The mid-run target edit removes P,
// so the two packets require different portions at B and must both leave the
// wagon there. A leading wagon remains full for P and cannot reload the
// discharged P packet during the source convoy's loading pass at B.
case_kind <- "schedule-edit-divergence"
include("pickup-harness")
