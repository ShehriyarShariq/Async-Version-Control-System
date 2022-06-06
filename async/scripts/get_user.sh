#!/bin/bash

userCredFileName="$HOME/bin/async/usr/async_user_cred.txt"
userLogInCheckFileName="$HOME/bin/async/usr/async_user_login_check.txt"

email=""
pass=""
while read line; do
	if [[ $line == *"email="* ]]; then
		email=$(echo $line | cut -d '=' -f 2)
	fi
	if [[ $line == *"pass="* ]]; then
		pass=$(echo $line | cut -d '=' -f 2)
	fi
done < $userCredFileName

isLoggedIn="0"
while read line; do
	isLoggedIn=$line
done < $userLogInCheckFileName

echo $isLoggedIn
echo $email
echo $pass
