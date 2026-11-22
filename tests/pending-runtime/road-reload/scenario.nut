//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// The vehicle loads at a through stop and holds the stand; a network join saves
// and reloads the world; the saved stand mask and pulled-over state must survive
// and the vehicle must then depart and reach the terminal.
//
// Run through scripts/run-pending-runtime-reload-tests.ps1 with:
//   -CaseName save-reload-stand
//   -ScenarioName pending-runtime-road-save-reload-stand
//   -Selector tests\pending-runtime\road-reload\scenario.nut
//   -Harness tests\pending-runtime\road-harness.nut
//   -HarnessName road-harness
//

case_kind <- "save-reload-stand"
include("road-harness")
