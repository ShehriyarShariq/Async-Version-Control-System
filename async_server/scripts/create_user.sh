#!/bin/bash

dbFileName="$HOME/bin/async_server/db/users_db.txt"

userExists=0
while read line; do
	if [[ $line == *"$1"* ]]; then
		userExists=1
		break
	fi
done < $dbFileName

if [[ $userExists == 0 ]]; then 
	id=$(echo -n "$1 $2" | sha1sum | awk '{print $1}')
	echo "$id $1 $2" >> $dbFileName
	echo 0
else
	echo -2
fi
