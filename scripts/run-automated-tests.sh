#!/bin/bash
set -e


pushd simutrans
# -debug 1 keeps the suite's own prints and script errors but suppresses the
# per-step warning log, which dominates load and run time. -fast_forward runs
# the simulation as fast as the machine allows; the suite asserts API state, not
# wall-clock timing.
../simutrans-extended -use_workdir -objects pak128.britain-ex-nightly -lang en -scenario automated-tests -debug 1 -nomidi -nosound -fast_forward 2>&1 | ts -s | tee output.log &
pid=$!

result=1

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

	if [[ -n "$(grep 'error \[Call function failed\] calling' output.log)" ||
	      -n "$(grep 'error \[Reading / compiling script failed] calling' output.log)" ||
	      -n "$(grep '</error>' output.log)" ]]
	then
		# kill
		echo "Killing process (test failed)"
		kill %1
		result=1
		break
	fi
done

popd

exit $result
