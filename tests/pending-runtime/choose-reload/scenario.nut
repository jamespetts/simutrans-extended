//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Choose-signal coupling approach followed by save/reload: the receiver is
// held at the choose signal because another convoy blocks the target-platform
// approach. The target is loading and the receiver's approach link is
// recorded. The case prints "RUNTIME CASE choose-reload READY" and holds
// while a network client joins the server, so that the server saves,
// transfers and reloads its state. After the load the case re-asserts the
// reciprocal approach link through the public script API, prints RESTORED and
// passes.
//
// Run through scripts/run-pending-runtime-reload-tests.ps1 or .sh with
// -CaseName choose-reload -ScenarioName pending-runtime-choose-reload
// -Selector tests\pending-runtime\choose-reload\scenario.nut
// -Harness tests\pending-runtime\choose-harness.nut -HarnessName choose-harness.
//

case_kind <- "choose-reload"
include("choose-harness")
