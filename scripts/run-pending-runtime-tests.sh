#!/bin/bash
# Pending-runtime recombination fixture runner.
#
# This is NOT part of the default passing suite. The runner stages each selected
# case's scenario.nut with the shared harness and fixture maps in the pakset
# scenario directory, runs one game process per case, and checks the case's
# result markers:
#
#   RUNTIME CASE <case> PASS
#   RUNTIME CASE <case> FAIL: <assertion>
#
# Cases in cases/ are Y-division cases and use harness.nut. Cases in
# pair-cases/ are paired H exchange cases and use pair-harness.nut. Cases in
# choose-cases/ are choose-signal call-on cases and use choose-harness.nut. Cases in
# pickup-cases/ are pick-up goods cases and use pickup-harness.nut. The forward
# case additionally prints the older Y DIVISION PASS and Y DIVISION FAIL
# markers.
#
# The fixture worlds are tiny (16x16), so several cases are run at once. Each
# case has its own game process, scenario directory and log; the work directory
# is shared read-only. The default job count is the number of processors,
# capped at 8. Do not raise it without checking the memory that the selected
# pakset needs per process.
#
# Every case runs in fast-forward mode by default: the simulation advances in
# fast-forward mode instead of at wall-clock speed, and the fast_forward
# simuconf setting (env_t::max_acceleration) caps the step rate. The tests
# assert game-time state, so this changes only how long a run takes. Set
# PENDING_RUNTIME_FAST_FORWARD=0 to run at normal speed.
#
# Exit codes:
#   0  every selected case passed its result-marker and case-specific assertions
#   1  any assertion or setup failure, script error, no progress, timeout or
#      crash
#   2  invalid selection
#
# Environment:
#   PENDING_RUNTIME_CASES          space-separated case names to run; overrides
#                                  PENDING_RUNTIME_GROUPS
#   PENDING_RUNTIME_GROUPS         space-separated group names: division, pair,
#                                  choose, pickup (default: every group)
#   PENDING_RUNTIME_JOBS           game processes to run at once (default: the
#                                  processor count, capped at 8)
#   PENDING_RUNTIME_FAST_FORWARD   1 to run the simulation as fast as the
#                                  machine allows, 0 for wall-clock speed
#                                  (default 1)
#   PENDING_RUNTIME_DEBUG          engine -debug level (default 1). The three
#                                  cases that assert an in-game warning message
#                                  always use level 3, because those messages
#                                  are logged only at that level.
#   PENDING_RUNTIME_TIMEOUT        overall timeout per case, seconds (default 300)
#   PENDING_RUNTIME_PICKUP_TIMEOUT overall timeout for a slow pick-up goods case
#                                  (default 600: those cases build a consumer
#                                  industry, several services and a wagon
#                                  release sequence before the collection)
#   PENDING_RUNTIME_STALL_TIMEOUT  no-output-progress timeout per case (default 120).
#                                  The harnesses print a heartbeat every 200
#                                  observation steps, so this works at any debug
#                                  level.
#   PENDING_RUNTIME_BINARY         game binary, relative to simutrans/
#                                  (default ../simutrans-extended)
#   PENDING_RUNTIME_OBJECTS        pakset directory (default pak128.britain-ex-nightly)
#   PENDING_RUNTIME_LIST           1 to print the selected cases and exit
set -u

repo_root=$(cd "$(dirname "$0")/.." && pwd)
source_root="$repo_root/tests/pending-runtime"
workdir="$repo_root/simutrans"
objects=${PENDING_RUNTIME_OBJECTS:-pak128.britain-ex-nightly}
binary=${PENDING_RUNTIME_BINARY:-../simutrans-extended}
scenario_root="$workdir/$objects/scenario"

timeout_seconds=${PENDING_RUNTIME_TIMEOUT:-300}
pickup_timeout_seconds=${PENDING_RUNTIME_PICKUP_TIMEOUT:-600}
stall_seconds=${PENDING_RUNTIME_STALL_TIMEOUT:-120}
fast_forward=${PENDING_RUNTIME_FAST_FORWARD:-1}
debug_level=${PENDING_RUNTIME_DEBUG:-1}
# The result markers are Squirrel prints logged at LEVEL_ERROR; -debug 0 would
# suppress them.
[ "$debug_level" -lt 1 ] && debug_level=1

case_names=()
division_cases=(forward)
pair_cases=()
choose_cases=()
pickup_cases=()

for case_file in "$source_root"/cases/*/scenario.nut
do
	[ -f "$case_file" ] || continue
	division_cases+=("$(basename "$(dirname "$case_file")")")
done
for case_file in "$source_root"/pair-cases/*/scenario.nut
do
	[ -f "$case_file" ] || continue
	pair_cases+=("$(basename "$(dirname "$case_file")")")
done
for case_file in "$source_root"/choose-cases/*/scenario.nut
do
	[ -f "$case_file" ] || continue
	choose_cases+=("$(basename "$(dirname "$case_file")")")
done
for case_file in "$source_root"/pickup-cases/*/scenario.nut
do
	[ -f "$case_file" ] || continue
	pickup_cases+=("$(basename "$(dirname "$case_file")")")
done

group_cases()
{
	case "$1" in
		division) printf '%s\n' "${division_cases[@]}" ;;
		pair)     printf '%s\n' "${pair_cases[@]}" ;;
		choose)   printf '%s\n' "${choose_cases[@]}" ;;
		pickup)   printf '%s\n' "${pickup_cases[@]}" ;;
		*)        return 1 ;;
	esac
}

is_known_case()
{
	local wanted=$1
	local known
	for known in "${division_cases[@]}" "${pair_cases[@]}" "${choose_cases[@]}" "${pickup_cases[@]}"
	do
		if [ "$known" = "$wanted" ]; then return 0; fi
	done
	return 1
}

group_name_of_case()
{
	local wanted=$1
	local known
	for known in "${pickup_cases[@]}"; do [ "$known" = "$wanted" ] && { echo pickup; return; }; done
	for known in "${pair_cases[@]}"; do [ "$known" = "$wanted" ] && { echo pair; return; }; done
	for known in "${choose_cases[@]}"; do [ "$known" = "$wanted" ] && { echo choose; return; }; done
	echo division
}

if [ -n "${PENDING_RUNTIME_CASES:-}" ]
then
	# Optional subset. Reject unknown names so that a renamed case cannot make
	# a targeted run silently do nothing.
	for selected_case in $PENDING_RUNTIME_CASES
	do
		if ! is_known_case "$selected_case"
		then
			echo "Unknown case in PENDING_RUNTIME_CASES: $selected_case" >&2
			exit 2
		fi
		case_names+=("$selected_case")
	done
else
	# Slowest group first: parallel batches finish earlier when long cases are
	# started first.
	for selected_group in ${PENDING_RUNTIME_GROUPS:-pickup pair choose division}
	do
		if ! group_cases "$selected_group" > /dev/null
		then
			echo "Unknown group in PENDING_RUNTIME_GROUPS: $selected_group" >&2
			exit 2
		fi
		while IFS= read -r selected_case
		do
			[ -n "$selected_case" ] || continue
			case_names+=("$selected_case")
		done < <(group_cases "$selected_group")
	done
fi

# A repeated case would share a stage directory and a log with itself, so two
# processes would overwrite each other's run. Reject the selection instead.
selected_count=${#case_names[@]}
for ((i = 0; i < selected_count; i++))
do
	for ((j = i + 1; j < selected_count; j++))
	do
		if [ "${case_names[i]}" = "${case_names[j]}" ]
		then
			echo "Duplicate case in the selection: ${case_names[i]}" >&2
			exit 2
		fi
	done
done

if [ "${PENDING_RUNTIME_LIST:-0}" = "1" ]
then
	for selected_case in "${case_names[@]}"
	do
		printf '%s\t%s\n' "$(group_name_of_case "$selected_case")" "$selected_case"
	done
	exit 0
fi

if [ "${#case_names[@]}" -eq 0 ]
then
	echo "No pending-runtime cases selected." >&2
	exit 2
fi

if [ -n "${PENDING_RUNTIME_JOBS:-}" ]
then
	jobs=$PENDING_RUNTIME_JOBS
else
	if command -v nproc > /dev/null 2>&1
	then
		jobs=$(nproc)
	else
		jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
	fi
	[ "$jobs" -gt 8 ] && jobs=8
fi
[ "$jobs" -lt 1 ] && jobs=1
[ "$jobs" -gt "${#case_names[@]}" ] && jobs=${#case_names[@]}

if [ "$fast_forward" = "1" ]
then
	fast_forward_args=(-fast_forward)
	fast_forward_text="on"
else
	fast_forward_args=()
	fast_forward_text="off"
fi

# The in-game warning messages asserted by these cases are logged only at
# LEVEL_MSG (engine -debug 3), so they cannot use the default level.
warning_case_level()
{
	case "$1" in
		target-unreachable|unresolvable-order|pair-unresolvable) echo 3 ;;
		*) echo "$debug_level" ;;
	esac
}

case_scenario()
{
	local case_name=$1
	if [ "$case_name" = "forward" ]; then echo "$source_root/scenario.nut"; return; fi
	local group
	group=$(group_name_of_case "$case_name")
	case "$group" in
		division)   echo "$source_root/cases/$case_name/scenario.nut" ;;
		pair)       echo "$source_root/pair-cases/$case_name/scenario.nut" ;;
		choose)     echo "$source_root/choose-cases/$case_name/scenario.nut" ;;
		pickup)     echo "$source_root/pickup-cases/$case_name/scenario.nut" ;;
	esac
}

case_harness_file()
{
	local case_name=$1
	local group
	group=$(group_name_of_case "$case_name")
	case "$group" in
		division)   echo "$source_root/harness.nut" ;;
		pair)       echo "$source_root/pair-harness.nut" ;;
		choose)     echo "$source_root/choose-harness.nut" ;;
		pickup)     echo "$source_root/pickup-harness.nut" ;;
	esac
}

declare -A case_pid
declare -A case_log
declare -A case_stage
declare -A case_start
declare -A case_last_size
declare -A case_last_progress
declare -A case_timeout

active=0
failed_cases=()
start_all=$SECONDS

stop_game()
{
	local pid=${1:-}
	if [ -n "$pid" ]
	then
		kill "$pid" 2>/dev/null || true
		wait "$pid" 2>/dev/null || true
	fi
}

cleanup()
{
	local case_name
	for case_name in "${!case_pid[@]}"
	do
		stop_game "${case_pid[$case_name]}"
		rm -rf -- "${case_stage[$case_name]}"
	done
}
trap cleanup EXIT
trap 'cleanup; exit 130' INT TERM

start_case()
{
	local case_name=$1
	local case_file
	local harness_file
	local level
	local stage_dir
	local log_file

	case_file=$(case_scenario "$case_name")
	harness_file=$(case_harness_file "$case_name")
	level=$(warning_case_level "$case_name")

	if [ ! -f "$case_file" ]
	then
		echo "Case $case_name: scenario file not found: $case_file"
		return 1
	fi

	stage_dir="$scenario_root/pending-runtime-$case_name"
	log_file="$workdir/pending-runtime-$case_name.log"
	rm -rf -- "$stage_dir"
	mkdir -p "$stage_dir" || return 1
	cp "$harness_file" "$source_root"/*.sve "$stage_dir/" || return 1
	cp "$case_file" "$stage_dir/scenario.nut" || return 1

	rm -f -- "$log_file"
	(
		cd "$workdir" || exit 1
		exec "$binary" -use_workdir -objects "$objects" -lang en \
			-scenario "pending-runtime-$case_name" -debug "$level" \
			-nomidi -nosound "${fast_forward_args[@]}"
	) > "$log_file" 2>&1 &
	# The subshell uses exec, so its process identifier is the game process.
	case_pid[$case_name]=$!
	case_log[$case_name]=$log_file
	case_stage[$case_name]=$stage_dir
	case_start[$case_name]=$SECONDS
	case_last_size[$case_name]=-1
	case_last_progress[$case_name]=$SECONDS
	case "$case_name" in
		cargo-through|flash-full|flash-partial|flash-late)
			case_timeout[$case_name]=$pickup_timeout_seconds ;;
		*)
			case_timeout[$case_name]=$timeout_seconds ;;
	esac
	echo "=== Pending-runtime case: $case_name (debug $level) ==="
	return 0
}

finish_case()
{
	local case_name=$1
	local result=$2
	local log_file=${case_log[$case_name]}
	local elapsed=$((SECONDS - case_start[$case_name]))

	stop_game "${case_pid[$case_name]}"
	unset 'case_pid[$case_name]'

	if [ "$result" -ne 0 ]
	then
		failed_cases+=("$case_name")
		echo "Case $case_name: FAILED (${elapsed}s)"
		echo "Log: $log_file"
		grep -E 'RUNTIME CASE|Y DIVISION|Script: Error|</error>|FATAL|ASSERT' "$log_file" | tail -n 20 || true
	else
		echo "Case $case_name: PASS (${elapsed}s)"
		rm -f -- "$log_file"
	fi

	rm -rf -- "${case_stage[$case_name]}"
	unset 'case_log[$case_name]'
	unset 'case_stage[$case_name]'
	((active--))
}

echo "Pending-runtime: ${#case_names[@]} case(s), $jobs at a time, fast-forward $fast_forward_text, debug $debug_level"

next_case=0
case_count=${#case_names[@]}

while [ "$next_case" -lt "$case_count" ] || [ "$active" -gt 0 ]
do
	while [ "$active" -lt "$jobs" ] && [ "$next_case" -lt "$case_count" ]
	do
		if start_case "${case_names[$next_case]}"
		then
			((active++))
		else
			failed_cases+=("${case_names[$next_case]}")
		fi
		((next_case++))
	done

	sleep 0.2

	for case_name in "${case_names[@]}"
	do
		[ -n "${case_pid[$case_name]:-}" ] || continue
		log_file=${case_log[$case_name]}
		pid=${case_pid[$case_name]}

		if grep -qF "RUNTIME CASE $case_name PASS" "$log_file" 2>/dev/null
		then
			if [ "$case_name" = "target-unreachable" ] && ! grep -qF "check station geometry and one-way signs" "$log_file"
			then
				echo "Case $case_name: FAILED (missing unusable-geometry warning)"
				finish_case "$case_name" 1
			elif [ "$case_name" = "unresolvable-order" ] && ! grep -qF "cannot be completed into a powered consist from the two consists" "$log_file"
			then
				echo "Case $case_name: FAILED (missing unsatisfiable-order warning)"
				finish_case "$case_name" 1
			elif [ "$case_name" = "pair-unresolvable" ] && ! grep -qF "no arrangement of the two consists and the stop's loose vehicles satisfies both consist orders" "$log_file"
			then
				echo "Case $case_name: FAILED (missing unsatisfiable-pair warning)"
				finish_case "$case_name" 1
			else
				finish_case "$case_name" 0
			fi
			continue
		fi

		if grep -qF "RUNTIME CASE $case_name FAIL" "$log_file" 2>/dev/null
		then
			grep -m 1 -F "RUNTIME CASE $case_name FAIL" "$log_file" || true
			finish_case "$case_name" 1
			continue
		fi

		# "error [suspended] calling start" is the normal first suspension of
		# the scenario script for its first tool call, so it is not matched by
		# this check. Real script errors are logged as "Script: Error:" lines.
		if grep -q -e 'Script: Error' -e '</error>' "$log_file" 2>/dev/null
		then
			echo "Case $case_name: FAILED (script error)"
			grep -m 1 -e 'Script: Error' -e '</error>' "$log_file" || true
			finish_case "$case_name" 1
			continue
		fi

		if ! kill -0 "$pid" 2>/dev/null
		then
			echo "Case $case_name: FAILED (process exited before printing a result)"
			finish_case "$case_name" 1
			continue
		fi

		# The harness prints a heartbeat every 200 observation steps, so the
		# log grows during a long wait even at the default debug level.
		size=$(wc -c < "$log_file" 2>/dev/null || echo 0)
		if [ "$size" != "${case_last_size[$case_name]}" ]
		then
			case_last_size[$case_name]=$size
			case_last_progress[$case_name]=$SECONDS
		elif (( SECONDS - case_last_progress[$case_name] >= stall_seconds ))
		then
			echo "Case $case_name: FAILED (no output progress for ${stall_seconds} seconds)"
			finish_case "$case_name" 1
			continue
		fi

		if (( SECONDS - case_start[$case_name] >= case_timeout[$case_name] ))
		then
			echo "Case $case_name: FAILED (timed out after ${case_timeout[$case_name]} seconds)"
			finish_case "$case_name" 1
			continue
		fi
	done
done

echo "Pending-runtime: $((SECONDS - start_all))s total"

if [ "${#failed_cases[@]}" -ne 0 ]
then
	echo "Pending-runtime cases failed: ${failed_cases[*]}"
	exit 1
fi

echo "All pending-runtime cases passed."
exit 0
