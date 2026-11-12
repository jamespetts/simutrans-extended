#!/bin/bash
set -e


pushd simutrans
../simutrans-extended -use_workdir -objects pak128.britain-ex-nightly -lang en -scenario automated-tests -debug 2 -nomidi 2>&1 | ts -s | tee output.log &
pid=$!

result=1
# Optional overall timeout in seconds; the CI job has its own 10 minute limit.
timeout_seconds=${AUTOMATED_TESTS_TIMEOUT:-600}
start_time=$SECONDS

while :
do
	sleep 1

	if [[ ! -d /proc/$pid/ ]]
	then
		# process crashed etc.
		echo "Process crashed (test failed)"
		result=1
		break
	fi

	if [[ -n "$(grep 'Tests completed successfully.' output.log)" ]]
	then
		# kill
		echo "Killing process (test succeeded)"
		kill %1
		result=0
		break
	fi

	if [[ -n "$(grep 'TEST FAILED:' output.log)" ]]
	then
		# a single test reported an assertion or script failure
		echo "Killing process (test failed: $(grep 'TEST FAILED:' output.log | head -n 1))"
		kill %1
		result=1
		break
	fi

	# The startup warning "error [suspended] calling start" also occurs when
	# the first scripted tool call suspends normally and is resumed later.
	# The overall timeout below detects a call that is never resumed.

	if [[ -n "$(grep 'error \[Call function failed\] calling' output.log)" ||
	      -n "$(grep 'error \[Reading / compiling script failed] calling' output.log)" ||
	      -n "$(grep '</error>' output.log)" ]]
	then
		# kill
		echo "Killing process (test failed: script error)"
		kill %1
		result=1
		break
	fi

	if (( SECONDS - start_time >= timeout_seconds ))
	then
		echo "Killing process (test failed: timed out after ${timeout_seconds} seconds)"
		kill %1
		result=1
		break
	fi
done

popd

exit $result
