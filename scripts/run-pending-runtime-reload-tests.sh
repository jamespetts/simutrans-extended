#!/usr/bin/env bash
#
# Simutrans-Extended pending-runtime reload fixture runner (Linux/POSIX).
#
# Purpose: exercise one forward Y division across a genuine save/reload. The
# staged scenario tests/pending-runtime/reload/scenario.nut runs the forward
# division case from tests/pending-runtime/harness.nut on a loopback server.
# Once the split-off portion has formed at target entry 21, the case prints
# "RUNTIME CASE reload READY" and holds before PASS. The runner then starts a
# second process as a client (-load net:127.0.0.1:<port>). The join makes the
# server save, transfer and reload its state (nwc_sync_t in
# network/network_cmd_ingame.cc), and the server script's resume_game rebuilds
# the fixture from the loaded world. The case must print
# "RUNTIME CASE reload RESTORED" and then "RUNTIME CASE reload PASS" when the
# existing split-off portion advances from target entry 21 to entry 22. The
# server's save written by the join (server<port>-network.sve) is then loaded
# by a fresh offline process, which must print RESTORED and PASS again.
#
# The scenario is NOT part of scripts/run-pending-runtime-tests.sh: that runner
# iterates a fixed case list plus tests/pending-runtime/cases/*, so this
# directory is not picked up by it.
#
# Oracles (all must hold for PASS):
#  1. the server prints READY, RESTORED and PASS, in that order, and the joined
#     client logs its own unpause (network_game_set_pause ... pause=0);
#  2. no desync markers: client "Lost synchronisation"/"checklist mismatch";
#     server "kicking client"/"disconnect client" due to a checklist mismatch;
#  3. the checklist pairs the client prints for every received nwc_check_t
#     contain no mismatch;
#  4. neither peer log holds a script error, RUNTIME CASE reload FAIL, or the
#     configured failure markers (FATAL ERROR, sanitizer reports);
#  5. both peers survive for OBSERVE_S seconds beyond the server's PASS;
#  6. the preserved join save loads offline and prints RESTORED then PASS, with
#     no failure marker.
#
# shunting_time_seconds is set to 600 in the user-directory simuconf.tab of
# each role's work directory so that the split-off portion stays at target
# entry 21 while the client starts. This is a real settings value, not an
# artificial state: settings_t::rdwr only reads shunting_time_seconds for
# Extended 15 and later saves (dataobj/settings.cc), the fixture map
# (tests/pending-runtime/empty-16x16.sve) is an Extended 14 save, and both the
# server reload and the offline load read the value from the ex-15 save written
# during the join.
#
# The server, the joining client and the offline reload all run the same
# headless binary. The binary MUST be a MULTI_THREAD build for the checklist
# comparison to be meaningful, as in scripts/run-join-sync-test.sh. All
# transient state lives under ai/temp/ (gitignored; AGENTS.md rule 9). The only
# file created outside that directory is the scenario staging directory inside
# the pakset; the EXIT trap removes it.

set -u
set -o pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(dirname -- "$script_dir")

EXE="${PENDING_RUNTIME_RELOAD_EXE:-}"
WORKDIR="${PENDING_RUNTIME_RELOAD_WORKDIR:-}"
PAKSET="${PENDING_RUNTIME_RELOAD_PAKSET:-pak128.Britain-Ex}"
OBJECTS="${PENDING_RUNTIME_RELOAD_OBJECTS:-pak128.Britain-Ex}"
SERVER_PORT="${PENDING_RUNTIME_RELOAD_PORT:-13357}"
SERVER_READY_TIMEOUT_S=900
CLIENT_TIMEOUT_S=420
OFFLINE_TIMEOUT_S=420
OBSERVE_S=20
SHUNTING_SECONDS=600
DEBUG_LEVEL=2
SAVE_FORMAT="bzip2"
MARKERS="FATAL ERROR|AddressSanitizer|ThreadSanitizer|runtime error|rendezvous released with work outstanding"
SCENARIO_NAME="pending-runtime-reload"
CLEAN=0

usage() {
	cat <<EOF
Usage: run-pending-runtime-reload-tests.sh [options]
  --exe PATH                 binary for the server, client and offline reload
                             (default: <repo>/simutrans-extended;
                             env PENDING_RUNTIME_RELOAD_EXE overrides)
  --workdir DIR              transient work directory
                             (default: <repo>/ai/temp/pending-runtime-reload)
  --pakset NAME              pakset dir name under <repo>/simutrans/
                             (default: $PAKSET)
  --objects NAME             -objects name / pakset link name in workdirs
                             (default: $OBJECTS)
  --server-port PORT         loopback server port (default: $SERVER_PORT)
  --server-ready-timeout S   watchdog for server setup up to RUNTIME CASE
                             reload READY (default: $SERVER_READY_TIMEOUT_S)
  --client-timeout S         watchdog for the whole client phase, from READY to
                             OBSERVE_S beyond the server's PASS (default:
                             $CLIENT_TIMEOUT_S)
  --offline-timeout S        watchdog for the fresh offline load of the join
                             save (default: $OFFLINE_TIMEOUT_S)
  --observe-seconds S        joined-play observation window after PASS
                             (default: $OBSERVE_S)
  --shunting-seconds N       shunting_time_seconds written to the user-directory
                             simuconf.tab of each role's work directory
                             (default: $SHUNTING_SECONDS)
  --debug-level N            engine -debug level, >= 2 for checklist pairs
                             (default: $DEBUG_LEVEL)
  --markers REGEX            ERE of failure markers to grep in the run logs
                             (default: "$MARKERS")
  --clean                    remove the workdir and staging directory, then exit

  PENDING_RUNTIME_RELOAD_EXE, _WORKDIR, _PAKSET, _OBJECTS and _PORT supply the
  defaults for --exe, --workdir, --pakset, --objects and --server-port.
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--exe) EXE="$2"; shift 2 ;;
		--workdir) WORKDIR="$2"; shift 2 ;;
		--pakset) PAKSET="$2"; shift 2 ;;
		--objects) OBJECTS="$2"; shift 2 ;;
		--server-port) SERVER_PORT="$2"; shift 2 ;;
		--server-ready-timeout) SERVER_READY_TIMEOUT_S="$2"; shift 2 ;;
		--client-timeout) CLIENT_TIMEOUT_S="$2"; shift 2 ;;
		--offline-timeout) OFFLINE_TIMEOUT_S="$2"; shift 2 ;;
		--observe-seconds) OBSERVE_S="$2"; shift 2 ;;
		--shunting-seconds) SHUNTING_SECONDS="$2"; shift 2 ;;
		--debug-level) DEBUG_LEVEL="$2"; shift 2 ;;
		--markers) MARKERS="$2"; shift 2 ;;
		--clean) CLEAN=1; shift ;;
		-h|--help) usage; exit 0 ;;
		*) echo "FAIL: unknown argument: $1"; usage; exit 1 ;;
	esac
done

[[ -n "$EXE" ]]     || EXE="$repo/simutrans-extended"
[[ -n "$WORKDIR" ]] || WORKDIR="$repo/ai/temp/pending-runtime-reload"

SRV_DIR="$WORKDIR/server"
CLI_DIR="$WORKDIR/client"
OFF_DIR="$WORKDIR/offline"
LOGS="$WORKDIR/logs"
STAGE="$repo/simutrans/$PAKSET/scenario/$SCENARIO_NAME"
PRESERVED_SAVE="$LOGS/server$SERVER_PORT-network.sve"

if [[ "$CLEAN" -eq 1 ]]; then
	[[ -d "$WORKDIR" ]] && rm -rf -- "$WORKDIR"
	# Remove a staging directory left by an interrupted run as well.
	[[ -d "$STAGE" ]] && rm -rf -- "$STAGE"
	echo "CLEAN: removed $WORKDIR and $STAGE"
	exit 0
fi

if [[ "$DEBUG_LEVEL" -lt 2 ]]; then
	echo "FAIL: --debug-level must be >= 2 (the checklist-pair oracle is logged only at -debug 2 or higher)"
	exit 1
fi

for p in "$EXE" "$repo/tests/pending-runtime/harness.nut" \
	"$repo/tests/pending-runtime/reload/scenario.nut" \
	"$repo/tests/pending-runtime/empty-16x16.sve"; do
	[[ -f "$p" ]] || { echo "FAIL: missing $p"; exit 1; }
done
pak_target="$repo/simutrans/$PAKSET"
[[ -d "$pak_target" ]] || { echo "FAIL: missing pakset $pak_target"; exit 1; }

server_pid=""
client_pid=""
offline_pid=""

# timeout(1) puts its child in its own process group; TERM/KILL the group so
# both the watchdog and the engine die.
kill_tree() {
	local pid="$1" i
	[[ -n "$pid" ]] || return 0
	kill -0 "$pid" 2>/dev/null || return 0
	kill -TERM -- "-$pid" 2>/dev/null || kill -TERM "$pid" 2>/dev/null || true
	for i in 1 2 3 4 5 6 7 8; do
		kill -0 "$pid" 2>/dev/null || return 0
		sleep 1
	done
	kill -KILL -- "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
	return 0
}

# The trap kills only processes this run started and removes only the staging
# directory this run created inside the pakset. The work directory is kept for
# logs and the preserved join save.
cleanup() {
	kill_tree "$offline_pid"
	kill_tree "$client_pid"
	kill_tree "$server_pid"
	[[ -n "$STAGE" && -d "$STAGE" ]] && rm -rf -- "$STAGE"
	return 0
}
trap cleanup EXIT INT TERM

fail_now() {
	echo "RESULT: FAIL"
	for f in "$@"; do echo "  - $f"; done
	exit 1
}

# True when any of the given files contains the extended regular expression.
has() {
	local pattern="$1" f
	shift
	for f in "$@"; do
		[[ -f "$f" ]] || continue
		if grep -qE -- "$pattern" "$f" 2>/dev/null; then
			return 0
		fi
	done
	return 1
}

# Echoes the first matching line found in any of the given files.
first_match_any() {
	local pattern="$1" f m
	shift
	for f in "$@"; do
		[[ -f "$f" ]] || continue
		m=$(grep -h -m 1 -E -- "$pattern" "$f" 2>/dev/null) || true
		if [[ -n "$m" ]]; then
			printf '%s' "$m"
			return 0
		fi
	done
	return 0
}

# Echoes the 1-based line number of the first match, or nothing.
marker_line() {
	grep -n -m 1 -E -- "$1" "$2" 2>/dev/null | cut -d: -f1 || true
}

log_markers() {
	local f m bad=""
	for f in "$@"; do
		[[ -f "$f" ]] || continue
		m=$(grep -h -m 3 -E "$MARKERS" -- "$f" 2>/dev/null)
		[[ -n "$m" ]] && bad+="$m"$'\n'
	done
	printf '%s' "$bad"
}

# Fresh role workdir: isolated config, save directory, pakset and resource
# links, and the sandbox simuconf values. The pakset target is linked so that
# scenario_path ("<objects>/scenario/<name>/"), which karte_t::load resolves
# relative to the user directory, reaches the staged scenario.
setup_role() {
	local dir="$1"
	rm -rf -- "$dir"
	mkdir -p "$dir/config" "$dir/save"
	ln -sfn -- "$pak_target" "$dir/$OBJECTS"
	ln -sfn -- "$repo/simutrans/font" "$dir/font"
	ln -sfn -- "$repo/simutrans/text" "$dir/text"
	ln -sfn -- "$repo/simutrans/themes" "$dir/themes"
	ln -sfn -- "$repo/simutrans/script" "$dir/script"
	{
		printf 'frames_per_second = 100\n'
		printf 'fast_forward_frames_per_second = 100\n'
		printf 'autosave = 0\n'
		printf 'saveformat = %s\n' "$SAVE_FORMAT"
		printf 'listen = 127.0.0.1\n'
		printf 'announce_server = 0\n'
		printf 'pause_server_no_clients = 0\n'
		printf 'server_save_game_on_quit = 1\n'
		printf 'reload_and_save_on_quit = 0\n'
		printf 'server_frames_per_step = 4\n'
		printf 'server_frames_between_checks = 1\n'
	} > "$dir/config/simuconf.tab"
	# Read after the pakset's own simuconf, which also sets
	# shunting_time_seconds, so this value wins (simmain.cc parses the program
	# directory config first and the user directory config last).
	printf 'shunting_time_seconds = %s\n' "$SHUNTING_SECONDS" > "$dir/simuconf.tab"
}

stage_scenario() {
	rm -rf -- "$STAGE"
	mkdir -p -- "$STAGE" || return 1
	cp -- "$repo/tests/pending-runtime/harness.nut" "$STAGE/" || return 1
	cp -- "$repo/tests/pending-runtime/empty-16x16.sve" "$STAGE/" || return 1
	cp -- "$repo/tests/pending-runtime/reload/scenario.nut" "$STAGE/scenario.nut" || return 1
}

mkdir -p "$LOGS"
stage_scenario || fail_now "could not stage the scenario in $STAGE"
setup_role "$SRV_DIR"
setup_role "$CLI_DIR"
setup_role "$OFF_DIR"

s_out="$SRV_DIR/server.out.log"; s_err="$SRV_DIR/server.err.log"
c_out="$CLI_DIR/client.out.log"; c_err="$CLI_DIR/client.err.log"
o_out="$OFF_DIR/offline.out.log"; o_err="$OFF_DIR/offline.err.log"
rm -f -- "$s_out" "$s_err" "$c_out" "$c_err" "$o_out" "$o_err"

export ASAN_OPTIONS="${ASAN_OPTIONS:-print_stacktrace=1 abort_on_error=1 detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1 abort_on_error=1}"
export TSAN_OPTIONS="${TSAN_OPTIONS:-print_stacktrace=1 second_deadlock_stack=1 history_size=7}"

failures=()

echo "Staged scenario: $STAGE"
echo "Starting server: $EXE (scenario $SCENARIO_NAME, port $SERVER_PORT, shunting ${SHUNTING_SECONDS}s)"
# The server must stay alive for setup, the whole client phase (the join
# save/transfer/reload and the portion's remaining shunting delay), the
# observation window and a margin.
server_watchdog=$((SERVER_READY_TIMEOUT_S + CLIENT_TIMEOUT_S + OBSERVE_S + 300))
timeout -k 5 "$server_watchdog" "$EXE" \
	-set_workdir "$SRV_DIR" -singleuser -objects "$OBJECTS" \
	-scenario "$SCENARIO_NAME" -server "$SERVER_PORT" \
	-debug "$DEBUG_LEVEL" -lang en -fps 100 -nosound -nomidi \
	> "$s_out" 2> "$s_err" &
server_pid=$!

srv_code=""
ready=0
ready_seconds=-1
for ((i=0; i<SERVER_READY_TIMEOUT_S; i++)); do
	sleep 1
	if has "Script: Error|</error>|Could not load scenario|Loading scenario script failed|No scenario map specified|Could not load scenario map" "$s_err" "$s_out"; then
		fail_now "server : scenario loading or script failure before READY -> $(first_match_any 'Could not load scenario|Loading scenario script failed|No scenario map specified|Could not load scenario map|Script: Error|</error>' "$s_err" "$s_out")"
	fi
	if has "RUNTIME CASE reload FAIL" "$s_err" "$s_out"; then
		fail_now "server : division case failed before READY -> $(first_match_any 'RUNTIME CASE reload FAIL' "$s_err" "$s_out")"
	fi
	if [[ -z "$srv_code" ]] && ! kill -0 "$server_pid" 2>/dev/null; then
		wait "$server_pid"; srv_code=$?
	fi
	if [[ -n "$srv_code" ]]; then
		bad=$(log_markers "$s_out" "$s_err")
		fail_now "server : exited with code $srv_code before RUNTIME CASE reload READY" \
			${bad:+"log markers -> $(printf '%s' "$bad" | tr '\n' '|' | sed 's/|$//')"}
	fi
	if has "RUNTIME CASE reload READY" "$s_err" "$s_out"; then
		ready=1
		ready_seconds=$i
		break
	fi
done
if [[ "$ready" -ne 1 ]]; then
	bad=$(log_markers "$s_out" "$s_err")
	fail_now "server : did not reach RUNTIME CASE reload READY within ${SERVER_READY_TIMEOUT_S}s" \
		${bad:+"log markers -> $(printf '%s' "$bad" | tr '\n' '|' | sed 's/|$//')"}
fi
echo "Server reached RUNTIME CASE reload READY after ~${ready_seconds}s; starting client"

echo "Starting client: $EXE (net:127.0.0.1:$SERVER_PORT)"
timeout -k 5 "$CLIENT_TIMEOUT_S" "$EXE" \
	-set_workdir "$CLI_DIR" -singleuser -objects "$OBJECTS" \
	-load "net:127.0.0.1:$SERVER_PORT" \
	-debug "$DEBUG_LEVEL" -lang en -fps 100 -nosound -nomidi \
	> "$c_out" 2> "$c_err" &
client_pid=$!

desynced=0
joined_seconds=-1
restored_seen=0
pass_seconds=-1
cli_code=""
cli_started=$SECONDS
while (( SECONDS - cli_started < CLIENT_TIMEOUT_S )); do
	sleep 2
	if has "Script: Error|</error>|Could not load scenario|Loading scenario script failed|No scenario map specified|Could not load scenario map" "$s_err" "$s_out"; then
		fail_now "server : scenario loading or script failure after starting the client"
	fi
	if has "Lost synchronisation|checklist mismatch|disconnect client|kicking client" \
		"$c_err" "$c_out" "$s_err" "$s_out"; then
		desynced=1
		sleep 5
		break
	fi
	if has "RUNTIME CASE reload FAIL" "$s_err" "$s_out"; then
		break
	fi
	if [[ -z "$cli_code" ]] && ! kill -0 "$client_pid" 2>/dev/null; then
		wait "$client_pid"; cli_code=$?
		echo "[client] exited (code $cli_code)"
		break
	fi
	if [[ -z "$srv_code" ]] && ! kill -0 "$server_pid" 2>/dev/null; then
		wait "$server_pid"; srv_code=$?
		echo "[server] exited during observation (code $srv_code)"
		break
	fi
	if [[ "$joined_seconds" -lt 0 ]] && has "network_game_set_pause.*pause=0" "$c_out" "$c_err"; then
		joined_seconds=$((SECONDS - cli_started))
		echo "Client joined and unpaused after ~${joined_seconds}s"
	fi
	if has "RUNTIME CASE reload RESTORED" "$s_err" "$s_out"; then
		restored_seen=1
	fi
	if [[ "$pass_seconds" -lt 0 ]] && has "RUNTIME CASE reload PASS" "$s_err" "$s_out"; then
		pass_seconds=$((SECONDS - cli_started))
		echo "Server printed RUNTIME CASE reload PASS after ~${pass_seconds}s; observing for ${OBSERVE_S}s beyond PASS"
	fi
	if [[ "$pass_seconds" -ge 0 ]] && (( SECONDS - cli_started - pass_seconds >= OBSERVE_S )); then
		break
	fi
done

# Capture exit codes while the jobs are still unreaped, then stop both peers.
if [[ -z "$cli_code" ]] && ! kill -0 "$client_pid" 2>/dev/null; then
	wait "$client_pid"; cli_code=$?
fi
if [[ -z "$srv_code" ]] && ! kill -0 "$server_pid" 2>/dev/null; then
	wait "$server_pid"; srv_code=$?
fi
kill_tree "$client_pid"
kill_tree "$server_pid"
client_pid=""
server_pid=""

# Preserve the save written by the join before analysing the run, so that it
# survives both verdicts.
if [[ -f "$SRV_DIR/server$SERVER_PORT-network.sve" ]]; then
	cp -- "$SRV_DIR/server$SERVER_PORT-network.sve" "$PRESERVED_SAVE" || true
fi

echo "---- analysis: server and client ----"

server_fail_msg=$(first_match_any "RUNTIME CASE reload FAIL" "$s_err" "$s_out")
if [[ -n "$server_fail_msg" ]]; then
	failures+=("server : $server_fail_msg")
fi
if [[ "$desynced" -eq 1 ]]; then
	failures+=("join-sync : desync markers detected (client 'Lost synchronisation'/'checklist mismatch' or server kick)")
fi
if [[ "$joined_seconds" -lt 0 && "$desynced" -eq 0 ]]; then
	failures+=("join-sync : client never joined and unpaused within ${CLIENT_TIMEOUT_S}s (no 'network_game_set_pause ... pause=0' in the client log)")
fi
if [[ "$restored_seen" -ne 1 ]]; then
	failures+=("server : no RUNTIME CASE reload RESTORED after the join (the save was not reloaded or the divided state was not re-asserted)")
fi
if [[ "$pass_seconds" -lt 0 ]]; then
	failures+=("server : no RUNTIME CASE reload PASS (the restored split-off portion did not advance from target entry 21 to entry 22)")
fi
ready_line=$(marker_line "RUNTIME CASE reload READY" "$s_err")
restored_line=$(marker_line "RUNTIME CASE reload RESTORED" "$s_err")
pass_line=$(marker_line "RUNTIME CASE reload PASS" "$s_err")
if [[ -n "$restored_line" && -n "$pass_line" ]] && (( restored_line > pass_line )); then
	failures+=("server : RUNTIME CASE reload PASS appears before RUNTIME CASE reload RESTORED")
fi
if [[ -n "$ready_line" && -n "$restored_line" ]] && (( ready_line > restored_line )); then
	failures+=("server : RUNTIME CASE reload RESTORED appears before RUNTIME CASE reload READY")
fi
if [[ -n "$cli_code" ]]; then
	if [[ "$cli_code" -eq 124 || "$cli_code" -eq 137 ]]; then
		failures+=("client : TIMEOUT after ${CLIENT_TIMEOUT_S}s - killed")
	else
		failures+=("client : exited on its own (code $cli_code) before the runner stopped it")
	fi
fi
if [[ -n "$srv_code" ]]; then
	if [[ "$srv_code" -eq 124 || "$srv_code" -eq 137 ]]; then
		failures+=("server : TIMEOUT after ${server_watchdog}s - killed")
	else
		failures+=("server : exited on its own (code $srv_code) before the runner stopped it")
	fi
fi
if [[ "$pass_seconds" -ge 0 && -z "$cli_code" && -z "$srv_code" && "$desynced" -eq 0 ]] \
	&& (( SECONDS - cli_started - pass_seconds < OBSERVE_S )); then
	failures+=("join-sync : observation ended before the requested ${OBSERVE_S}s beyond PASS")
fi
if has "Script: Error|</error>|Could not load scenario|Loading scenario script failed|No scenario map specified|Could not load scenario map" "$s_err" "$s_out" "$c_err" "$c_out"; then
	failures+=("logs : scenario loading or script error -> $(first_match_any 'Could not load scenario|Loading scenario script failed|No scenario map specified|Could not load scenario map|Script: Error|</error>' "$s_err" "$s_out" "$c_err" "$c_out")")
fi
bad=$(log_markers "$s_out" "$s_err" "$c_out" "$c_err")
if [[ -n "$bad" ]]; then
	failures+=("logs : markers -> $(printf '%s' "$bad" | tr '\n' '|' | sed 's/|$//')")
fi

# Checklist pairs the client prints for every received nwc_check_t. Both
# blocks are captured from the "ss=" field text onwards (the differing entity
# prefixes are stripped) so that identical states compare equal; a pair only
# counts when both halves look like a checklist (contain "sums=").
read -r pairs_n mis_n first_mis < <(awk '
	match($0, /sync_step=[0-9]+/) {
		ss = substr($0, RSTART + 10, RLENGTH - 10)
		p = index($0, "server=[")
		srv = (p > 0 ? substr($0, p + 8) : "") "\n"
		cli = ""; phase = 1; next
	}
	phase == 1 { srv = srv $0 "\n"; if (index($0, "]") > 0) phase = 2; next }
	phase == 2 {
		line = $0
		p = index(line, "client=[")
		if (p > 0) line = substr(line, p + 8)
		cli = cli line "\n"
		if (index($0, "]") > 0) {
			if (index(srv, "sums=") > 0 && index(cli, "sums=") > 0) {
				pairs++
				if (srv != cli) { mis++; if (first == "") first = ss }
			}
			phase = 0
		}
		next
	}
	END { printf "%d %d %s\n", pairs+0, mis+0, (first == "" ? "-" : first) }
' "$c_err" 2>/dev/null || echo "0 0 -")
echo "checklist pairs: $pairs_n, mismatches: $mis_n"
if [[ "$mis_n" -gt 0 ]]; then
	failures+=("join-sync : $mis_n of $pairs_n checklist pairs mismatch (first at sync_step $first_mis)")
fi

if [[ ${#failures[@]} -gt 0 ]]; then
	echo "RESULT: FAIL"
	for f in "${failures[@]}"; do echo "  - $f"; done
	echo "Logs: $LOGS (client/server logs)"
	[[ -f "$PRESERVED_SAVE" ]] && echo "Preserved join save: $PRESERVED_SAVE"
	exit 1
fi

echo "SERVER: PASS (joined after ~${joined_seconds}s; RESTORED and PASS; observed ${OBSERVE_S}s beyond PASS)"

# ---- fresh offline load of the join save ----
[[ -f "$PRESERVED_SAVE" ]] || fail_now "offline : the preserved join save $PRESERVED_SAVE does not exist"

setup_role "$OFF_DIR"
cp -- "$PRESERVED_SAVE" "$OFF_DIR/save/server$SERVER_PORT-network.sve" || fail_now "offline : could not copy the join save into the offline save directory"

echo "Starting offline reload: $EXE (-load server$SERVER_PORT-network.sve)"
timeout -k 5 "$OFFLINE_TIMEOUT_S" "$EXE" \
	-set_workdir "$OFF_DIR" -singleuser -objects "$OBJECTS" \
	-load "server$SERVER_PORT-network.sve" \
	-debug "$DEBUG_LEVEL" -lang en -fps 100 -nosound -nomidi \
	> "$o_out" 2> "$o_err" &
offline_pid=$!

off_code=""
offline_restored=0
offline_pass=0
offline_pass_seconds=-1
off_started=$SECONDS
while (( SECONDS - off_started < OFFLINE_TIMEOUT_S )); do
	sleep 2
	if has "Script: Error|</error>|Could not load scenario|Loading scenario script failed|No scenario map specified|Could not load scenario map" "$o_err" "$o_out"; then
		break
	fi
	if has "RUNTIME CASE reload FAIL" "$o_err" "$o_out"; then
		break
	fi
	if [[ -z "$off_code" ]] && ! kill -0 "$offline_pid" 2>/dev/null; then
		wait "$offline_pid"; off_code=$?
		echo "[offline] exited (code $off_code)"
		break
	fi
	if has "RUNTIME CASE reload RESTORED" "$o_err" "$o_out"; then
		offline_restored=1
	fi
	if has "RUNTIME CASE reload PASS" "$o_err" "$o_out"; then
		offline_pass=1
		offline_pass_seconds=$((SECONDS - off_started))
		break
	fi
done

if [[ -z "$off_code" ]] && ! kill -0 "$offline_pid" 2>/dev/null; then
	wait "$offline_pid"; off_code=$?
fi
kill_tree "$offline_pid"
offline_pid=""

echo "---- analysis: offline reload ----"

off_failures=()
offline_fail_msg=$(first_match_any "RUNTIME CASE reload FAIL" "$o_err" "$o_out")
if [[ -n "$offline_fail_msg" ]]; then
	off_failures+=("offline : $offline_fail_msg")
fi
if [[ "$offline_restored" -ne 1 ]]; then
	off_failures+=("offline : no RUNTIME CASE reload RESTORED after loading the join save")
fi
if [[ "$offline_pass" -ne 1 ]]; then
	off_failures+=("offline : no RUNTIME CASE reload PASS (the restored split-off portion did not advance from target entry 21 to entry 22)")
fi
off_restored_line=$(marker_line "RUNTIME CASE reload RESTORED" "$o_err")
off_pass_line=$(marker_line "RUNTIME CASE reload PASS" "$o_err")
if [[ -n "$off_restored_line" && -n "$off_pass_line" ]] && (( off_restored_line > off_pass_line )); then
	off_failures+=("offline : RUNTIME CASE reload PASS appears before RUNTIME CASE reload RESTORED")
fi
if [[ -n "$off_code" ]]; then
	if [[ "$off_code" -eq 124 || "$off_code" -eq 137 ]]; then
		off_failures+=("offline : TIMEOUT after ${OFFLINE_TIMEOUT_S}s - killed")
	else
		off_failures+=("offline : exited on its own (code $off_code) before the runner stopped it")
	fi
fi
if has "Script: Error|</error>|Could not load scenario|Loading scenario script failed|No scenario map specified|Could not load scenario map" "$o_err" "$o_out"; then
	off_failures+=("offline : scenario loading or script error -> $(first_match_any 'Could not load scenario|Loading scenario script failed|No scenario map specified|Could not load scenario map|Script: Error|</error>' "$o_err" "$o_out")")
fi
bad=$(log_markers "$o_out" "$o_err")
if [[ -n "$bad" ]]; then
	off_failures+=("offline : markers -> $(printf '%s' "$bad" | tr '\n' '|' | sed 's/|$//')")
fi

if [[ ${#off_failures[@]} -gt 0 ]]; then
	echo "RESULT: FAIL"
	for f in "${off_failures[@]}"; do echo "  - $f"; done
	echo "Logs: $LOGS (offline log)"
	echo "Preserved join save: $PRESERVED_SAVE"
	exit 1
fi

echo "OFFLINE: PASS (RESTORED and PASS after ~${offline_pass_seconds}s)"
echo "RESULT: PASS (join save $PRESERVED_SAVE restored and continued normally offline)"
exit 0
