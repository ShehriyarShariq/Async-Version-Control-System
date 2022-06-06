#!/bin/bash

userCredFile="$HOME/bin/async/usr/async_user_cred.txt"
userLogInCheckFile="$HOME/bin/async/usr/async_user_login_check.txt"

if [[ "$1" == "--login" ]]; then
	echo "$2" > $userLogInCheckFile
elif [[ "$1" == "--logout" ]]; then
	isLoggedIn=1
	while read line; do
		if [[ $line == "0" || $line == "" ]]; then
			isLoggedIn=0
		fi
	done < $userLogInCheckFile
	
	if [[ $isLoggedIn == 1 ]]; then
		> $userCredFile
		echo "0" > $userLogInCheckFile
		echo 0
	else
		echo -1
	fi

else
	isLoggedIn=0
	while read line; do
		if [[ $line == "1" ]]; then
			isLoggedIn=1
		fi
	done < $userLogInCheckFile
	
	if [[ $isLoggedIn == 0 ]]; then
		i=0
		emailExists=0
		passExists=0
		email=""
		pass=""
		while read line; do
			i=i+1
			if [[ $line == *"email="* ]]; then
				emailExists=1
				email=$line
			fi
			if [[ $line == *"pass="* ]]; then
				passExists=1
				pass=$line
			fi
		done < $userCredFile

		if [[ "$1" == "--email" ]]; then
			if [[ $i == 0 || $emailExists == 1 ]]; then
				echo "email=$2" > $userCredFile
			else
				echo "email=$2" >> $userCredFile
			fi
			
			if [[ $emailExists == 1 && $passExists == 1 ]]; then
				echo "$pass" >> $userCredFile
			fi

			echo 0
		elif [[ "$1" == "--password" ]]; then
			if [[ $i == 0 || $passExists == 1 ]]; then
				echo "pass=$2" > $userCredFile
			else
				echo "pass=$2" >> $userCredFile
			fi
			
			if [[ $passExists == 1 && $emailExists == 1 ]]; then
				echo "$email" >> $userCredFile
			fi

			echo 0
		fi
	else
		echo -1
	fi
fi
