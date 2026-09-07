---
status: reviewed
verified: master @ 78a4bb3b9
---
# Network & multiplayer architecture

**Covers:** network/ (socket layer, packets, command framework, file transfer, pakset comparison), utils/checklist.* (mechanism), nettools/, the network functions of `karte_t` in simworld.cc (`process_network_commands`, pacing in `interactive`, `network_disconnect`, `switch_server`), announce infrastructure in simversion.h.

Read when CHANGING the networking system itself (change-restricted, AGENTS.md rule 5). The day-to-day rules for keeping simulation code in sync live in [sync-and-determinism](sync-and-determinism.md); version negotiation → [savegame-versioning](savegame-versioning.md).

## Synchronisation model

Deterministic lockstep: only commands travel during play; full game state travels only as a complete savegame at join/forced resync. Every world command carries `sync_step` (the frame at which all peers execute it) and `map_counter` (identifies which world load it belongs to; commands from another load are dropped or disconnect the peer). All rules this imposes on simulation code → [sync-and-determinism](sync-and-determinism.md). [CODE master @ 78a4bb3b9]

## Transport & packet layer

- network/network.cc — socket layer (header: "borrowed from OpenTTD"). TCP only; winsock/BSD abstraction; default port 13353; client connect `network_open_address` (non-blocking, 2 s timeout); server `network_init_server(port, env_t::listen)` — one listening socket per address in `env_t::listen` (default dual-stack "::" + "0.0.0.0", `IPV6_V6ONLY`); `TCP_NODELAY` on client sockets; ban list (`address_list_t blacklist`) checked at `accept()`, in-memory only. Polling/draining: `network_check_activity`, `network_process_send_queues`; broadcast `network_send_all`, client→server `network_send_server` [CODE].
- network_packet.h/cc — `packet_t : memory_rw_t`: fixed `MAX_PACKET_LEN` 8192; 6-byte header (size, version, command id); `NETWORK_VERSION` (=1, network.h) checked on receive; incremental send/recv with per-socket partial-packet state [CODE].
- memory_rw.h/cc — `memory_rw_t`: endian-safe (little-endian on the wire) read/write cursor; overflow flag fails the packet → connection dropped. (There is no class named `network_memory_t`.) [CODE]
- network_socket_list.h/cc — `socket_info_t : connection_info_t`: per-connection state (`inactive/server/connected/playing/has_left/admin`), socket, `player_unlocked` bitmask, send queue; `socket_list_t`: static list; client ids are indices, id 0 is always the server's own entry; `send_all` copies a packet into every eligible queue (optional player_nr filter) [CODE].
- network_address.h/cc — `net_address_t` (IPv4 + mask) and `address_list_t` bans: IPv4-only even though sockets support IPv6 (the accept path stores only sockaddr_in) — see open questions [CODE].
- checksum.h/cc — `checksum_t`: SHA-1 based, used for pakset/descriptor checksums (NOT the in-game checklist); `pakset_info.h/cc` collects per-descriptor checksums from every descriptor reader; `pwd_hash.h/cc`: SHA-1 company-password hashes [CODE].

## Command framework (network_cmd*.h/cc)

- `network_command_t` (network_cmd.cc): base; owns a `packet_t`; ids `NWC_INVALID/GAMEINFO/NICK/CHAT/JOIN/SYNC/GAME/READY/TOOL/CHECK/PAKSETINFO/SERVICE/AUTH_PLAYER/CHG_PLAYER/SCENARIO/SCENARIO_RULES/STEP/ROUTESEARCH`; factory `read_from_packet()`; virtual `execute(karte_t*)` [CODE].
- `network_world_command_t`: base for simulation-frame-bound commands; fields `sync_step` + `map_counter`; `execute()` rejects past-dated commands (disconnect unless `ignore_old_events()`) and appends to the world's `command_queue` (`karte_t::command_queue_append`, insertion-sorted by sync_step); virtual `do_command()` applies it [CODE].
- `network_broadcast_world_command_t`: server-side pattern — server validates via virtual `clone()`, re-stamps `sync_step = welt->get_sync_steps()+1`, sets `exec=true`, broadcasts to ALL peers including itself; clients' `exec` packets are refused. On the server, own broadcasts are re-queued via `sent_by_server()` so server and clients share one execution path [CODE].
- Command inventory [CODE]:
  - `nwc_tool_t` (NWC_TOOL) — the workhorse: serialised `tool_t` invocation (tool_id, pos, init vs work, flags, player_nr, ≤256 bytes custom_data). Carries the sender's `last_sync_step` + `last_checklist` (desync guard). `clone()` enforces player unlock + scenario rules (violations become `TOOL_ERROR_MESSAGE` back to the initiator); `do_command()` re-instantiates the tool and calls init/work/exit; `WFL_LOCAL` only for the initiator. Execution model → [sync-and-determinism](sync-and-determinism.md).
  - `nwc_chg_player_t` (NWC_CHG_PLAYER) — company create/delete/AI/freeplay via `karte_t::change_player_tool`; statics record which connection founded/plays each company (admin reports).
  - `nwc_sync_t` (NWC_SYNC) — full save/reload resync at join or admin force-sync; `do_command()` runs on every peer (see lifecycle).
  - `nwc_check_t` (NWC_CHECK) — server→clients: server's checklist for the previous sync step; comparison happens in `karte_t::do_network_world_command`.
  - `nwc_step_t` (NWC_STEP) — per-frame broadcast of the server's sync step for client pacing (no checklist).
  - `nwc_ready_t` (NWC_READY) — join handshake completion; carries sync_step, map_counter and a checklist; server validates and unpause-replies.
  - `nwc_join_t` (NWC_JOIN, : nwc_nick_t) — join request/answer; static `pending_join_client` serialises concurrent joins.
  - `nwc_game_t` (NWC_GAME) — announces savegame byte length preceding a raw file transfer (server→client only).
  - `nwc_gameinfo_t` (NWC_GAMEINFO) — server writes `serverinfo.sve` and sends a `gameinfo_t` summary + raw file (server-list dialog).
  - `nwc_nick_t` / `nwc_chat_t` — nicknames (uniqueness enforced; welcome/farewell messages; `__ChatLog__` CSV logging) and chat incl. whisper (relayed only to clients with that player unlocked).
  - `nwc_auth_player_t` (NWC_AUTH_PLAYER) — company password auth; `pwd_hash_t` to the server; `player_unlocked` bitmask back; `init_player_lock_server()` seeds server state.
  - `nwc_routesearch_t` (NWC_ROUTESEARCH) — Extended-only (@author Knightly): synchronises `path_explorer_t::limit_set_t` iteration limits; clients report local limits, server computes and broadcasts the minimum set (batched: after 8 updates or a quiet period); joining clients receive the active set.
  - `nwc_scenario_t` / `nwc_scenario_rules_t` — scenarios run SERVER-ONLY (dataobj/scenario.h comment): clients request script results (`dynamic_string::fetch_result`), server answers; forbidden tool/area rules are broadcast (server-only accepted).
  - `nwc_service_t` (NWC_SERVICE) — admin protocol: login (against `env_t::server_admin_pw`), announce, client list, kick/ban (IP blacklist), admin chat, shutdown, force-sync, company list/info/unlock/remove/lock. Requires `admin` socket state except login/announce.
  - `nwc_pakset_info_t` (NWC_PAKSETINFO) + `network_compare_pakset_with_server()` — client-driven one-descriptor-at-a-time pakset diff (SHA-1 checksums, cap `MAX_WRONG_PAKS`), rendered as a help window; server serves one client at a time.

## Connection lifecycle

- Server start: `-server [port]` / `-easyserver [port]` (easyserver adds UPnP port forwarding via miniupnpc under USE_UPNP + external-IP query) → `network_init_server`; or convert a running game via `karte_t::switch_server(true, ...)` (also `init_player_lock_server`) [CODE].
- Client join: `karte_t::load("net:<host[:port]>")` → `network_connect()` (network_file_transfer.cc). Sequence [CODE]:
  1. send `nwc_join_t` (nickname); await answer (client_id, sanitised nickname).
  2. await `nwc_sync_t` → new `map_counter`.
  3. await `nwc_game_t` (byte length), then `network_receive_file()` streams the savegame to `client<i>-network.sve`.
  4. await `nwc_routesearch_t` → apply the server's active path-explorer limit set.
  5. load the received save; socket state → `playing`.
- `nwc_sync_t::do_command()` runs on every peer at the stamped sync step [CODE]: each client saves+reloads its own state and sends `nwc_ready_t` (with its checklist); the server strips password hashes from the transferred save (keeping them in `server%d-pwdhash.sve`, loadsave filetype "hashes"), saves+sends+reloads `server%d-network.sve`, transmits the active limit set, then on the joiner's ready: compares checklists (mismatch → remove client), unpause-replies, sends the unlocked-players mask and a welcome message. MOTD (`env_t::server_motd_filename`) is embedded in the transferred save.
- Version negotiation [CODE]: packet level — `NETWORK_VERSION` in every header. Game level — implicit via transferred files: server writes saves/gameinfo with `SERVER_SAVEGAME_VER_NR` + `EXTENDED_VER_NR` + `EXTENDED_REVISION_NR` (simversion.h); client refuses future base version (`FILE_STATUS_ERR_FUTURE_VERSION`) and too-old Extended version (`is_version_ex_less`) → [savegame-versioning](savegame-versioning.md). Pakset level — optional SHA-1 comparison (above); the server list pre-filters by revision and pakset name.

## In-game frame sync & pacing

- Network mode forces `FIX_RATIO` stepping: `sync_steps` counts frames; a simulation `step()` runs every `frames_per_step` frames (`sync_steps = steps * frames_per_step + network_frame_count`); frame time from clamped fps (`karte_t::reset_timer`) [CODE].
- Server broadcasts every frame: normally `nwc_step_t`; every `env_t::server_sync_steps_between_checks` sync steps (simuconf `server_frames_between_checks`) or when lagging, `nwc_check_t` instead [CODE].
- Client pacing (`karte_t::process_network_commands`) [CODE]: from the received server sync step it computes a target offset (`settings.get_server_frames_ahead()` + `env_t::additional_client_frames_behind`) and applies gentle speed correction via `ms_difference` (clamped in `interactive` to roughly 83%–500% of normal rate); hard limit: `sync_steps_barrier` — a client at the barrier only displays (`sync_step(0,false,true)`) and cannot overtake the server.
- Pause machinery: `karte_t::network_game_set_pause(pause, syncsteps)` re-derives step/frame counters and, on client unpause, grants the server a head start. `env_t::pause_server_no_clients` pauses an empty server; `env_t::server_runs_background_tasks_when_paused` (`-run-background-tasks`) lets it keep running path explorer/private-car routing via `pause_step()` [CODE].
- Command processing order per iteration of `karte_t::interactive`: `process_network_commands` (receive → execute/queue → drain due commands via `karte_t::do_network_world_command`) BEFORE simulation advance [CODE].

## Checklist mechanism

- `checklist_t` (utils/checklist.h): hash, random_seed, halt/line/convoy quickstone counters, ss/st/nfc, `rand[32]`, `debug_sum[10]`; `rdwr(memory_rw_t*)`; `operator==` compares ALL fields exactly; `print()` dumps groups labelled `ssr`/`str`/`exr`/`sums`. WHAT feeds it (the coverage contract for simulation programmers) → [sync-and-determinism](sync-and-determinism.md) [CODE].
- Created every sync step in `interactive()`; history ring `LCHKLST(x)` of `LAST_CHECKLISTS_COUNT`=64 (simworld.h); cleared on network loads (`clear_all_checklists`) [CODE].
- Comparison points [CODE]:
  - `nwc_check_t` in `karte_t::do_network_world_command`: prints both checklists; mismatch → `karte_t::network_disconnect()` ("Lost synchronisation with server" news + pause); `log_t::fatal` in heavy mode 2.
  - Every `nwc_tool_t`: server compares the initiator's attached checklist on receipt (mismatch → kick client; future-dated → skipped "client was too fast"); each peer re-compares at execution (clients disconnect, servers only skip).
  - `nwc_ready_t` at join: server compares and removes the client on mismatch.
- Heavy modes (`-heavy 0..2`, `env_t::network_heavy_mode`) [CODE]: 1 replaces the checklist with a per-frame whole-game-state adler32 (`karte_t::get_gamestate_hash` streams `rdwr_gamestate` through `stream_loadsave_t`/`adler32_stream_t`); 2 additionally writes rotating dumps `save/heavy/heavy-{server|client}-<sync_steps>.sve` (last 10, `karte_t::heavy_rotate_saves`).
- Note: the loadsave filetype "hashes" (`server%d-pwdhash.sve`) holds player PASSWORD hashes preserved across the sync reload — not a desync artefact [CODE].

## Administration (nettools/)

- nettools/nettool.cc — standalone console admin client ("Network server control tool"), built with `-DNETTOOL` (which also strips game-dependent code from the shared network/ sources via `#ifndef NETTOOL`); MSVC `Nettool-Extended.sln`. Speaks `NWC_SERVICE` only (its own `read_from_packet` stub). Commands: announce, clients, kick-client, ban-client, blacklist, ban-ip, unban-ip, say, shutdown, force-sync, companies, info-company, unlock/remove/lock-company; auth via `SRVC_LOGIN_ADMIN` against `env_t::server_admin_pw` [CODE].

## Announcement & server list

- `karte_t::announce_server()` HTTP-POSTs (`network_http_post`) to `ANNOUNCE_SERVER` = list.extended.simutrans.org:8080, path `ANNOUNCE_URL` (simversion.h): HELLO on start/load, HEARTBEAT every `env_t::server_announce_interval` and on client-count change, GOODBYE on exit/switch_server. Payload includes rev/ver (REVISION parsed as hex), pakset name, map/client/company stats. Easy-server mode re-queries the external IP (`QUERY_ADDR_IP`/`QUERY_ADDR_URL`, simversion.h) and re-announces on change. Server list: CSV via `network_http_get` (gui/server_frame.cc) [CODE].
- The server list must not be refreshed while already in a network game (code comment: "otherwise desync to current game may happen") [CODE comment].

## Threading

Network I/O is entirely single-threaded on the main loop: receive/send via `select` inside `karte_t::process_network_commands`; blocking sends only for handshakes/file transfer/HTTP; no thread primitives anywhere in network/. Simulation threading and its determinism constraints → [sync-and-determinism](sync-and-determinism.md), [threading](threading.md) [CODE].

## Known problems & history

- Dead code [CODE]: `nwc_routesearch_t::remove_client_entry()` and `reset()` have no callers — departed clients' limit entries are never pruned from `client_entries`; `karte_t::interactive`'s local `hashes_ok` vector is unused.
- Local-only root artefacts ("2019 server signal desync logs.txt", "Commands for debugging server.txt") document historical server desync debugging [CODE]; the desync leads derived from them and from forum reports live in [known-bugs](known-bugs.md) (canonical home for unverified leads).
- Historical test switches for passenger-generation/network desync investigation sit commented-out in simworld.h (e.g. `FORBID_PARALLELL_PASSENGER_GENERATION_IN_NETWORK_MODE`, 2017) [CODE].

## Provenance

Verified against master @ 78a4bb3b9. network/, utils/checklist.* are materially identical on ex-15 (one whitespace-only difference), so this doc applies to both branches [CODE]. Extended-era additions on top of the Standard network base: `nwc_routesearch_t` (Extended path explorer), heavy-mode debugging — written by a contributor to aid desync debugging [RECOLLECTION:2026-09-07], company-administration service commands, Extended checklist feeders, Extended version negotiation and listing server; coarse provenance only, per conventions.

Walkthrough 2026-09-07: the user confirmed the default port, the forced FIX_RATIO stepping in network mode and the announcement URL; the user had little involvement in the lower-level network code and could neither confirm nor disconfirm the remaining content, which therefore rests on agent code verification [CODE].

## Open questions

- Are the unused `nwc_routesearch_t::remove_client_entry`/`reset` deliberate (stale entries tolerated) or an oversight/bug? (Client limit entries accumulate for departed clients; user could not confirm, asked 2026-09-07.)
- Is the IPv4-only ban list (despite IPv6 sockets) a known limitation or a bug? (User could not confirm, asked 2026-09-07.)
- Do desync-debugging tools or scripts exist OUTSIDE the repo (e.g. Bridgewater-Brunel server infrastructure)? The repo holds only the two local-only root artefacts [CODE]. (Asked 2026-09-07; the question as then phrased — "the BB server's desync tooling" — was not clear to the user.)
- Should `nwc_routesearch_t` batch thresholds / the pacing constants be documented as tunables somewhere canonical (simuconf keys vs env_t)? (Currently discoverable only from dataobj/settings.cc + environment.cc; user unsure, asked 2026-09-07.)
