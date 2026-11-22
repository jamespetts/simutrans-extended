// Without a TPO, mail uses the goods system. The loose van keeps the packet
// whose next transfer is B and delivers the packet whose destination is B.
case_kind <- "mail-no-tpo"
include("pickup-harness")
