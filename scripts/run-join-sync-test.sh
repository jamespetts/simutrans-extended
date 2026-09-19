#!/usr/bin/env bash
#
# Simutrans-Extended loopback network-join synchronisation test (Linux).
#
# Purpose: CI regression gate for the live-server join desync fixed by
# promote_orphaned_linked_slots (boden/wege/weg.cc, called from karte_t::save in
# simworld.cc): orphaned shared private-car route lists were not serialised, so a
# client joining a running server received different private-car route data and
# lost synchronisation within seconds of unpause. The test replays the proven
# local repro (ai/temp/desync-repro/run-repro.ps1) against the real fixture
# bb6-apr-2010.sve: a headless server loads the fixture and free-runs
# --pre-join-steps steps (so private-car recording and duplicate-list merging
# have produced orphaned shared lists before the join save), then a second
# process joins as a client via -load net:127.0.0.1:<port> and is observed for
# --observe-seconds of joined play.
#
# Oracles (all must hold for PASS):
#  1. no desync markers: client "Lost synchronisation"/"checklist mismatch";
#     server "kicking client"/"disconnect client" due to checklist mismatch;
#  2. the per-step semantic private-car route hash sequences agree on all common
#     steps ("Private car route hash step N: <hex>", logged at -debug 2 or
#     higher). The server log also holds pre-join steps, and the join pause
#     re-derives the step counter (karte_t::network_game_set_pause), so a step
#     can be logged twice on the server side: per step, the LAST occurrence
#     wins (the post-join entry is the comparable one);
#  3. the server=[...]/client=[...] checklist pairs the client prints for every
#     received nwc_check_t contain no mismatch;
#  4. no generic failure markers (FATAL ERROR / sanitizer reports) in any log.
#
# Both roles run the SAME headless binary (posix backend, COLOUR_DEPTH=0), which
# is the default: a headless build can act as network client [CODE master @
# 075d540f3]:
#  - simmain.cc handles "-load net:<host[:port]>" like any other loadgame and it
#    ends in karte_t::load("net:...") (simworld.cc), which calls
#    network_connect() (network/network_file_transfer.cc) - pure socket/file IO;
#    its only display call is loadingscreen_t, a no-op under COLOUR_DEPTH=0
#    (display/simgraph0.cc stubs), and the NETTOOL builds compile the same join
#    transfer with no display at all;
#  - after the download the client enters the same karte_t::interactive() loop
#    that the headless server already runs in the smoke harness; the client
#    network branches there are pure simulation + socket processing;
#  - desync detection output is dbg->warning log lines, backend-independent.
# One binary for both roles also guarantees identical REVISION and identical
# thread counts between the peers (the checklist compares env_t::num_threads in
# debug_sums[4]), so no SDL_VIDEODRIVER=dummy workaround is needed.
#
# The binary MUST be a MULTI_THREAD build (the desync machinery under test is
# multi-threaded): on Linux the GNU-make autoconf route sets MULTI_THREAD = 1
# (configure.ac pthread check); CMake's SIMUTRANS_MULTI_THREAD defaults to OFF.
# A DEBUG build is not required (-until is unused; -debug 2 is a runtime level).
#
# Requires a pakset matching the branch's Extended object version (same rule as
# run-smoke-tests.sh). The fixture must be loadable by the engine under test.
# Sanitizer options are honoured from the environment. All transient state lives
# under ai/temp/ (gitignored; AGENTS.md rule 8); --clean removes it. On PASS the
# derived ~186 MB network saves are deleted (logs kept); on FAIL everything is
# kept for artifact upload.

set -u
set -o pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(dirname -- "$script_dir")

EXE=""
SERVER_EXE="${BB_SYNC_SERVER_EXE:-}"
CLIENT_EXE="${BB_SYNC_CLIENT_EXE:-}"
WORKDIR=""
PAKSET="pak128.Britain-Ex"
OBJECTS="pak128.Britain-Ex"
FIXTURE="${BB_SYNC_FIXTURE:-}"
SERVER_PORT="${BB_SYNC_PORT:-13356}"
READY_TIMEOUT_S=1800
CLIENT_TIMEOUT_S=1800
PRE_JOIN_STEPS=50
OBSERVE_S=240
MIN_COMMON_STEPS=10
DEBUG_LEVEL=2
SAVE_FORMAT="zstd"
MARKERS="FATAL ERROR|AddressSanitizer|runtime error|rendezvous released with work outstanding"
CLEAN=0

usage() {
	cat <<EOF
Usage: run-join-sync-test.sh [options]
  --exe PATH                 binary for both roles (default: <repo>/simutrans-extended;
                             env BB_SYNC_SERVER_EXE/BB_SYNC_CLIENT_EXE override per role)
  --server-exe PATH          server-role binary (default: --exe)
  --client-exe PATH          client-role binary (default: --exe)
  --workdir DIR              transient work directory (default: <repo>/ai/temp/join-sync)
  --pakset NAME              pakset dir name under <repo>/simutrans/ (default: $PAKSET)
  --objects NAME             -objects name / pakset link name in workdirs (default: $OBJECTS)
  --fixture PATH             savegame to load (env BB_SYNC_FIXTURE;
                             default: <repo>/bb6-apr-2010.sve)
  --server-port PORT         loopback server port (default: $SERVER_PORT)
  --server-ready-timeout S   watchdog for server load + pre-join steps (default: $READY_TIMEOUT_S)
  --client-timeout S         watchdog for the whole client phase (default: $CLIENT_TIMEOUT_S)
  --pre-join-steps N         server free-run steps before the join (default: $PRE_JOIN_STEPS)
  --observe-seconds S        joined-play observation window (default: $OBSERVE_S)
  --min-common-steps N       minimum comparable per-step route hashes (default: $MIN_COMMON_STEPS)
  --debug-level N            engine -debug level, >= 2 for the route hash (default: $DEBUG_LEVEL)
  --save-format FORMAT       saveformat written to simuconf.tab (default: $SAVE_FORMAT;
                             engines built without zstd silently fall back to bzip2)
  --markers REGEX            ERE of failure markers to grep in the run logs
                             (default: "$MARKERS";
                              TSan builds: "FATAL ERROR|ThreadSanitizer|runtime error")
  --clean                    remove the workdir and exit
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--exe) EXE="$2"; shift 2 ;;
		--server-exe) SERVER_EXE="$2"; shift 2 ;;
		--client-exe) CLIENT_EXE="$2"; shift 2 ;;
		--workdir) WORKDIR="$2"; shift 2 ;;
		--pakset) PAKSET="$2"; shift 2 ;;
		--objects) OBJECTS="$2"; shift 2 ;;
		--fixture) FIXTURE="$2"; shift 2 ;;
		--server-port) SERVER_PORT="$2"; shift 2 ;;
		--server-ready-timeout) READY_TIMEOUT_S="$2"; shift 2 ;;
		--client-timeout) CLIENT_TIMEOUT_S="$2"; shift 2 ;;
		--pre-join-steps) PRE_JOIN_STEPS="$2"; shift 2 ;;
		--observe-seconds) OBSERVE_S="$2"; shift 2 ;;
		--min-common-steps) MIN_COMMON_STEPS="$2"; shift 2 ;;
		--debug-level) DEBUG_LEVEL="$2"; shift 2 ;;
		--save-format) SAVE_FORMAT="$2"; shift 2 ;;
		--markers) MARKERS="$2"; shift 2 ;;
		--clean) CLEAN=1; shift ;;
		-h|--help) usage; exit 0 ;;
		*) echo "FAIL: unknown argument: $1"; usage; exit 1 ;;
	esac
done

[[ -n "$EXE" ]]        || EXE="$repo/simutrans-extended"
[[ -n "$SERVER_EXE" ]] || SERVER_EXE="$EXE"
[[ -n "$CLIENT_EXE" ]] || CLIENT_EXE="$EXE"
[[ -n "$WORKDIR" ]]    || WORKDIR="$repo/ai/temp/join-sync"
[[ -n "$FIXTURE" ]]    || FIXTURE="$repo/bb6-apr-2010.sve"

if [[ "$DEBUG_LEVEL" -lt 2 ]]; then
	echo "FAIL: --debug-level must be >= 2 (the per-step route-hash oracle is logged only at -debug 2 or higher)"
	exit 1
fi

SRV_DIR="$WORKDIR/server"
CLI_DIR="$WORKDIR/client"
LOGS="$WORKDIR/logs"

if [[ "$CLEAN" -eq 1 ]]; then
	# rm -rf removes symlinks as links without following them, so a plain
	# recursive delete is safe here.
	[[ -d "$WORKDIR" ]] && rm -rf -- "$WORKDIR"
	echo "CLEAN: removed $WORKDIR"
	exit 0
fi

for p in "$SERVER_EXE" "$CLIENT_EXE" "$FIXTURE"; do
	[[ -f "$p" ]] || { echo "FAIL: missing $p"; exit 1; }
done
pak_target="$repo/simutrans/$PAKSET"
[[ -d "$pak_target" ]] || { echo "FAIL: missing pakset $pak_target"; exit 1; }

# Fresh role workdirs every run (settings and stale network saves persist
# between runs and would poison the comparison). Missing font/text/themes in a
# workdir is a fatal error for the engine.
setup_role() {
	local dir="$1"
	rm -rf -- "$dir"
	mkdir -p "$dir/config" "$dir/save"
	ln -sfn -- "$pak_target" "$dir/$OBJECTS"
	for d in font text themes; do
		ln -sfn -- "$repo/simutrans/$d" "$dir/$d"
	done
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
}

mkdir -p "$LOGS"
setup_role "$SRV_DIR"
setup_role "$CLI_DIR"

fixture_name=$(basename -- "$FIXTURE")
cp -- "$FIXTURE" "$SRV_DIR/save/"

s_out="$LOGS/server.out.log"; s_err="$LOGS/server.err.log"
c_out="$LOGS/client.out.log"; c_err="$LOGS/client.err.log"
rm -f -- "$s_out" "$s_err" "$c_out" "$c_err" "$LOGS"/*.server.log

export ASAN_OPTIONS="${ASAN_OPTIONS:-print_stacktrace=1 abort_on_error=1 detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1 abort_on_error=1}"
export TSAN_OPTIONS="${TSAN_OPTIONS:-print_stacktrace=1 second_deadlock_stack=1 history_size=7}"

failures=()

server_pid=""
client_pid=""

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

cleanup() {
	kill_tree "$client_pid"
	kill_tree "$server_pid"
	return 0
}
trap cleanup EXIT INT TERM

fail_now() {
	echo "RESULT: FAIL"
	for f in "$@"; do echo "  - $f"; done
	exit 1
}

log_markers() {
	local f m bad=""
	for f in "$s_err" "$s_out" "$c_err" "$c_out" "$LOGS/server.server.log" "$LOGS/client.server.log"; do
		[[ -f "$f" ]] || continue
		m=$(grep -h -m 3 -E "$MARKERS" -- "$f")
		[[ -n "$m" ]] && bad+="$m"$'\n'
	done
	printf '%s' "$bad"
}

echo "Starting server: $SERVER_EXE (port $SERVER_PORT, fixture $fixture_name, pre-join steps $PRE_JOIN_STEPS)"
# The server must stay alive for its own load+free-run phase, the whole client
# phase (the join save/transfer and the client's load happen while the server
# waits paused), the observation window, and a margin.
server_watchdog=$((READY_TIMEOUT_S + CLIENT_TIMEOUT_S + OBSERVE_S + 300))
timeout -k 5 "$server_watchdog" "$SERVER_EXE" \
	-set_workdir "$SRV_DIR" -singleuser -objects "$OBJECTS" \
	-load "$fixture_name" -server "$SERVER_PORT" \
	-debug "$DEBUG_LEVEL" -lang en -fps 100 -nosound \
	> "$s_out" 2> "$s_err" &
server_pid=$!

# Wait until the server has finished loading AND free-ran PRE_JOIN_STEPS steps,
# so that private-car recording + the duplicate-list merge have run and
# orphaned shared lists exist before the join save is taken.
srv_code=""
step_pattern="Private car route hash step $((PRE_JOIN_STEPS - 1)):"
ready=0
for ((i=0; i<READY_TIMEOUT_S; i++)); do
	sleep 1
	if [[ -z "$srv_code" ]] && ! kill -0 "$server_pid" 2>/dev/null; then
		wait "$server_pid"; srv_code=$?
	fi
	if [[ -n "$srv_code" ]]; then
		echo "---- server.err.log (tail) ----"
		tail -n 20 -- "$s_err" 2>/dev/null
		bad=$(log_markers)
		fail_now "server : exited with code $srv_code before reaching step $((PRE_JOIN_STEPS - 1))" \
			${bad:+"log markers -> $(printf '%s' "$bad" | tr '\n' '|' | sed 's/|$//')"}
	fi
	if grep -qF -- "$step_pattern" "$s_err" 2>/dev/null; then ready=1; break; fi
done
if [[ "$ready" -ne 1 ]]; then
	bad=$(log_markers)
	fail_now "server : did not reach step $((PRE_JOIN_STEPS - 1)) within ${READY_TIMEOUT_S}s" \
		${bad:+"log markers -> $(printf '%s' "$bad" | tr '\n' '|' | sed 's/|$//')"}
fi
echo "Server reached step $((PRE_JOIN_STEPS - 1)) after ~${i}s; starting client"

echo "Starting client: $CLIENT_EXE (net:127.0.0.1:$SERVER_PORT)"
timeout -k 5 "$CLIENT_TIMEOUT_S" "$CLIENT_EXE" \
	-set_workdir "$CLI_DIR" -singleuser -objects "$OBJECTS" \
	-load "net:127.0.0.1:$SERVER_PORT" \
	-debug "$DEBUG_LEVEL" -lang en -fps 100 -nosound \
	> "$c_out" 2> "$c_err" &
client_pid=$!

desynced=0
joined_at=""
cli_code=""
for ((i=0; i*2<CLIENT_TIMEOUT_S; i++)); do
	sleep 2
	if grep -qE "Lost synchronisation|checklist mismatch" "$c_err" 2>/dev/null \
		|| grep -qE "checklist mismatch|disconnect client|kicking client" "$s_err" 2>/dev/null; then
		desynced=1
		sleep 5
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
	if [[ -z "$joined_at" ]] && grep -qE "network_game_set_pause.*pause=0" "$c_err" 2>/dev/null; then
		joined_at=$i
		echo "Client joined and unpaused after ~$((i*2))s; observing for ${OBSERVE_S}s"
	fi
	if [[ -n "$joined_at" ]] && (( (i - joined_at) * 2 >= OBSERVE_S )); then break; fi
done

# Capture exit codes while the jobs are still unreaped, then stop both engines.
if [[ -z "$cli_code" ]] && ! kill -0 "$client_pid" 2>/dev/null; then
	wait "$client_pid"; cli_code=$?
fi
if [[ -z "$srv_code" ]] && ! kill -0 "$server_pid" 2>/dev/null; then
	wait "$server_pid"; srv_code=$?
fi
kill_tree "$client_pid"
kill_tree "$server_pid"

# simu-server<port>.log exists only when the engine is run with -log; move it
# aside if a future invocation enables it.
[[ -f "$SRV_DIR/simu-server$SERVER_PORT.log" ]] && mv -f -- "$SRV_DIR/simu-server$SERVER_PORT.log" "$LOGS/server.server.log"
[[ -f "$CLI_DIR/simu-server$SERVER_PORT.log" ]] && mv -f -- "$CLI_DIR/simu-server$SERVER_PORT.log" "$LOGS/client.server.log"

echo "---- analysis ----"

# Run-phase verdicts.
if [[ "$desynced" -eq 1 ]]; then
	failures+=("join-sync : desync markers detected (client 'Lost synchronisation'/'checklist mismatch' or server kick)")
fi
if [[ -z "$joined_at" && "$desynced" -eq 0 ]]; then
	failures+=("join-sync : client never joined and unpaused within ${CLIENT_TIMEOUT_S}s (no 'network_game_set_pause ... pause=0')")
fi
if [[ -n "$cli_code" && "$cli_code" -ne 0 ]]; then
	if [[ "$cli_code" -eq 124 || "$cli_code" -eq 137 ]]; then
		failures+=("client : TIMEOUT after ${CLIENT_TIMEOUT_S}s - killed")
	else
		failures+=("client : exit code $cli_code")
	fi
fi
if [[ -n "$srv_code" && "$srv_code" -ne 0 ]]; then
	if [[ "$srv_code" -eq 124 || "$srv_code" -eq 137 ]]; then
		failures+=("server : TIMEOUT after ${server_watchdog}s - killed")
	else
		failures+=("server : exit code $srv_code")
	fi
fi
if [[ -n "$joined_at" && -z "$cli_code" && "$desynced" -eq 0 ]] \
	&& (( (i - joined_at) * 2 < OBSERVE_S )); then
	failures+=("join-sync : observation ended after $(( (i - joined_at) * 2 ))s of the requested ${OBSERVE_S}s")
fi

bad=$(log_markers)
if [[ -n "$bad" ]]; then
	failures+=("logs : markers -> $(printf '%s' "$bad" | tr '\n' '|' | sed 's/|$//')")
fi

# Fix evidence (informational): the promotion runs inside the join save only
# when orphaned lists exist at that moment.
promoted=$(grep -h -m 1 -E "Promoted [0-9]+ linked private car route slots" "$s_err" 2>/dev/null || true)
if [[ -n "$promoted" ]]; then
	echo "PROMOTION: $promoted"
else
	echo "PROMOTION: no promotion logged at save time (no orphaned lists existed in this run)"
fi

# After-load route hashes (informational only: the server's is from the fixture
# load, the client's from the join save taken PRE_JOIN_STEPS steps later, so
# they are not expected to be equal).
s_load=$(grep -oE "route hash after load: all=[0-9a-f]+" "$s_err" 2>/dev/null | tail -n 1 || true)
c_load=$(grep -oE "route hash after load: all=[0-9a-f]+" "$c_err" 2>/dev/null | tail -n 1 || true)
[[ -n "$s_load" ]] && echo "after-load route hash (server, fixture load): $s_load"
[[ -n "$c_load" ]] && echo "after-load route hash (client, join save):    $c_load"

# Oracle 2: per-step semantic route hashes over common steps; per step the last
# occurrence wins (see the header for why the server can log a step twice).
s_pairs="$LOGS/server.hash-steps.txt"
c_pairs="$LOGS/client.hash-steps.txt"
grep -oE "Private car route hash step [0-9]+: [0-9a-f]+" -- "$s_err" 2>/dev/null \
	| sed -E 's/^Private car route hash step ([0-9]+): ([0-9a-f]+)$/\1 \2/' > "$s_pairs" || true
grep -oE "Private car route hash step [0-9]+: [0-9a-f]+" -- "$c_err" 2>/dev/null \
	| sed -E 's/^Private car route hash step ([0-9]+): ([0-9a-f]+)$/\1 \2/' > "$c_pairs" || true
read -r common_n diverge_n first_diff < <(awk '
	NR==FNR { srv[$1] = $2; next }
	($1 in srv) {
		n++
		if (srv[$1] != $2) { d++; if (first == "") first = "step " $1 " server=" srv[$1] " client=" $2 }
	}
	END { printf "%d %d %s\n", n+0, d+0, (first == "" ? "-" : first) }
' "$s_pairs" "$c_pairs")
echo "route hash steps: server=$(wc -l < "$s_pairs") client=$(wc -l < "$c_pairs") common=$common_n diverging=$diverge_n"
if [[ "$common_n" -lt "$MIN_COMMON_STEPS" ]]; then
	failures+=("join-sync : too few comparable per-step route hashes (common=$common_n < $MIN_COMMON_STEPS; the hash is logged only at -debug 2 or higher)")
elif [[ "$diverge_n" -gt 0 ]]; then
	failures+=("join-sync : per-step route hashes diverge on $diverge_n of $common_n common steps; first: $first_diff")
else
	echo "ROUTE-HASHES: PASS ($common_n common steps identical)"
fi

# Oracle 3: the checklist pairs the client prints for every received
# nwc_check_t (multi-line server=[...]/client=[...] blocks; mismatch count).
# Both blocks are captured from the "ss=" field text onwards (the differing
# entity prefixes are stripped) so that identical states compare equal; a pair
# only counts when both halves look like a checklist (contain "sums="), which
# keeps interleaved log lines from producing spurious mismatches.
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

# On PASS drop the derived ~186 MB network saves (logs are kept); on FAIL keep
# everything for artifact upload.
if [[ ${#failures[@]} -eq 0 ]]; then
	rm -f -- "$SRV_DIR"/server"$SERVER_PORT"-*.sve "$SRV_DIR"/server"$SERVER_PORT"-*.sv_ 2>/dev/null || true
	rm -f -- "$CLI_DIR"/client*-network.sve 2>/dev/null || true
fi

if [[ ${#failures[@]} -gt 0 ]]; then
	echo "RESULT: FAIL"
	for f in "${failures[@]}"; do echo "  - $f"; done
	exit 1
fi
echo "RESULT: PASS (client stayed in sync for ${OBSERVE_S}s of joined play; $common_n common per-step route hashes identical)"
exit 0
