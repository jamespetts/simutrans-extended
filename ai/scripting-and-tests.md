---
status: draft
verified: ex-15 @ 97029f453 (2026-11-14, scripting remainder); ex-15 @ 0692167b6 (2026-11-15, signalling API additions in the inventory); ex-15 @ bf157b7a7 plus working-tree pick-up goods API (2026-11-17, scenario cargo API and snapshot fields); ex-15 @ dafe7031e plus working-tree test tooling (2026-11-18, suite options and timings); test-harness facts as cited; suite count 81/81 (2026-11-14); ex-15 @ a201bf056d plus working-tree crossroads road case (2026-11-23)
---
# Scripting & tests

**Covers:** script/ (Squirrel virtual machine (VM) host and application programming interface
(API)), squirrel/ (vendored language source), tests/ (all_tests.nut, test_*.nut,
test_helpers.nut, scenario.nut, empty-16x16.sve), scripting-and-tests/ (per-feature test docs),
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
  schedule, consist-order, convoy-observation and signalling API tests.
  [EXECUTION-VERIFIED:2026-11-14, 81 of 81 tests, run with `-nomidi`].
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
- `halt_x.add_cargo` and `halt_x.clear_cargo` mutate the saved world directly rather than
  travelling as a network command. They exist for scenario setup, which runs on the server only,
  and both are refused unless the game is offline or is a server with no connected client. A
  direct mutation with a client connected would not be applied by that client and would
  desynchronise; a client that joins later receives the server's complete saved state. The
  injected packet is an ordinary `ware_t` on the halt's waiting list, so the savegame format is
  unchanged.
- RNG: script/query code must not consume `simrand`. `way_builder_t::is_allowed_step` consumes it
  only in the river bautyp; `vehicle_builder_t::vehicle_search` consumes it. Read-only planner
  queries should be audited and tested to leave the RNG seed unchanged.
- `set_tool_api` does not broadcast; do not use it as the action path for a network bot. Actions
  belong on `welt->set_tool()` from a client, or on a server-local command that goes through
  `clone()` and the broadcast path.

### Scenario cargo setup

`halt_x.add_cargo` and `halt_x.clear_cargo` are scenario-authoring calls, not player actions: they
have no cost, ownership check or tool. `add_cargo(goods, amount, destination, next_transfer,
arrival_time[, origin, destination_pos, class])` appends one ordinary cargo packet to the halt's
waiting list and returns `true`; invalid input raises a Squirrel error instead. The optional
arguments after `arrival_time` are, in order, the origin halt (default: this halt), the delivery
position (default: the destination halt's basis position) and the cargo class (default:
0; it must be below `goods_desc_t::get_number_of_classes()`). The packet's fields are:

| argument | `ware_t` field | behaviour |
| --- | --- | --- |
| `goods` | `index`, through the `goods_desc_t` | must be a known goods descriptor |
| `amount` | `menge` | must be a positive 32-bit quantity; repeated calls append separate packets rather than merging |
| `destination` | `ziel` | the ultimate destination halt, which must handle the cargo kind |
| `next_transfer` | `zwischenziel` | the halt at which the packet must leave its next convoy; an unbound halt (for example `halt_x(0)`) means `destination` |
| `arrival_time` | `arrival_time` | stored unchanged; `add_ware_to_halt(ware, true)` does not replace it with the current tick |
| `origin` | `origin` | the halt at which the journey began; an unbound halt means this halt; the fare calculation uses it for the distance from the origin that the recorded leg does not cover |
| `destination_pos` | `zielpos` | normally the consumer industry's position; it must be within the map |
| `class` | `g_class` | zero for goods |

`haltestelle_t::add_cargo` sets `last_transfer` to this halt through `add_ware_to_halt`,
`is_commuting_trip` to false, and leaves `comfort_preference_percentage` at the value that
`ware_t::ware_t(const goods_desc_t*)` gives, which is zero. That is harmless for freight, which does
not use it; a passenger packet created this way does not carry the value that
`karte_t::generate_passengers_and_mail` assigns (between 100 and the configured maximum), and the API
has no argument for it. It also calls `fabrik_t::update_transit(ware, true)`, like the halt save
loader, so a consumer's in-transit statistic includes the packet. The halt's storage capacity is not
checked: an over-capacity packet is accepted and the halt's ordinary overcrowding state follows.

`halt_x.clear_cargo()` removes all waiting and transferring packets at the halt without booking
revenue or a refund. It decrements the consumers' in-transit statistics, clears the halt's
current-wait snapshot (`current_waiting_times`) while leaving the retained `waiting_times` samples
alone, sets `resort_freight_info` and calls `recalc_status()`. It returns `true`.

Both calls change the saved world directly and are refused while a network client is connected, as
described in the read/write split above. The injected packet is ordinary saved halt cargo, so
`add_cargo` adds no savegame field; `clear_cargo` removes saved packets but adds no field either.

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
  `add_cargo` places a normal cargo packet with an explicit routing and arrival time, and
  `clear_cargo` discards the halt's waiting cargo; both are scenario-setup calls.
- Line: name, owner, schedule, goods category, capacity, convoy count, revenue, cost, profit,
  distance, way tolls, convoy list, waytype.
- Convoy: name, position, owner, waytype, schedule and current entry, state, reversal, capacity,
  revenue, cost, profit, distance, tolls, line, speed, loading level, home depot, obsolete vehicles,
  withdrawal, depot state, waiting state and tile length. `get_vehicle_snapshot` returns value
  objects containing each slot's descriptor, owner, reversal, cargo quantity, capacity, `pos`,
  `route_index`, `steps`, `steps_next` and `speed` (the convoy's current speed in km/h), plus the
  road-specific `stand_mask` (the four-bit stop-stand reservation, or zero), `pulled_over`
  (whether the road vehicle is beside the travel lane at a stand: loading, laid over or
  stopped at the stand) and `overtaking`
  (whether the road convoy is in the passing lane; false for other waytypes); it also carries
  the loose recipient (`loose_recipient_id`
  and the `loose_recipient_is_convoy`, `loose_recipient_is_same` and `loose_recipient_is_any` flags),
  the earliest `ware_t::arrival_time` among the vehicle's packets and the collection credit ticks
  (`collection_loose_since`, `collection_time`). `get_vehicles` separately retains the Standard
  descriptor-list interface. `get_current_loading_time` returns the loading time assigned at the
  current stop, and `get_arrival_time` the convoy's arrival tick there. `get_working_method` returns
  the working method of a convoy whose front vehicle derives from `rail_vehicle_t`, or -1 for any
  other convoy.
- Player: construction, vehicle maintenance, new vehicles, income, infrastructure maintenance,
  assets, cash, net wealth, profit, operating profit, margin, powerline revenue, transported
  passengers/mail/goods, convoy count, way tolls, line list, and way-access permissions
  (`set_allow_access_to`, `allows_access_to`).
- Tiles: ground, slope, way, depot, halt, objects, neighbours, climate, height. For rail ways,
  `way_x` exposes `get_reserved_convoi`, `get_reservation_type`, `get_reserved_direction` and
  `can_reserve`, which accepts a convoy and an optional direction.
- Descriptors: vehicles, buildings, ways, bridges, tunnels, goods, signals, way objects; costs,
  capacities, availability, intro/retire dates. Vehicle descriptors additionally expose total and
  overcrowded capacity and minimum/maximum loading times. Signal descriptors expose `get_permissive`,
  `get_has_call_on`, `get_has_selective_choose`, `get_working_method`, `get_aspects`,
  `get_normal_danger`, `get_double_block`, `is_station_signal`, `get_max_distance_to_signalbox`,
  `get_signal_group` and `get_allow_underground` (0 = above ground only, 1 = underground only,
  2 = both); the existing `is_choose_sign` and `is_end_choose_signal` identify choose and
  end-of-choose signs. Building descriptors expose `is_signalbox`, `get_radius` and
  `get_clusters`.
- Signalboxes and signalling commands: `signalbox_x` exposes `get_capacity`, `get_signal_count`,
  `get_signal_list`, `can_add_more_signals` and `can_add_signal`. `command_x.build_signalbox`
  builds a signalbox with an optional rotation; the signalbox form of
  `command_x.build_sign_at` attaches a signal to an explicit `signalbox_x`, and the
  three-argument form remains for signals that need no box. Scripted builds do not use the
  player's selected signalbox. `command_x.can_build_signalbox_at` returns null for a valid
  site or the signalbox tool's error message otherwise, including the player's funds check;
  `command_x.signalbox_get_price` returns the amount in 1/100 credits that the player pays,
  including land value. Both take an explicit rotation and do not consume `simrand`.
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
  settings, the per-slot `optional` flag and the per-alternative `load_requirement` are preserved.
  Conversion rejects duplicate or dangling identifiers, incompatible categories and other malformed
  data rather than silently discarding data.

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
- Suite runs use `-fast_forward`, which starts the simulation in fast-forward mode instead of
  advancing it at wall-clock speed; the `fast_forward` simuconf setting
  (`env_t::max_acceleration`) caps the step rate. The tests assert game-time state, so this changes
  only how long a run takes; `karte_t::set_fast_forward` ignores the flag in network mode.
  `-debug 1` keeps the
  script's own `print` output and the engine's error lines, but suppresses the per-step warning
  log; the suite does not assert warning text. A full run took about 4 seconds end to end,
  including the pakset load, against about 56 seconds at `-debug 2` without fast-forward
  [EXECUTION-VERIFIED:2026-11-18, ex-15 @ dafe7031e plus working-tree test tooling, Debug x64].
- The direct Windows recipe uses a `simutrans/pak128.Britain-Ex/scenario/automated-tests`
  directory junction pointing to the repository's `tests/` directory. With working directory
  `simutrans/`, invoke `Simutrans-Extended-debug.exe -use_workdir -objects pak128.Britain-Ex
  -lang en -scenario automated-tests -debug 1 -nomidi -nosound -fast_forward`. Standard error
  contains the test progress and assertion markers. Remove the directory junction after the run.
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

### Test cadence

A suite is a gate, not an edit loop:

- During iteration, run only the case or the smallest subset that can observe the changed branch.
  The pending-runtime fixture runner starts several cases at once because its worlds are tiny and
  each case has its own process, log and watchdog; do not start two separate runner invocations at
  once, because the work directory, ports and watchdog ownership are shared between invocations.
  The selection rules and measured costs are in
  [consist-recombination](scripting-and-tests/consist-recombination.md).
- Run the full suite for a package once, when the package's behaviour is complete and before it is
  committed or pushed. Do not repeat a full suite that already passed in the same session when only
  test or scenario code has changed since; record the earlier result instead.
- Stop a run that can no longer produce new evidence and keep its partial log. Completing a suite
  for its own sake is not a reason to spend the machine time.
- Prefer a deterministic fixture that supplies the required state directly over one that waits for
  the state to emerge through production, traffic or a long simulation. The direct fixture is both
  faster and reproducible.
- Kill stale game processes before a run so that a previous process cannot hold the work directory
  or a port.

The per-feature cases, their selection rules and their measured costs are in the feature's test
doc, for example [consist-recombination](scripting-and-tests/consist-recombination.md).

## Feature test docs

Testing details for a feature live in that feature's own test doc, keyed here. They do not belong in
this master doc or in the general feature doc; the feature doc links to its test doc from a short
Tests section. See [documentation-architecture](documentation-architecture.md) for the policy.

- [consist-recombination](scripting-and-tests/consist-recombination.md) — read when running,
  extending or debugging the recombination runtime fixture (`tests/pending-runtime/`), its harnesses,
  cases or reload runners.
- [road-vehicle-operations implementation](road-vehicle-operations/implementation.md#tests) — read
  when running the `road-cases/` group of the pending-runtime fixture (`single-run`,
  `moving-gap`, `overtake`, `junction`, `crossroads`, `queue`, `stop-release`,
  `reverse-through`, `reverse-through-westbound`, `terminal-stands`, `terminal-turnover`,
  `through-pass`, `through-pass-oncoming`), the `save-reload-stand` reload case, or extending
  the road-space, stop-stand and private-car tests.

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
- Should `halt_x.add_cargo` expose `comfort_preference_percentage` for passenger and mail packets?
  The current call leaves it at zero, which is not the value that generated passengers receive.
- join-sync on ex-15: the fixture is a 14.x-era save; loading it with the ex-15 engine and the
  ex-15 pakset pin is unverified. If it fails, restrict `join-sync.yml` to master or host a
  branch-specific fixture.
- join-sync runner sizing: both engine processes hold the whole BB map simultaneously; the
  standard 16 GB / 4-core GitHub runner has not been measured against this fixture.

## Provenance

Scripting findings at ex-15 @ c082c94ae: `script/script.cc`, `script/script_loader.cc`,
`script/export_objs.cc`, `script/api/*`, `script/api_param.cc`,
`simutrans/script/script_base.nut`, `dataobj/scenario.cc`, `dataobj/schedule.cc`,
`dataobj/consist_order_t.h`, `network/network_cmd_ingame.cc`, `player/simplay.cc`,
`player/simplay.h`, `simmenu.h`, `simmain.cc`, `simtool.cc`, `simtool.h`, `bauer/hausbauer.cc`
and the build source lists. The signalling API additions are `signalbox_x`, the signal,
signal-descriptor, convoy, way and vehicle-slot getters, the signal-construction commands, the
signalbox site-validity and price queries, and the signed object-type parameter handling.
The API breadth is a code inventory; only the explicitly marked tests and fixture outcomes are
execution-verified. The `-fast_forward` command-line option is in `simmain.cc` (after the world is
loaded, `args.has_arg("-fast_forward")` calls `karte_t::set_fast_forward(true)`);
`scripts/run-automated-tests.sh` passes it with `-debug 1` and `-nosound`. Fixture-specific
provenance is in [consist-recombination](scripting-and-tests/consist-recombination.md).
Other test-harness and CI facts retain their cited provenance (master @ 78a4bb3b9 / ex-15 @
91d9b252e; mapgen @ 0954e8c3c; smoke/join facts as cited there).
