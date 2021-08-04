#!/bin/bash

SERVER_ADMIN_PW=
START_CMD="./simutrans-extended -server -objects Pak128.Britain-Ex/ -server_admin_pw $SERVER_ADMIN_PW $@ > >(tee -i >(log >> ./server.log) ) 2> >(tee -i >(log >> ./server.err) >&2)"

log(){
	while IFS= read -r line
	do
		if [ -z "$line" ]
		then
			echo $line
		else
			printf '%s: %s\n' "$(date)" "$line"
		fi
	done
}
export -f log

#redirect output to file
exec > >(tee >(log >> ./restart.log))
exec 2>&1

set -e

printf "\nServer Restarting...\n"
#stop if running
if netstat -tln | tr -s ' ' | cut -d ' ' -f 4 | grep -q 13353
then
	echo "Server Stopping..."
	./nettool -q -s localhost -p $SERVER_ADMIN_PW say "server restarting in 1 minute"
	./nettool -q -s localhost -p $SERVER_ADMIN_PW force-sync
	sleep 60
	./nettool -q -s localhost -p $SERVER_ADMIN_PW shutdown
		
	echo "Creating savegame backup..."
	cp ~/simutrans/server13353-network.sve ~/simutrans/save/$(date +"%Y%m%d%H").sve
else
	echo "There was no Server running."
fi

while screen -list | grep -q dome_simutrans
do
	sleep 5
done

echo "Server updating..."
java -jar ./Nightly\ Updater\ V2.jar -cl
wget -nv -O simutrans-extended http://bridgewater-brunel.me.uk/downloads/nightly/linux-x64/command-line-server-build/simutrans-extended
chmod +x ./simutrans-extended


echo "Server starting..."
echo "Using cmdline: $START_CMD"
printf "\nServer starting...\n" | log >> ./server.log

screen -d -m -S dome_simutrans bash -c "$START_CMD"
