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
	echo -1
else
	reposDir="$HOME/bin/async_server/usr_repos"
	if [[ ! -d "$reposDir" ]]; then
		mkdir $reposDir
	fi

	userReposFileName="$reposDir/$1"

	if [[ ! -f "$userReposFileName" ]]; then
		> $userReposFileName
	fi

	repoExists=0
	while read line; do
		if [[ $line == *"$2"* ]]; then
			repoExists=1
			break
		fi
	done < $userReposFileName

	if [[ $repoExists == 1 ]]; then
		echo -2
	else
		echo "$2 $3" >> $userReposFileName
		echo 0
	fi
fi
