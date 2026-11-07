---
status: draft
verified: ex-15 @ edd49a130
---
# Bot and AI automation API

**Covers:** proposed `api/` module; `network/` command framework; `simtool.*` and `simdepot.*`
actions; `player/ai*.cc` planners; `bauer/wegbauer.*`; `dataobj/route.*`; `finder/`;
`script/` read model; `gui/minimap.cc` as a map-rendering reference.

Read when planning an external AI/bot interface, assessing whether an AI can build new
infrastructure or only maintain existing infrastructure, adding query or planning functions to
the Squirrel API, or automating a company in a network game.

Code claims are `[CODE ex-15 @ edd49a130]` unless tagged otherwise. Recommendations and designs
are `[UNVERIFIED]` and are not implemented.

## Purpose and target

The target is a bot that controls one company: it observes that company and the world, decides
what to build or change, and issues the corresponding game commands. The intended deployment is
a **bot as a client**: a headless client that joins an existing server as an ordinary client and
acts through the same network command path as a human player. This requires no server change and
uses the existing company-password authentication and frame pacing.

A server-hosted variant (an API module inside the headless server) is also viable, mainly for a
test harness and for operator-run bots; it avoids a second simulation but needs a new trusted
command path and per-company authentication. The two variants share the same action and query
library.

## Why the action side is feasible

- The main in-world company actions are already `tool_t` commands. `nwc_tool_t` carries the tool
  id, position, `default_param`, flags, init/work phase, up to 256 bytes of custom data, the
  sender's checklist and the sync step. `nwc_tool_t::clone` performs the ownership, player-unlock
  and scenario checks, then broadcasts the command to every peer.
- Tool coverage includes build/terraform/way/station/depot/signals, line create/delete/schedule/
  withdraw/livery/colour, convoy schedule/line/withdraw/replace, depot buy/insert/remove/sell/
  copy/start, rename, and related operations. Most management actions therefore do not need a
  new game-action protocol.
- Headless operation exists: `COLOUR_DEPTH=0` builds, a headless client can join with
  `-load net:`, and `-fast-network-sync` runs a loopback server at high speed for testing.
  `-generate_map` produces reproducible worlds.
- `nettools/` is an existing out-of-process protocol client (admin `NWC_SERVICE` only); it proves
  the socket and packet layer can be reused.

## Command path and constraints

- **Bot as client.** `welt->set_tool(tool, player)` in network mode builds an `nwc_tool_t` from the
  caller's current checklist and sync step and sends it to the server. The server clones,
  validates and broadcasts it; clients execute it at the stamped sync step. This is the same path
  the GUI uses and is the recommended action path.
- **Server-hosted.** A server-local command must also go through `clone()` and the broadcast path.
  `karte_t::set_tool_api` appends a command to the server's local queue without broadcasting, and
  `nwc_tool_t::clone` clears `WFL_SCRIPT` for commands not sent by the server. A server-hosted API
  therefore needs a trusted flag or explicit player-unlock bits for the API connection and a
  broadcast path; do not reuse `set_tool_api` as-is for multiplayer. [UNVERIFIED]
- **Init and work.** `karte_t::set_tool` sends the tool's init command; in network mode the init
  position comes from the client cursor (`zeiger`), not from a parameter. `karte_t::call_work`
  (and its script-facing variant `call_work_api`) sends the work command with an explicit
  position. An API must drive both phases, set the cursor for tools that read it, or use tools
  whose `default_param` encodes the position (the depot tools do). [UNVERIFIED]
- **Command feedback.** `tool_t::work` returns an error string, but `nwc_tool_t::do_command` only
  logs it, and `player_t::tell_tool_result` is UI feedback. An API needs a request id and a result
  queue, or must infer the result from the next observation. [UNVERIFIED]
- **Composite actions.** Buying and assembling vehicles, creating a line with stops, and building
  a route span several tools. The API should expose typed composite intents over the existing
  tools, not raw tool strings. [UNVERIFIED]
- **Compatibility.** Reusing existing tool ids leaves the network protocol unchanged. Adding new
  tool ids or command types is a protocol change; all peers must run compatible builds.

## Read model

- The Squirrel API already exposes most entity fields: world, city, factory, halt, line, convoy,
  player, tiles, descriptors and schedules. The gap is aggregation, Extended-specific fields and
  a stable output format. Details: [scripting-and-tests](scripting-and-tests.md).
- A read-only Squirrel query VM inside the bot client is a practical first implementation. The
  VM executes; the read path does not need the broken tool-call suspension; query logic can live
  in `.nut` files and change without an engine rebuild. [UNVERIFIED]
- The VM can only be used read-only. Direct mutators on a client change local state without a
  network command and desynchronise the client. The bot's actions must go through `tool_t`.
  [UNVERIFIED]
- The VM runs on the main loop. Queries over thousands of entities can delay the client; bound
  them with opcode limits and run them at month boundaries or while paused. Map-wide tile scans
  are not practical in Squirrel and need C++ bounding-box queries. [UNVERIFIED]
- Line, halt and convoy objects have stable numeric ids in the API; cities and factories are
  addressed by coordinate. The API should expose a consistent identity scheme for every entity
  the bot can act on. [UNVERIFIED]

## MCP adapter boundary

MCP is the LLM-facing adapter, not the game protocol. A separate Python or TypeScript MCP server
should expose a small set of coarse tools (for example `observe_company`, `plan_route`,
`build_route`, `adjust_service`) and resources for slow-changing data, and translate them to the
game API. One tool per tile-level operation would be unusable. The engine side can use a simple
local line protocol; the adapter handles JSON. A player-run bot can use stdio MCP; a
server-hosted service needs remote HTTP with per-player authentication. [UNVERIFIED]

## Geography and new construction

Entity data alone supports maintenance and short local extensions, not strategic new
construction. An LLM cannot perform A* over a 4096x4096 map through tile queries; the number of
calls and the context size make it impractical.

The engine already contains the planners needed for new construction:

- `way_builder_t::calc_route`, `calc_route(start_list, end_list)`, `get_route`, `calc_costs`,
  `build`, `init_builder(waytype, way_desc, tunnel_desc, bridge_desc)` and `set_maximum`. The
  built-in AI uses this planner for road and rail (`player/ai.cc`, `player/ai_goods.cc`).
  `build()` mutates the world directly; on a client it must not be used, because the change
  would not be synchronised. Planning is read-only; construction goes through `tool_t`.
- `placefinder_t::find_place` and the AI helpers `ai_t::find_place`, `find_shore`,
  `find_harbour`, `create_simple_road_transport`, `ai_passenger_t::build_airport`,
  `cover_city_with_bus_route` and `walk_city`.
- `route_t::calc_route` for checking existing connectivity and travel time; the built-in AI uses
  it before building.

The Squirrel API exposes only `way_planner_x::set_build_types` and `is_allowed_step`, plus a
binary heap. It does not expose the planner itself. The single most useful addition is a
`calc_route` binding. [UNVERIFIED]

Proposed planning API: [UNVERIFIED]

- `plan_route(mode, start, end, way_desc, tunnel_desc, bridge_desc, flags)` returning the route
  polyline, length, construction cost, bridge and tunnel counts/lengths, maximum gradient,
  estimated travel time, and a feasibility or failure reason.
- `find_site(kind, near, size, constraints)` returning candidate station, depot, harbour or
  airport sites with cost and land use.
- `connectivity(a, b, mode)` returning whether a route exists, its length, travel time and
  capacity.
- `terrain_summary(bbox, cell_size)` or `render_map(bbox, scale, layers)` returning a coarse
  terrain/land-use grid or an image. MCP can return image content; a map with cities, industries,
  existing lines, proposed routes and a coordinate grid gives the AI global context that entity
  lists cannot.
- `demand_summary(bbox or nodes)` returning city population/growth, attractions, industry
  production/consumption, existing line capacity/load and unmet-demand proxies.

The resulting new-build workflow: [UNVERIFIED]

1. Query cities, industries, existing lines and stops; render or summarise the map.
2. Rank candidate pairs by distance, population, industry output and lack of connection.
3. Call `plan_route` for each candidate; compare cost, length, time, bridges and tunnels.
4. Score expected demand against construction and operating cost; choose a shortlist.
5. Call `find_site` at each end; re-plan from the chosen station sites.
6. Build way, bridge and tunnel segments, then stations, depots and signals.
7. Buy and assemble vehicles, create the line and schedule, assign convoys.
8. Observe load, revenue, cost and journey time; adjust or withdraw.

The AI makes the strategic choices; the engine computes geometry and cost. The AI does not draw
the alignment itself, which is the correct division of labour.

## What remains hard

- **Demand and profit forecasting.** The engine exposes city and factory statistics but no
  origin-destination matrix. New-line profitability remains uncertain; historical line P&L,
  waiting passengers and capacity utilisation are proxies. Expect experimentation and some
  loss-making lines. [UNVERIFIED]
- **Signalling and capacity.** `way_builder` plans alignment, not block signalling. Extended's
  signalling is essential for rail capacity, and the built-in AI does not place signals. Start
  with roads/buses or single-track one-train rail, or add a signalling policy to a composite
  rail-build intent. [UNVERIFIED]
- **Construction execution.** A planned route containing bridges and tunnels cannot be built by
  one existing tool: `tool_build_way_t` recomputes the route with `calc_route` rather than
  accepting a polyline, initialises the way builder without bridge or tunnel descriptors,
  `tool_build_tunnel_t` uses a straight route with a tunnel descriptor, and bridges are a
  separate tool. The bot must decompose the plan into way, bridge and tunnel segments and issue
  the corresponding tools, or a composite `build_route` command must be added. [UNVERIFIED]
- **RNG neutrality.** Road/rail planning in `way_builder_t` is RNG-free except the river bautyp;
  `vehicle_builder_t::vehicle_search` consumes `simrand`. A client-side planner or catalogue query
  must not consume the synced RNG. Expose a vehicle catalogue instead of calling `vehicle_search`,
  and test that a planning query leaves the RNG seed unchanged. [UNVERIFIED]
- **Planner cost.** `way_builder_t` has a bounded search (`maximum`, `route_t::MAX_STEP`), but
  repeated route searches can still stall the client. Limit candidate pairs, cache plans and run
  planning at month boundaries or while paused. [UNVERIFIED]

## Maintenance versus new build

- **Maintenance and optimisation** need only entity-level data: line/convoy P&L, capacity, load,
  waiting passengers, vehicle age and replacement, stop/depot data and company finances. This is
  the low-risk first product and where an AI can add value quickly.
- **Local extensions** (a stop on an existing route, a depot, a short siding) add site finding.
- **New corridors** add route planning and connectivity, and are feasible once the planners are
  exposed.
- **Strategic network design** adds demand data and a map summary; it is feasible but carries
  real economic risk.

## Staged route

1. Read-only observer: headless client plus a read-only query VM; verify RNG neutrality and
   behaviour across a network resync. [UNVERIFIED]
2. Command channel: one action (a short road) through `welt->set_tool`; verify with a second
   client that no desync occurs. [UNVERIFIED]
3. Test driver: scripted company actions and queries under `-fast-network-sync` and `-until`.
   [UNVERIFIED]
4. Player bot: MCP adapter, company authentication, composite actions and documentation.
   [UNVERIFIED]
5. Planning: `plan_route`, `find_site`, `connectivity`, map summary and the new-build workflow.
   [UNVERIFIED]
6. Server-hosted multi-tenant API: tokens, isolation, rate limits, audit and pause handling.
   [UNVERIFIED]

## Open questions

- Does the Squirrel `ai` host mode work in a headless client? It has never been wired.
- Which Extended accessors are missing from the Squirrel API? A field-level gap analysis against
  the GUI is needed.
- Is `way_builder_t::calc_route` deterministic and RNG-neutral for every bautyp used by the bot?
- Should a planned route be executed by decomposing it into existing tools, or by adding a
  composite `build_route` command? The latter changes the network protocol.
- What demand data can be exposed without building a new demand model?
- For a server-hosted API, is a trusted command flag or explicit unlock bits the correct
  authentication mechanism?
- MCP transport: local stdio for a player-run bot, or remote HTTP with per-player auth for a
  server-hosted service?
- What are the query and planner budgets that keep the client inside the server's pacing margin?

## Provenance

Code facts verified against ex-15 @ edd49a130: `network/network_cmd_ingame.*`,
`network/network.cc`, `simtool.*`, `simdepot.cc`, `simworld.cc`, `simmain.cc`,
`player/ai*.cc`, `bauer/wegbauer.*`, `dataobj/route.h`, `finder/placefinder.h`,
`script/api/api_pathfinding.cc`, `script/export_objs.cc`, `CMakeLists.txt`,
`scripts/run-smoke-tests.sh` and `scripts/run-join-sync-test.sh`. Deployment choices, planning
functions and staged work are design proposals, not implemented behaviour.
