#!/bin/bash

if [ "$#" -ne 1 ]; then
	echo "Usage: $0 <serial_port>" >&2
	exit 1
fi

if [ ! -e "$1" ]; then
	echo "$1 does not exist" >&2
	exit 1
fi

# https://wiki.archlinux.org/title/Arduino#stty
stty -F "$1" cs8 9600 ignbrk -brkint -imaxbel -opost -onlcr -isig -icanon -iexten -echo -echoe -echok -echoctl -echoke noflsh -ixon -crtscts

echo "enter 'q' to quit."

cat "$1" &
cat_pid=$!

sleep 2

while true; do
	read -p "Frequency [Hz]> " FREQUENCY
	[ "$FREQUENCY" == "q" ] && break
	echo "$FREQUENCY" > "$1"
	sleep 0.5
done

kill -HUP "$cat_pid"

