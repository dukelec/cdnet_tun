#!/bin/bash

if [ $UID -ne 0 ]; then echo "restart as root"; sudo "$0"; exit; fi
cd "$(dirname "$0")"
trap 'kill $(jobs -p)' EXIT


mac="0"
unique_self="fd00::ff:fe00:0"


./cdnet_tun --mac="$mac" --unique_self="$unique_self" &

echo "bring tun0 to up, and set ip to it"

ip link set tun0 up
rm_ip=$(ip -6 -br addr show dev tun0)
IFS=' ' read -r -a array <<< "$rm_ip"
rm_ip="${array[2]}"
[ "$rm_ip" != "" ] && ip addr del "$rm_ip" dev tun0
ip addr add "$unique_self/64" dev tun0
#ip addr add 192.168.100.1/24 dev tun0

echo "set ip done:"
ip -6 -br addr show dev tun0

# type ctrl-c to exit
sleep infinity

