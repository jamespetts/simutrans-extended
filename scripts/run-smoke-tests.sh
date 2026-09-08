#!/usr/bin/env bash
#
# Simutrans-Extended test runner (Linux): smoke + network determinism.
# Linux analogue of scripts/run-smoke-tests.ps1 with the same semantics.
#
# Default mode is network: run a loopback-only server with -fast-network-sync against
# tests/demo.sve and compare the final server<port>-restore.sve written at the -until
# horizon. This exercises the network server code path, where deterministic lockstep is
# expected.
#
# Optional singleuser mode fast-forwards and compares monthly autosaves. It exercises the
# single-player code path; byte-identical determinism is NOT expected there.
#
# Requires a DEBUG or PROFILE build (-until is compiled only in those builds; in CI append
# "DEBUG = 2" to config.default) and a pakset matching the branch's Extended object
# version (engine master: pakset master branch; engine ex-15: pakset ex-15 branch; a
# mismatched pak version fatals in the object readers).
#
# Sanitizer options are honoured from the environment; ASan/UBSan defaults set below.
# All transient state lives under ai/temp/ (gitignored; AGENTS.md rule 8); --clean
# removes it.

set -u
set -o pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(dirname -- "$script_dir")

EXE=""
WORKDIR=""
PAKSET="pak128.Britain-Ex"
OBJECTS="pak128.Britain-Ex"
FIXTURE=""
UNTIL="1945.6"
RT_AUTOSAVE="autosave06.sve"
TIMEOUT_S=240
SAVE_FORMAT="zipped"
MODE="network"
FAST_NETWORK_SYNC=100
SERVER_PORT=13353
MARKERS="FATAL ERROR|AddressSanitizer|runtime error"
SKIP_ROUNDTRIP=0
CLEAN=0

usage() {
	cat <<EOF
Usage: run-smoke-tests.sh [options]
  --exe PATH                 simutrans binary (default: <repo>/simutrans-extended)
  --workdir DIR              transient work directory (default: <repo>/ai/temp/simutest)
  --pakset NAME              pakset dir name under <repo>/simutrans/ (default: $PAKSET)
  --objects NAME             -objects name / pakset link name in workdir (default: $OBJECTS)
  --fixture PATH             savegame to load (default: <repo>/tests/demo.sve)
  --until Y.M                run horizon (default: $UNTIL; needs DEBUG/PROFILE build)
  --roundtrip-autosave NAME  run-A autosave to reload in singleuser mode (default: $RT_AUTOSAVE)
  --timeout SECONDS          watchdog per run (default: $TIMEOUT_S)
  --save-format FORMAT       save/autosave format written by the engine (default: $SAVE_FORMAT)
  --mode MODE                singleuser or network (default: $MODE)
  --fast-network-sync N      network-mode speed multiplier (default: $FAST_NETWORK_SYNC)
  --server-port PORT         network-mode server port (default: $SERVER_PORT)
  --markers REGEX            ERE of failure markers to grep in the run logs
                             (default: "$MARKERS";
                              TSan builds: "FATAL ERROR|ThreadSanitizer|runtime error")
  --skip-roundtrip           skip the singleuser round-trip test
  --clean                    remove the workdir and exit
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--exe) EXE="$2"; shift 2 ;;
		--workdir) WORKDIR="$2"; shift 2 ;;
		--pakset) PAKSET="$2"; shift 2 ;;
		--objects) OBJECTS="$2"; shift 2 ;;
		--fixture) FIXTURE="$2"; shift 2 ;;
		--until) UNTIL="$2"; shift 2 ;;
		--roundtrip-autosave) RT_AUTOSAVE="$2"; shift 2 ;;
		--timeout) TIMEOUT_S="$2"; shift 2 ;;
		--save-format) SAVE_FORMAT="$2"; shift 2 ;;
		--mode) MODE="$2"; shift 2 ;;
		--fast-network-sync) FAST_NETWORK_SYNC="$2"; shift 2 ;;
		--server-port) SERVER_PORT="$2"; shift 2 ;;
		--markers) MARKERS="$2"; shift 2 ;;
		--skip-roundtrip) SKIP_ROUNDTRIP=1; shift ;;
		--clean) CLEAN=1; shift ;;
		-h|--help) usage; exit 0 ;;
		*) echo "FAIL: unknown argument: $1"; usage; exit 1 ;;
	esac
done

if [[ "$MODE" != "singleuser" && "$MODE" != "network" ]]; then
	echo "FAIL: --mode must be 'singleuser' or 'network'"
	exit 1
fi
if [[ "$MODE" == "network" && "$FAST_NETWORK_SYNC" -lt 1 ]]; then
	echo "FAIL: network mode requires --fast-network-sync >= 1"
	exit 1
fi

[[ -n "$EXE" ]]     || EXE="$repo/simutrans-extended"
[[ -n "$WORKDIR" ]] || WORKDIR="$repo/ai/temp/simutest"
[[ -n "$FIXTURE" ]] || FIXTURE="$repo/tests/demo.sve"
SAVE="$WORKDIR/save"
LOGS="$WORKDIR/logs"
RES="$WORKDIR/results"
STATE_PATTERN="autosave*.sve"
if [[ "$MODE" == "network" ]]; then STATE_PATTERN="final.sve"; fi

if [[ "$CLEAN" -eq 1 ]]; then
	# rm -rf removes symlinks as links without following them (unlike PowerShell
	# 5.1 over junctions), so a plain recursive delete is safe here.
	[[ -d "$WORKDIR" ]] && rm -rf -- "$WORKDIR"
	echo "CLEAN: removed $WORKDIR"
	exit 0
fi

for p in "$EXE" "$FIXTURE"; do
	[[ -f "$p" ]] || { echo "FAIL: missing $p"; exit 1; }
done
pak_target="$repo/simutrans/$PAKSET"
[[ -d "$pak_target" ]] || { echo "FAIL: missing pakset $pak_target"; exit 1; }

# Idempotent workdir setup (all under ai/temp/, gitignored).
# Missing font/text/themes in the workdir is a fatal error for the engine.
mkdir -p "$SAVE" "$LOGS" "$WORKDIR/config"
for d in runA runB runC; do
	rm -rf -- "$RES/$d"
	mkdir -p "$RES/$d"
done
ln -sfn -- "$pak_target" "$WORKDIR/$OBJECTS"
for d in font text themes; do
	ln -sfn -- "$repo/simutrans/$d" "$WORKDIR/$d"
done

{
	printf 'frames_per_second = 100\nfast_forward_frames_per_second = 100\n'
	if [[ "$MODE" == "network" ]]; then
		printf 'autosave = 0\n'
		printf 'saveformat = %s\n' "$SAVE_FORMAT"
		printf 'listen = 127.0.0.1\n'
		printf 'announce_server = 0\n'
		printf 'pause_server_no_clients = 0\n'
		printf 'server_save_game_on_quit = 1\n'
		printf 'reload_and_save_on_quit = 0\n'
		printf 'server_frames_per_step = 4\n'
	else
		printf 'autosave = 1\n'
		printf 'autosaveformat = %s\n' "$SAVE_FORMAT"
	fi
} > "$WORKDIR/config/simuconf.tab"

fixture_name=$(basename -- "$FIXTURE")
[[ -f "$SAVE/$fixture_name" ]] || cp -- "$FIXTURE" "$SAVE/"

export ASAN_OPTIONS="${ASAN_OPTIONS:-print_stacktrace=1 abort_on_error=1 detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1 abort_on_error=1}"
export TSAN_OPTIONS="${TSAN_OPTIONS:-print_stacktrace=1 second_deadlock_stack=1 history_size=7}"

failures=()
code=0

# Watchdog: a stall was observed once in an unattended run (never reproduced), and
# fatal errors can block; always run under timeout and kill. 124/137 = timed out.
run_sim() {
	local load="$1" tag="$2" port="$3"
	local mode_args=()
	if [[ "$MODE" == "network" ]]; then
		mode_args+=(-server "$port" -fast-network-sync "$FAST_NETWORK_SYNC")
	fi
	rm -f -- "$LOGS/$tag.out.log" "$LOGS/$tag.err.log" "$LOGS/$tag.server.log"
	timeout -k 5 "$TIMEOUT_S" "$EXE" \
		-set_workdir "$WORKDIR" -singleuser -objects "$OBJECTS" \
		-load "$load" -until "$UNTIL" -debug 2 -lang en -fps 100 -nosound "${mode_args[@]}" \
		> "$LOGS/$tag.out.log" 2> "$LOGS/$tag.err.log"
	code=$?
	if [[ -f "$WORKDIR/simu-server$port.log" ]]; then
		mv -f -- "$WORKDIR/simu-server$port.log" "$LOGS/$tag.server.log"
	fi
	if [[ "$MODE" == "network" ]]; then sleep 1; fi
	if [[ $code -eq 124 || $code -eq 137 ]]; then
		echo "[$tag] TIMEOUT after ${TIMEOUT_S}s - killed"
	else
		echo "[$tag] exit code: $code"
	fi
}

clear_state() {
	rm -f -- "$SAVE"/autosave*.sve
	rm -f -- "$WORKDIR"/settings-extended*.xml
	rm -f -- "$WORKDIR"/server*-restore.sve
	rm -f -- "$WORKDIR"/server*-restore.sv_
	rm -f -- "$WORKDIR"/server*-pwdhash.sve
	rm -f -- "$WORKDIR"/server*-network.sve
	rm -f -- "$WORKDIR"/server*-network.sv_
	return 0
}

move_state() {
	local dest="$1" port="$2" f
	if [[ "$MODE" == "network" ]]; then
		if [[ -f "$WORKDIR/server$port-restore.sve" ]]; then
			mv -f -- "$WORKDIR/server$port-restore.sve" "$dest/final.sve"
		fi
		for f in "$WORKDIR"/server"$port"-*.sve; do
			[[ -e "$f" ]] && mv -f -- "$f" "$dest/"
		done
		for f in "$WORKDIR"/server"$port"-*.sv_; do
			[[ -e "$f" ]] && mv -f -- "$f" "$dest/"
		done
	else
		for f in "$SAVE"/autosave*.sve; do
			[[ -e "$f" ]] && mv -f -- "$f" "$dest/"
		done
	fi
	return 0
}

log_markers() {
	local tag="$1" f
	for f in "$LOGS/$tag.err.log" "$LOGS/$tag.out.log" "$LOGS/$tag.server.log"; do
		[[ -f "$f" ]] && grep -h -m 3 -E "$MARKERS" -- "$f"
	done
	return 0
}

fast_network_sync_logged() {
	local tag="$1" f
	for f in "$LOGS/$tag.server.log" "$LOGS/$tag.err.log" "$LOGS/$tag.out.log"; do
		if [[ -f "$f" ]] && grep -q "Fast network sync test mode enabled" -- "$f"; then
			return 0
		fi
	done
	return 1
}

join_comma() {
	local list
	list=$(printf ', %s' "$@")
	echo "${list:2}"
}

test_run() {
	local load="$1" tag="$2" subdir="$3" port="$4" bad
	clear_state
	run_sim "$load" "$tag" "$port"
	move_state "$RES/$subdir" "$port"
	[[ $code -eq 0 ]] || failures+=("smoke [$tag] : exit code $code")
	bad=$(log_markers "$tag")
	[[ -z "$bad" ]] || failures+=("smoke [$tag] : log markers -> $(echo "$bad" | tr '\n' '|' | sed 's/|$//')")
	if [[ "$MODE" == "network" ]] && ! fast_network_sync_logged "$tag"; then
		failures+=("network [$tag] : fast network sync test mode not logged")
	fi
}

test_run "$fixture_name" runA runA "$SERVER_PORT"
test_run "$fixture_name" runB runB "$SERVER_PORT"

a_count=$(find "$RES/runA" -maxdepth 1 -name "$STATE_PATTERN" | wc -l)
b_count=$(find "$RES/runB" -maxdepth 1 -name "$STATE_PATTERN" | wc -l)
echo "runA state files: $a_count  runB state files: $b_count"
det_fail=()
if [[ $a_count -eq 0 ]]; then
	failures+=("determinism : runA produced no comparable state files")
else
	shopt -s nullglob
	for f in "$RES"/runA/$STATE_PATTERN; do
		name=$(basename -- "$f")
		if [[ ! -f "$RES/runB/$name" ]]; then det_fail+=("$name missing in B"); continue; fi
		cmp -s -- "$f" "$RES/runB/$name" || det_fail+=("$name")
	done
	shopt -u nullglob
fi
if [[ ${#det_fail[@]} -gt 0 ]]; then
	failures+=("determinism : differing state files -> $(join_comma "${det_fail[@]}")")
else
	echo "DETERMINISM: PASS ($a_count state files byte-identical)"
fi

if [[ "$SKIP_ROUNDTRIP" -eq 1 ]]; then
	:
elif [[ "$MODE" == "network" ]]; then
	echo "ROUND-TRIP: SKIPPED (network final-save mode)"
else
	rt_source="$RES/runA/$RT_AUTOSAVE"
	if [[ -f "$rt_source" ]]; then
		cp -f -- "$rt_source" "$SAVE/rt.sve"
		test_run "rt.sve" runC runC "$SERVER_PORT"
		rt_fail=()
		rt_count=0
		shopt -s nullglob
		for f in "$RES"/runC/autosave*.sve; do
			name=$(basename -- "$f")
			if [[ ! -f "$RES/runA/$name" ]]; then rt_fail+=("$name missing in A"); continue; fi
			rt_count=$((rt_count + 1))
			cmp -s -- "$f" "$RES/runA/$name" || rt_fail+=("$name")
		done
		shopt -u nullglob
		if [[ ${#rt_fail[@]} -gt 0 ]]; then
			failures+=("round-trip : differing autosaves -> $(join_comma "${rt_fail[@]}")")
		elif [[ $rt_count -eq 0 ]]; then
			failures+=("round-trip : runC produced no comparable autosaves")
		else
			echo "ROUND-TRIP: PASS ($rt_count autosaves byte-identical)"
		fi
	else
		failures+=("round-trip : $rt_source not found (runA did not reach that month?)")
	fi
fi

if [[ ${#failures[@]} -gt 0 ]]; then
	echo "RESULT: FAIL"
	for f in "${failures[@]}"; do echo "  - $f"; done
	exit 1
fi
if [[ "$MODE" == "network" ]]; then
	echo "RESULT: PASS (smoke + network determinism)"
else
	echo "RESULT: PASS (smoke + determinism + round-trip)"
fi
exit 0
