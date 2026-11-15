#!/bin/bash
# Pending-runtime recombination fixture runner.
#
# This is NOT part of the default passing suite. For every committed case the
# runner stages the case's scenario.nut with the shared harness and fixture maps
# in the pakset scenario directory, runs one game process, and checks the case's
# result markers:
#
#   RUNTIME CASE <case> PASS
#   RUNTIME CASE <case> FAIL: <assertion>
#
# Cases in cases/ are Y-division cases and use harness.nut. Cases in
# pair-cases/ are paired H exchange cases and use pair-harness.nut. Cases in
# choose-cases/ are choose-signal call-on cases and use choose-harness.nut. The forward
# case additionally prints the older Y DIVISION PASS and Y DIVISION FAIL
# markers.
#
# Exit codes:
#   0  every case passed its result-marker and case-specific assertions
#   1  any assertion or setup failure, script error, no progress, timeout or
#      crash
#
# Environment:
#   PENDING_RUNTIME_TIMEOUT        overall timeout per case, seconds (default 300)
#   PENDING_RUNTIME_STALL_TIMEOUT  no-output-progress timeout per case (default 120)
#   PENDING_RUNTIME_BINARY         game binary, relative to simutrans/
#                                  (default ../simutrans-extended)
#   PENDING_RUNTIME_OBJECTS        pakset directory (default pak128.britain-ex-nightly)
#   PENDING_RUNTIME_CASES          space-separated subset of case names to run
#                                  (default: every case)
set -u

repo_root=$(cd "$(dirname "$0")/.." && pwd)
source_root="$repo_root/tests/pending-runtime"
workdir="$repo_root/simutrans"
objects=${PENDING_RUNTIME_OBJECTS:-pak128.britain-ex-nightly}
binary=${PENDING_RUNTIME_BINARY:-../simutrans-extended}
scenario_root="$workdir/$objects/scenario"

timeout_seconds=${PENDING_RUNTIME_TIMEOUT:-300}
stall_seconds=${PENDING_RUNTIME_STALL_TIMEOUT:-120}

case_names=(forward)
for case_file in "$source_root"/cases/*/scenario.nut
do
	[ -f "$case_file" ] || continue
	case_names+=("$(basename "$(dirname "$case_file")")")
done
pair_case_names=()
for case_file in "$source_root"/pair-cases/*/scenario.nut
do
	[ -f "$case_file" ] || continue
	pair_case_names+=("$(basename "$(dirname "$case_file")")")
	case_names+=("$(basename "$(dirname "$case_file")")")
done
choose_case_names=()
for case_file in "$source_root"/choose-cases/*/scenario.nut
do
	[ -f "$case_file" ] || continue
	choose_case_names+=("$(basename "$(dirname "$case_file")")")
	case_names+=("$(basename "$(dirname "$case_file")")")
done

# Optional subset. Reject unknown names so that a renamed case cannot make a
# targeted run silently do nothing.
if [ -n "${PENDING_RUNTIME_CASES:-}" ]
then
	selected_cases=()
	for selected_case in $PENDING_RUNTIME_CASES
	do
		known=0
		for known_case in "${case_names[@]}"
		do
			if [ "$selected_case" = "$known_case" ]
			then
				known=1
				break
			fi
		done
		if [ "$known" -eq 0 ]
		then
			echo "Unknown case in PENDING_RUNTIME_CASES: $selected_case" >&2
			exit 2
		fi
		selected_cases+=("$selected_case")
	done
	case_names=("${selected_cases[@]}")
fi

stage_dir=""
game_pid=""
log_file=""

stop_game()
{
	if [ -n "$game_pid" ]
	then
		kill "$game_pid" 2>/dev/null || true
		wait "$game_pid" 2>/dev/null || true
		game_pid=""
	fi
}

cleanup_stage()
{
	if [ -n "$stage_dir" ]
	then
		rm -rf "$stage_dir"
		stage_dir=""
	fi
}

cleanup()
{
	stop_game
	cleanup_stage
}
trap cleanup EXIT


# Kills the run and waits for it. The job specification addresses the whole
# background process, so the game does not outlive the runner.
run_case()
{
	local case_name=$1
	local case_scenario
	local harness_file=$source_root/harness.nut
	local debug_level=2

	if [ "$case_name" = "forward" ]
	then
		case_scenario="$source_root/scenario.nut"
	elif [ -f "$source_root/cases/$case_name/scenario.nut" ]
	then
		case_scenario="$source_root/cases/$case_name/scenario.nut"
	elif [ -f "$source_root/pair-cases/$case_name/scenario.nut" ]
	then
		case_scenario="$source_root/pair-cases/$case_name/scenario.nut"
		harness_file=$source_root/pair-harness.nut
	elif [ -f "$source_root/choose-cases/$case_name/scenario.nut" ]
	then
		case_scenario="$source_root/choose-cases/$case_name/scenario.nut"
		harness_file=$source_root/choose-harness.nut
		if [ "$case_name" = "target-unreachable" ]
		then
			debug_level=3
		fi
	else
		echo "Case $case_name: scenario file not found"
		return 1
	fi

	if [ ! -f "$case_scenario" ]
	then
		echo "Case $case_name: scenario file not found: $case_scenario"
		return 1
	fi

	stage_dir="$scenario_root/pending-runtime-$case_name"
	log_file="$workdir/pending-runtime-$case_name.log"
	rm -rf "$stage_dir"
	mkdir -p "$stage_dir" || return 1
	cp "$harness_file" "$source_root"/*.sve "$stage_dir/" || return 1
	cp "$case_scenario" "$stage_dir/scenario.nut" || return 1

	rm -f "$log_file"
	(
		cd "$workdir" || exit 1
		exec "$binary" -use_workdir -objects "$objects" -lang en \
			-scenario "pending-runtime-$case_name" -debug "$debug_level" -nomidi
	) > "$log_file" 2>&1 &
	# The subshell uses exec, so its process identifier is the game process.
	game_pid=$!

	local result=1
	local start_time=$SECONDS
	local last_size=-1
	local last_progress=$SECONDS

	while :
	do
		sleep 1

		if grep -qF "RUNTIME CASE $case_name PASS" "$log_file" 2>/dev/null
		then
			if [ "$case_name" = "target-unreachable" ] && ! grep -qF "check station geometry and one-way signs" "$log_file"
			then
				echo "Case $case_name: FAILED (missing unusable-geometry warning) ($((SECONDS - start_time))s)"
				result=1
			else
				echo "Case $case_name: PASS ($((SECONDS - start_time))s)"
				result=0
			fi
			break
		fi

		if grep -qF "RUNTIME CASE $case_name FAIL" "$log_file" 2>/dev/null
		then
			echo "Case $case_name: FAILED ($((SECONDS - start_time))s)"
			grep -m 1 -F "RUNTIME CASE $case_name FAIL" "$log_file" || true
			result=1
			break
		fi

		# "error [suspended] calling start" is the normal first suspension of
		# the scenario script for its first tool call, so it is not matched by
		# this check. Real script errors are logged as "Script: Error:" lines.
		if grep -q -e 'Script: Error' -e '</error>' "$log_file" 2>/dev/null
		then
			echo "Case $case_name: FAILED (script error) ($((SECONDS - start_time))s)"
			grep -m 1 -e 'Script: Error' -e '</error>' "$log_file" || true
			result=1
			break
		fi

		if ! kill -0 "$game_pid" 2>/dev/null
		then
			echo "Case $case_name: FAILED (process exited before printing a result) ($((SECONDS - start_time))s)"
			result=1
			break
		fi

		# The log grows while the fixture and the simulation make progress.
		local size
		size=$(wc -c < "$log_file" 2>/dev/null || echo 0)
		if [ "$size" != "$last_size" ]
		then
			last_size=$size
			last_progress=$SECONDS
		elif (( SECONDS - last_progress >= stall_seconds ))
		then
			echo "Case $case_name: FAILED (no output progress for ${stall_seconds} seconds) ($((SECONDS - start_time))s)"
			result=1
			break
		fi

		if (( SECONDS - start_time >= timeout_seconds ))
		then
			echo "Case $case_name: FAILED (timed out after ${timeout_seconds} seconds) ($((SECONDS - start_time))s)"
			result=1
			break
		fi
	done

	stop_game

	if [ "$result" -ne 0 ]
	then
		echo "Log: $log_file"
		grep -E 'RUNTIME CASE|Y DIVISION|Script: Error|</error>|FATAL|ASSERT' "$log_file" | tail -n 20 || true
	else
		rm -f "$log_file"
	fi

	cleanup_stage
	return "$result"
}


failed_cases=()

for case_name in "${case_names[@]}"
do
	echo "=== Pending-runtime case: $case_name ==="
	if ! run_case "$case_name"
	then
		failed_cases+=("$case_name")
	fi
done

if [ "${#failed_cases[@]}" -ne 0 ]
then
	echo "Pending-runtime cases failed: ${failed_cases[*]}"
	exit 1
fi

echo "All pending-runtime cases passed."
exit 0
