---
status: draft
verified: ex-15 @ 235a9eddf plus paired reload fixture @ fa85a59b8 (2026-11-14); test-harness facts as cited; suite count 80/80 (2026-11-14)
---
# Scripting & tests

**Covers:** script/ (Squirrel virtual machine (VM) host and application programming interface
(API)), squirrel/ (vendored language source), tests/ (all_tests.nut, test_*.nut,
test_helpers.nut, scenario.nut, empty-16x16.sve),
gui/scenario_frame/scenario_info (→ [gui](gui.md)).

## Overview

- `script_vm_t` owns a Squirrel VM and a Squirrel thread used for queued calls.
  `script_loader_t::start_vm` loads `simutrans/script/script_base.nut`, loads a host-specific base
  file, then calls
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
- The scenario host executes read-only queries and tool calls. A suspended tool call is resumed
  with its result by `nwc_tool_t::do_command` as described below.
- The active Squirrel suite completes under `Debug|x64` with `pak128.Britain-Ex`, including the
  schedule, consist-order and convoy-observation tests.
  [EXECUTION-VERIFIED:2026-11-14, 80 of 80 tests, run with `-nomidi`].
- The API was ported from Standard. Extended schedule entries and complete consist orders are
  script-accessible, but other Extended fields remain incomplete, including parts of the
  line/convoy/replacement read model. Use the exported declarations in `script/api/` as the
  authority for what each object exposes.
- The AI host mode is designed but unused; expect latent assumptions (global `welt`, null
  scenario, GUI calls, `get_my_player` returning null) until it is exercised.

## Scripted tool completion

`call_tool_init` and `call_tool_work` assign a unique `tool_t::callback_id` when they suspend the
Squirrel virtual machine and register that virtual machine in `suspended_scripts_t`. `nwc_tool_t`
carries the identifier through construction, copying and packet serialisation. After executing the
tool, `nwc_tool_t::do_command` calls `suspended_scripts_t::tell_return_value` only on the
originating peer: an init-phase call returns a Boolean and a work-phase call returns its null or
error-string result. An init-phase tool call that reaches `do_command` reports successful execution
rather than exposing `tool_t::init`'s separate has-further-work meaning. Interactive tool-result
handling is suppressed when a script is waiting for the result.

`scenario_t::init` still logs `error [suspended] calling start` when the first scripted tool
suspends normally during `start`; that warning alone does not show that a call failed to resume.
The overall test timeout catches an unresumed call. The scripted NWC_TOOL packet addition has not
had a two-process network execution test.

`set_tool_api` appends a server-local `nwc_tool_t` to the command queue without broadcasting, and
`nwc_tool_t::clone` clears `WFL_SCRIPT` for commands not sent by the server; the path is therefore
not a safe model for multiplayer-synchronised scripted actions. Network mechanics:
[network](network.md); sync rules: [sync-and-determinism](sync-and-determinism.md).

## Read/write split and determinism

- The API contains getters, command-backed mutators and direct mutators. `line_x.change_schedule`
  and `convoy_x.change_schedule` use the existing tool path, including the network packet-size
  check. A direct mutator on a network client changes local state without a network command and
  desynchronises the client. A bot or query VM must register only getters and command-backed
  mutators appropriate to that VM's location.
- `call_tool_init`/`call_tool_work` set `WFL_SCRIPT`; `nwc_tool_t::clone` clears it for commands
  not sent by the server, so client scripts do not bypass the normal player-authentication check.
- RNG: script/query code must not consume `simrand`. `way_builder_t::is_allowed_step` consumes it
  only in the river bautyp; `vehicle_builder_t::vehicle_search` consumes it. Read-only planner
  queries should be audited and tested to leave the RNG seed unchanged.
- `set_tool_api` does not broadcast; do not use it as the action path for a network bot. Actions
  belong on `welt->set_tool()` from a client, or on a server-local command that goes through
  `clone()` and the broadcast path.

## API inventory

- World: time, map size, season, players, attraction/label/convoy lists, monthly and yearly
  statistics (citizens, growth, towns, factories, convoys, city cars, passenger/mail/goods ratios
  and totals). The scenario host can also create and remove players through existing commands.
- City: name, position, bounds, growth, citizens, buildings, city cars, generated and transported
  passenger/mail, monthly and yearly history.
- Factory: name, position, production, power, electric/passenger/mail boosts, passenger/mail
  statistics, tile list, halt list, transformer, field count, descriptor, goods
  storage/received/consumed/in transit/delivered/produced.
- Halt: name, owner, waiting, happy/unhappy/no-route, convoys arrived, line list, convoy list,
  read-only laid-over convoy list, tile list, factory list, freight to destination, capacity.
- Line: name, owner, schedule, goods category, capacity, convoy count, revenue, cost, profit,
  distance, way tolls, convoy list, waytype.
- Convoy: name, position, owner, waytype, schedule and current entry, state, reversal, capacity,
  revenue, cost, profit, distance, tolls, line, speed, loading level, home depot, obsolete vehicles,
  withdrawal, depot state, waiting state and tile length. `get_vehicle_snapshot` returns value
  objects containing each slot's descriptor, owner, reversal, cargo quantity and capacity;
  `get_vehicles` separately retains the Standard descriptor-list interface.
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
- Schedules: `schedule_x` preserves the entry list, schedule-level timing/direction settings,
  `current_stop` and consist orders. A `schedule_entry_x` exposes every `schedule_entry_t` field.
  Its Standard aliases (`load`, `wait`) and Extended names are synchronised, as are raw `flags`
  and the named flag properties. The `orders` table is keyed by `unique_entry_id`;
  `consist_order_x` contains ordered `consist_order_element_x` slots, whose alternatives carry
  either a specific vehicle or every `vehicle_description_element` rule/range field. Recipient
  settings and empty slots are preserved. Conversion rejects duplicate or dangling identifiers,
  incompatible categories and other malformed data rather than silently discarding data.

## Remaining Standard compatibility gaps

The comparison reference for the restoration is Standard `aburch/simutrans` commit
`eb7dafc31b1046420d5ed4c33824d8749bb54175`. The interface is adapted to Extended rather than
importing Standard-only mechanics. Remaining work includes:

- `world.generate_goods`, whose Standard implementation depends on Standard's routing signatures
  and passenger-routing side effects;
- Standard's station-layout transformation, conditional bridge-end search and optional
  pathfinder bridge parameters, which need individual mappings to Extended's builder interfaces;
- `rules.clear_player`, which needs an equivalent per-player operation on `scenario_t`, and the
  global `get_tool_key` method, which needs an adapter for Extended's keycode representation;
- individual revenue, halt-permission, target-city and descriptor query mappings; the current
  exported declarations remain authoritative for these.

## Scenario host and test suite

- Tests are Squirrel scripts run through an in-game scenario (`tests/scenario.nut` and
  `tests/all_tests.nut`); they need a built binary and a pakset.
- The suite covers way, halt, factory, player, terraform, schedules and consist orders, and
  other subsystems; `tests/all_tests.nut` is the authority for the current test list. The test
  runner reports the first `TEST FAILED:` result and stops because subsequent tests would see the
  world modified by the failed test. The optional `AUTOMATED_TESTS_TIMEOUT` environment variable
  controls its overall watchdog.
- The direct Windows recipe uses a `simutrans/pak128.Britain-Ex/scenario/automated-tests`
  directory junction pointing to the repository's `tests/` directory. With working directory
  `simutrans/`, invoke `Simutrans-Extended-debug.exe -use_workdir -objects pak128.Britain-Ex
  -lang en -scenario automated-tests -debug 2 -nomidi`. Standard error contains the test progress
  and assertion markers. Remove the directory junction after the run.
- CI: the Squirrel scenario jobs in `run-tests.yml` (ASan/UBSan and TSan, via
  `scripts/run-automated-tests.sh`) run only on demand (`workflow_dispatch`, `squirrel_tests`
  input). The current Squirrel runner has not had a complete Linux sanitizer run since tool-call
  resumption was restored. The TSan flavour tests threading separately.
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

## Pending-runtime recombination fixture

`tests/pending-runtime/` is a separate fixture for the recombination runtime (Y division and paired H
exchange). It is not part of the default passing suite.

- `harness.nut` builds the infrastructure, convoy, target and consist orders through public script
  interfaces, then owns the Y-division observers and assertions. `pair-harness.nut` does the same for
  two convoys that meet at one stop on two platforms of the same halt, with a reciprocal `couple` /
  `uncouple` entry and an end-state order on each. Both read a case selector from the `case_kind`
  global that the case's `scenario.nut` sets, and print
  `RUNTIME CASE <case> PASS` or `RUNTIME CASE <case> FAIL: <assertion>`.
- `scenario.nut` selects the forward Y case; `cases/<case>/scenario.nut` selects each further Y case;
  `pair-cases/<case>/scenario.nut` selects each paired case. Each case runs in its own game process,
  so the cases cannot contaminate one another.
- `scripts/run-pending-runtime-tests.sh` stages the selected case with its harness and
  `empty-16x16.sve` in the pakset scenario directory, runs one process per case with `-nomidi`,
  and treats a printed PASS marker as the only success. A script error, a process exit before a
  marker, no log progress for the stall timeout, the overall timeout and a crash all fail the
  case. It selects `harness.nut` for `cases/` and `pair-harness.nut` for `pair-cases/`.
  Environment: `PENDING_RUNTIME_TIMEOUT`, `PENDING_RUNTIME_STALL_TIMEOUT`,
  `PENDING_RUNTIME_BINARY`, `PENDING_RUNTIME_OBJECTS`. Exit code 0 means every case printed PASS.
- The paired cases cover a symmetric locomotive swap with either convoy started first, a one-way
  transfer that leaves a vehicle as loose stock, an already-satisfied pair, a joint-solver
  backtracking conflict, an unsatisfiable pair refused all-or-nothing, and a receiver that collects
  a coach from the stop's laid-over pool while the emptied donor is destroyed.
- `reload/scenario.nut` is deliberately outside `cases/`, so the baseline runner does not select
  it. It runs the forward division on a loopback server, holds after the split-off portion forms,
  re-asserts the divided state in `resume_game()` after the join's save/transfer/reload, and
  advances the portion. `pair-reload/scenario.nut` does the same for the paired exchange's
  one-way transfer case: it holds after the commit, re-asserts both final consists and the
  laid-over vehicle from a string of each expected vehicle descriptor's scalar
  `vehicle_desc_x` properties (`get_power()`, `get_topspeed()`, `get_weight()`, `get_length()` and
  the coupling and electrification flags), because descriptor names are not reliable after a load,
  and then observes both convoys advance.
  Runners: `scripts/run-pending-runtime-reload-tests.sh` (POSIX) and
  `scripts/run-pending-runtime-reload-tests.ps1` (Windows). Both start a client to force the join,
  check for desync markers and parsed checklist-pair mismatches, require the joined peers to stay
  alive for an observation window, and then load the preserved join save in a fresh offline
  process. Both default to the division reload and select the pair reload with
  `-CaseName pair-reload -ScenarioName pending-runtime-pair-reload -Selector
  tests\pending-runtime\pair-reload\scenario.nut -Harness tests\pending-runtime\pair-harness.nut
  -HarnessName pair-harness` (PowerShell) or the corresponding `--case`, `--scenario`,
  `--selector`, `--harness` and `--harness-name` options (POSIX). The PowerShell runner stages the
  scenario through a directory junction and removes the junction without following it.
- The fixture uses `is_equal` for vehicle-descriptor and loose-stock identity. Sampled
  `vehicle_desc_x` names are invalid immediately after map load (open bug in
  [known-bugs](known-bugs.md)), so name comparison is not used. Successful cases print convoy
  identities and vehicle counts, not descriptor names.

## Writing new tests

- Follow the existing `tests/test_*.nut` conventions and use `test_helpers.nut`.
- Tests may call tools during `start`; each such call suspends the test until its command executes
  and supplies a result.
- For state that develops over simulation time, use a state machine driven by the scenario
  callbacks rather than assuming a tool call advances all the way to a later stop.
  `scenario_t::step` queues `is_scenario_completed` for each player; explicitly select the player
  whose checks should run once.

## Open questions

- Does the scenario VM's read path work headless? The VM executes, but this is not
  execution-verified.
- Is the `ai` host mode usable as-is? It has no caller and may depend on scenario-only state.
- Which remaining Extended fields need script interfaces? A full gap analysis against the GUI is
  still needed; schedule and consist-order coverage does not make the rest of the API complete.
- The pending-runtime reload runner exercises the Y-division schedule and the paired-exchange
  one-way transfer over loopback, including the restored NWC_TOOL callback, but no CI job runs
  either pending-runtime fixture. The fixture map is an Extended 14 save loaded and re-saved by
  ex-15; whether that is an adequate substitute for an ex-15-native save remains open.
- join-sync on ex-15: the fixture is a 14.x-era save; loading it with the ex-15 engine and the
  ex-15 pakset pin is unverified. If it fails, restrict `join-sync.yml` to master or host a
  branch-specific fixture.
- join-sync runner sizing: both engine processes hold the whole BB map simultaneously; the
  standard 16 GB / 4-core GitHub runner has not been measured against this fixture.

## Provenance

Scripting findings at ex-15 @ 684d570f9 plus working-tree changes: `script/script.cc`,
`script/script_loader.cc`, `script/export_objs.cc`, `script/api/*`, `script/api_param.cc`,
`simutrans/script/script_base.nut`, `dataobj/scenario.cc`, `dataobj/schedule.cc`,
`dataobj/consist_order_t.h`, `network/network_cmd_ingame.cc`, `player/simplay.cc`,
`player/simplay.h`, `simmenu.h`, `simmain.cc` and the build source lists. The API breadth is a
code inventory; only the explicitly marked tests and fixture outcomes are execution-verified.
Other test-harness and CI facts retain their cited provenance (master @ 78a4bb3b9 / ex-15 @
91d9b252e; mapgen @ 0954e8c3c; smoke/join facts as cited there).
