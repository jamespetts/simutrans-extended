---
status: draft
verified: ex-15 @ edd49a130 (script/ code); test-harness facts as cited
---
# Scripting & tests

**Covers:** script/ (Squirrel VM host, bindings/API), squirrel/ (vendored language source),
tests/ (all_tests.nut, test_*.nut, test_helpers.nut, scenario.nut, empty-16x16.sve),
gui/scenario_frame/scenario_info (→ [gui](gui.md)).

## Overview

- `script_vm_t` owns a Squirrel VM plus a worker thread. `script_loader_t::start_vm` loads
  `simutrans/script/script_base.nut`, loads a host-specific base file, then calls
  `register_export_function(vm, is_scenario)` to register the C++ API.
- Two host modes exist in the API registration: scenario (`is_scenario = true`,
  `scenario_base.nut`, hosted by `scenario_t`) and AI (`is_scenario = false`, `ai_base.nut`,
  intended for an AI player). Only the scenario host is wired: `scenario_t::load_script` starts
  the VM, `scenario_t::step`/`new_month`/`new_year` queue calls, `dynamic_string` serves results
  to clients, and `nwc_scenario_t` carries scenario requests/results over the network.
- No caller starts the AI VM: `simutrans/script/ai_base.nut` defines `start`, `step`,
  `resume_game`, `new_month` and `new_year`, and `register_export_function(vm, false)` registers
  `player.self()`, but no code calls `start_vm("ai_base.nut", ..., false)`.
- `script/export_objs.cc` registers the modules: city, control, convoy, factory, goods
  descriptors, GUI, halt, line, map objects, player, schedule, settings, simple values, string
  methods, tiles, world, pathfinding and commands.
- `script/api/api_command.cc` provides `call_tool_init` and `call_tool_work`. `call_tool_init`
  creates a `tool_t`, sets `WFL_SCRIPT`, and routes it through `karte_t::set_tool_api`;
  `call_tool_work` initialises the tool and calls `karte_t::call_work_api` for the work phase.

## Status

- Scripting is compiled into the game on all three build routes: the MSVC project, the Makefile and
  the CMake source list.
- The scenario host loads and executes scripts, but the tool-call suspension path is
  incomplete. A scenario that calls a tool during `start` suspends and is never resumed;
  `scenario_t::init` logs `error [suspended] calling start`. This is the cause of the
  Squirrel test-suite hang.
- Read-only scripts do not call the tool helpers, so the read path is not blocked by that
  defect.
- The API was ported from Standard and Extended additions are partial. Examples: factory
  suppliers/consumers are commented out, and several line/convoy/consist/replacement fields
  shown in the GUI are absent. A field-level gap analysis against the GUI is required before
  relying on the API as an Extended read model.
- The AI host mode is designed but unused; expect latent assumptions (global `welt`, null
  scenario, GUI calls, `get_my_player` returning null) until it is exercised.
- Scripting-specific bugs are not tracked in [known-bugs](known-bugs.md); this status note
  covers the system.

## The suspended-call defect

`call_tool_init` and `call_tool_work` assign a unique `tool->callback_id` and register the VM in
`suspended_scripts_t` when the tool is suspended. Nothing resumes that VM:

- `suspended_scripts_t::remove_suspended_script` has no caller;
- the callback invocation in `nwc_tool_t::do_command` is commented out;
- `player_t::tell_tool_result` only plays sounds and opens error windows.

The intended fix is to resume the registered VM from the tool execution path with the tool
result, or to add an explicit result channel. `set_tool_api` appends a server-local `nwc_tool_t`
to the command queue without broadcasting, and `nwc_tool_t::clone` clears `WFL_SCRIPT` for
commands not sent by the server; the path is therefore not a safe model for
multiplayer-synchronised scripted actions. Network mechanics: [network](network.md);
sync rules: [sync-and-determinism](sync-and-determinism.md).

## Read/write split and determinism

- The API contains both getters and direct mutators (`set_name`, `change_size`,
  `change_schedule`, `set_line`, `destroy`, `set_traffic_level`, and others). A direct mutator on
  a network client changes local state without a network command and desynchronises the client.
  A bot or query VM must register only the getters, or run on the server.
- `call_tool_init`/`call_tool_work` set `WFL_SCRIPT`; `nwc_tool_t::clone` clears it for commands
  not sent by the server, so client scripts do not bypass the normal player-authentication check.
- RNG: script/query code must not consume `simrand`. `way_builder_t::is_allowed_step` consumes it
  only in the river bautyp; `vehicle_builder_t::vehicle_search` consumes it. Read-only planner
  queries should be audited and tested to leave the RNG seed unchanged.
- `set_tool_api` does not broadcast; do not use it as the action path for a network bot. Actions
  belong on `welt->set_tool()` from a client, or on a server-local command that goes through
  `clone()` and the broadcast path.

## API inventory

- World: time, map size, season, players, attraction/convoy lists, monthly and yearly statistics
  (citizens, growth, towns, factories, convoys, city cars, passenger/mail/goods ratios and
  totals).
- City: name, position, bounds, growth, citizens, buildings, city cars, generated and transported
  passenger/mail, monthly and yearly history.
- Factory: name, position, production, power, electric/passenger/mail boosts, passenger/mail
  statistics, tile list, halt list, transformer, field count, descriptor, goods
  storage/received/consumed/in transit/delivered/produced.
- Halt: name, owner, waiting, happy/unhappy/no-route, convoys arrived, line list, convoy list,
  tile list, factory list, freight to destination, capacity.
- Line: name, owner, schedule, goods category, capacity, convoy count, revenue, cost, profit,
  distance, way tolls, convoy list, waytype.
- Convoy: name, position, owner, waytype, schedule, capacity, revenue, cost, profit, distance,
  tolls, line, vehicle list, speed, loading level, home depot, obsolete vehicles, withdrawal,
  depot state, waiting state, tile length.
- Player: construction, vehicle maintenance, new vehicles, income, infrastructure maintenance,
  assets, cash, net wealth, profit, operating profit, margin, powerline revenue, transported
  passengers/mail/goods, convoy count, way tolls, line list.
- Tiles: ground, slope, way, depot, halt, objects, neighbours, climate, height.
- Descriptors: vehicles, buildings, ways, bridges, tunnels, goods, signals, way objects; costs,
  capacities, availability, intro/retire dates.
- Pathfinding: `way_planner_x` exposes `set_build_types` and `is_allowed_step`, plus a binary
  heap. It does not expose `way_builder_t::calc_route`, `get_route`, `calc_costs` or `build`.
  The engine's full route planner is therefore unavailable to scripts. Planning implications:
  [bot-api](bot-api.md).
- Schedules: entries and construction.

## Scenario host and test suite

- Tests are Squirrel scripts run through an in-game scenario (`tests/scenario.nut` and
  `tests/all_tests.nut`); they need a built binary and a pakset.
- The suite covers way, halt, factory, player, terraform and other subsystems; `tests/` is the
  authority for the current test list.
- CI: the Squirrel scenario jobs in `run-tests.yml` (ASan/UBSan and TSan, via
  `scripts/run-automated-tests.sh`) are suspended and run only on demand (`workflow_dispatch`,
  `squirrel_tests` input). Their last full run hung until the 10-minute timeout after
  `error [suspended] calling start` at test 1/64 (see above). The wrapper does not recognise
  `[suspended]` as a failure pattern, so the failure surfaces as a timeout. The TSan flavour is
  a separate matter from the suspension defect.
- Push/PR CI gates on the smoke harness instead: smoke plus network determinism on
  `tests/demo.sve` via `scripts/run-smoke-tests.sh`, with the pakset pinned in
  `tests/pakset-pin.tab`. The TSan smoke flavour is its own workflow, `tsan-smoke.yml`.
  Mechanics: [build-and-toolchain](build-and-toolchain.md).
- World generation is covered by `scripts/run-mapgen-tests.sh`: a nine-case size/seed/town-count
  matrix driving headless `-generate_map` (`-map_size`, `-map_seed`, `-map_towns`,
  `-map_factories`, `-map_attractions`, `-map_water_level`). Pass = exit 0, `MAP-GEN: PASS`
  line and no failure markers; hangs fail via the per-run watchdog. `-map_seed` seeds all RNG
  streams, so one seed gives an identical `MAP-GEN:` result line across server/GUI and
  release/debug builds. Delete `settings-extended.xml` from the workdir before every run;
  factory placement remains config-sensitive in DEBUG builds ([performance](performance.md)).
- Smoke-harness network determinism oracle: the two loopback runs are compared on their
  per-step semantic private-car route-hash sequences, not on byte-identity of the final saves.
  Route-map list indices are allocated in thread-timing-dependent order, so byte-different
  `final.sve` files can be semantically identical; the byte comparison is informational only.
  The Windows local runner `scripts/run-smoke-tests.ps1` follows the same rule.
- Network-join gate: `scripts/run-join-sync-test.sh` and `join-sync.yml` replay a loopback
  server/client join on the Bridgewater-Brunel fixture `bb6-apr-2010.sve`. One MULTI_THREAD
  headless binary plays both roles; headless clients work because `-load net:` ends in
  `network_connect()` and uses only socket/file IO. Oracles: desync/kick log markers,
  per-step semantic route hashes over common steps (last occurrence per step wins on the
  server because the join pause re-derives the step counter), checklist-pair mismatches, and
  generic failure markers. The fixture is not in the repo: the workflow reads
  `BB_SYNC_FIXTURE_URL` (optional `BB_SYNC_FIXTURE_SHA256`) and SKIPs while unset.

## Writing new tests

- Follow the existing `tests/test_*.nut` conventions and use `test_helpers.nut`.
- Do not call tools during `start` until the suspended-call path is fixed; either use read-only
  queries or defer the tool call until the scenario is running.

## Open questions

- Does the scenario VM's read path work headless? The VM executes, but this is not
  execution-verified.
- Is the `ai` host mode usable as-is? It has no caller and may depend on scenario-only state.
- Which Extended fields are missing from the API? A gap analysis against the GUI is needed.
- What is the correct fix for the suspended-call path: resume from `nwc_tool_t::do_command`, or
  add an explicit tool-result channel? The current `callback_id` mechanism is incomplete.
- How to run the suite locally on Windows: the exact recipe is not verified. CI invocation
  mechanics are in [build-and-toolchain](build-and-toolchain.md).
- join-sync on ex-15: the fixture is a 14.x-era save; loading it with the ex-15 engine and the
  ex-15 pakset pin is unverified. If it fails, restrict `join-sync.yml` to master or host a
  branch-specific fixture.
- join-sync runner sizing: both engine processes hold the whole BB map simultaneously; the
  standard 16 GB / 4-core GitHub runner has not been measured against this fixture.

## Provenance

Scripting findings verified against ex-15 @ edd49a130: `script/script.cc`,
`script/script_loader.cc`, `script/export_objs.cc`, `script/api/*`, `script/api/api_command.cc`,
`dataobj/scenario.cc`, `network/network_cmd_ingame.cc`, `player/simplay.cc`, `player/simplay.h`,
`simmenu.h`, `simmain.cc` and the build source lists. The API breadth is a code inventory, not an
execution-verified claim. Test-harness and CI facts are inherited from the earlier version of
this doc with their cited commits (master @ 78a4bb3b9 / ex-15 @ 91d9b252e; mapgen @ 0954e8c3c;
smoke/join facts as cited there).
