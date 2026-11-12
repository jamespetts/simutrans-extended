#!/bin/bash
# Pre-runtime Y (dividing) fixture runner.
#
# This is NOT part of the default passing test suite. It builds a Y-shaped
# service through the public script API, starts the source convoy and records
# the precise outcome at the junction while the dividing runtime is absent.
#
# Exit codes:
#   0  the recorded pending junction state is unchanged
#   1  setup failed, script error, timeout or crash
#   3  the observed junction state differs from the recorded pending state
#
# Exit 3 does not by itself prove that the dividing runtime is implemented.
# The recorded pending state already includes the loose-stock convoy at the
# junction; any difference in counts, convoy ids or states produces exit 3 so
# that the fixture can be inspected and its expectation updated.
#
# The scenario directory must be available to the pakset, e.g.:
#   New-Item -ItemType Junction -Path "simutrans/pak128.Britain-Ex/scenario/pending-runtime" \
#            -Target (Resolve-Path tests/pending-runtime)
set -e


pushd simutrans
../simutrans-extended -use_workdir -objects pak128.britain-ex-nightly -lang en -scenario pending-runtime -debug 2 -nomidi 2>&1 | ts -s | tee pending-runtime-output.log &
pid=$!

result=1
timeout_seconds=${PENDING_RUNTIME_TIMEOUT:-300}
start_time=$SECONDS

while :
do
	sleep 1

	if [[ ! -d /proc/$pid/ ]]
	then
		echo "Process crashed (fixture failed)"
		result=1
		break
	fi

	if [[ -n "$(grep 'PENDING RUNTIME COMPLETE.' pending-runtime-output.log)" ]]
	then
		echo "Killing process (pending observation recorded)"
		kill %1
		if [[ -n "$(grep 'PENDING RUNTIME: OBSERVED CHANGE' pending-runtime-output.log)" ]]
		then
			result=3
		else
			result=0
		fi
		break
	fi

	if [[ -n "$(grep 'FIXTURE SETUP FAILED' pending-runtime-output.log)" ]]
	then
		echo "Killing process (fixture setup failed)"
		kill %1
		result=1
		break
	fi

	if [[ -n "$(grep 'PENDING RUNTIME: TIMEOUT' pending-runtime-output.log)" ]]
	then
		echo "Killing process (fixture timed out)"
		kill %1
		result=1
		break
	fi

	if [[ -n "$(grep 'error \[suspended\] calling' pending-runtime-output.log)" ||
	      -n "$(grep '</error>' pending-runtime-output.log)" ]]
	then
		echo "Killing process (fixture failed: script error)"
		kill %1
		result=1
		break
	fi

	if (( SECONDS - start_time >= timeout_seconds ))
	then
		echo "Killing process (fixture failed: timed out after ${timeout_seconds} seconds)"
		kill %1
		result=1
		break
	fi
done

popd

exit $result
