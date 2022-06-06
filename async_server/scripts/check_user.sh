#!/bin/bash

dbFileName="$HOME/bin/async_server/db/users_db.txt"

userExists=0
userID=""
while read line; do
	if [[ $line == *"$1 $2"* ]]; then
		userExists=1
		userID=$(echo $line | cut -d ' ' -f 1)
		break
	fi
done < $dbFileName

if [[ $userExists == 1 ]]; then 
	echo $userID
else
	echo -1
fi
