#!/usr/bin/env bash
#
# Simutrans-Extended world-generation test runner (Linux): drives the headless
# -generate_map mode across a matrix of map sizes/seeds/town counts.
#
# Purpose: exercise karte_t::init (terrain, cities, industries, attractions) in
# CI. Map generation is otherwise untested automatically: Squirrel scripting is
# non-functional in Extended and the smoke harness only loads a fixed savegame.
# This runner would have caught the objlist seqlock self-deadlock (2026-09,
# loesche_alle deleting a building whose destructor re-read the same list during
# city growth): it hung map generation whenever city growth replaced a building.
#
# Pass criteria per run: exit code 0, a "MAP-GEN: PASS" line on stdout, and no
# failure markers in the logs. A deadlock fails via the per-run watchdog
# (timeout exit 124/137); with a DEBUG build the bounded seqlock retry assertion
# (OLIST_SPIN_CHECK) turns that class of hang into a fast FATAL ERROR instead.
#
# Requires a pakset matching the branch's Extended object version (same rule as
# run-smoke-tests.sh). Sanitizer options are honoured from the environment.
# All transient state lives under ai/temp/ (gitignored; AGENTS.md rule 8);
# --clean removes it.

set -u
set -o pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(dirname -- "$script_dir")

EXE=""
WORKDIR=""
PAKSET="pak128.Britain-Ex"
OBJECTS="pak128.Britain-Ex"
TIMEOUT_S=600
MARKERS="FATAL ERROR|AddressSanitizer|runtime error|rendezvous released with work outstanding"
CLEAN=0

usage() {
	cat <<EOF
Usage: run-mapgen-tests.sh [options]
  --exe PATH           simutrans binary (default: <repo>/simutrans-extended)
  --workdir DIR        transient work directory (default: <repo>/ai/temp/mapgen)
  --pakset NAME        pakset dir name under <repo>/simutrans/ (default: $PAKSET)
  --objects NAME       -objects name / pakset link name in workdir (default: $OBJECTS)
  --timeout SECONDS    watchdog per map-generation run (default: $TIMEOUT_S)
  --markers REGEX      ERE of failure markers to grep in the run logs
                       (default: "$MARKERS";
                        TSan builds: "FATAL ERROR|ThreadSanitizer|runtime error")
  --clean              remove the workdir and exit
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--exe) EXE="$2"; shift 2 ;;
		--workdir) WORKDIR="$2"; shift 2 ;;
		--pakset) PAKSET="$2"; shift 2 ;;
		--objects) OBJECTS="$2"; shift 2 ;;
		--timeout) TIMEOUT_S="$2"; shift 2 ;;
		--markers) MARKERS="$2"; shift 2 ;;
		--clean) CLEAN=1; shift ;;
		-h|--help) usage; exit 0 ;;
		*) echo "FAIL: unknown argument: $1"; usage; exit 1 ;;
	esac
done

[[ -n "$EXE" ]]     || EXE="$repo/simutrans-extended"
[[ -n "$WORKDIR" ]] || WORKDIR="$repo/ai/temp/mapgen"
LOGS="$WORKDIR/logs"

if [[ "$CLEAN" -eq 1 ]]; then
	# rm -rf removes symlinks as links without following them (unlike PowerShell
	# 5.1 over junctions), so a plain recursive delete is safe here.
	[[ -d "$WORKDIR" ]] && rm -rf -- "$WORKDIR"
	echo "CLEAN: removed $WORKDIR"
	exit 0
fi

[[ -f "$EXE" ]] || { echo "FAIL: missing $EXE"; exit 1; }
pak_target="$repo/simutrans/$PAKSET"
[[ -d "$pak_target" ]] || { echo "FAIL: missing pakset $pak_target"; exit 1; }

# Idempotent workdir setup (all under ai/temp/, gitignored).
# Missing font/text/themes in the workdir is a fatal error for the engine.
mkdir -p "$LOGS" "$WORKDIR/config"
ln -sfn -- "$pak_target" "$WORKDIR/$OBJECTS"
for d in font text themes; do
	ln -sfn -- "$repo/simutrans/$d" "$WORKDIR/$d"
done

export ASAN_OPTIONS="${ASAN_OPTIONS:-print_stacktrace=1 abort_on_error=1 detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1 abort_on_error=1}"
export TSAN_OPTIONS="${TSAN_OPTIONS:-print_stacktrace=1 second_deadlock_stack=1 history_size=7}"

# Map-generation matrix: tag|size|seed|towns|factories|attractions|water level.
# The small maps run in seconds; the 256x256 case places multiple towns with the
# compiled-in default settings (smaller maps typically place only one with them)
# and takes minutes under ASan/TSan, hence the generous default watchdog.
cases=(
	"small-s1|64,64|1|2|4|2|-2"
	"small-s2|64,64|2|2|4|2|-2"
	"small-s3|64,64|3|2|4|2|-2"
	"small-s4|64,64|4|2|4|2|-2"
	"small-s5|64,64|5|2|4|2|-2"
	"medium-s7|96,96|7|4|6|3|-2"
	"medium-s11|96,96|11|4|6|3|-2"
	"large-s42|128,128|42|8|8|4|-2"
	"xlarge-s42|256,256|42|8|8|4|0"
)

failures=()

for case in "${cases[@]}"; do
	IFS='|' read -r tag size seed towns factories attractions water <<< "$case"
	out="$LOGS/$tag.out.log"
	err="$LOGS/$tag.err.log"
	rm -f -- "$out" "$err"
	timeout -k 5 "$TIMEOUT_S" "$EXE" \
		-set_workdir "$WORKDIR" -singleuser -objects "$OBJECTS" \
		-generate_map -map_size "$size" -map_seed "$seed" -map_towns "$towns" \
		-map_factories "$factories" -map_attractions "$attractions" -map_water_level "$water" \
		-debug 2 -lang en -nosound \
		> "$out" 2> "$err"
	code=$?
	if [[ $code -eq 124 || $code -eq 137 ]]; then
		failures+=("mapgen [$tag] : TIMEOUT after ${TIMEOUT_S}s (size=$size seed=$seed towns=$towns) - killed")
		continue
	fi
	if [[ $code -ne 0 ]]; then
		failures+=("mapgen [$tag] : exit code $code (size=$size seed=$seed towns=$towns)")
		continue
	fi
	if ! grep -q "MAP-GEN: PASS" -- "$out"; then
		failures+=("mapgen [$tag] : no MAP-GEN: PASS line (size=$size seed=$seed towns=$towns)")
		continue
	fi
	bad=$(grep -h -m 3 -E "$MARKERS" -- "$err" "$out" 2>/dev/null || true)
	if [[ -n "$bad" ]]; then
		failures+=("mapgen [$tag] : log markers -> $(echo "$bad" | tr '\n' '|' | sed 's/|$//')")
		continue
	fi
	echo "[mapgen $tag] PASS: $(grep -m 1 'MAP-GEN: PASS' -- "$out")"
done

if [[ ${#failures[@]} -gt 0 ]]; then
	echo "RESULT: FAIL"
	for f in "${failures[@]}"; do echo "  - $f"; done
	exit 1
fi
echo "RESULT: PASS (${#cases[@]} map-generation runs)"
exit 0
