#!/bin/bash

if [ $UID -ne 0 ]; then echo "restart as root"; sudo "$0" "$@"; exit; fi
cd "$(dirname "$0")"
trap 'kill $(jobs -p)' EXIT

if [ "$1" != "" ]; then
  id="$1"
else
  id="1"
fi

unique_self="fd00::ff:fe00:$id"
unique_self_cd="fd00::cd00:0:ff:fe00:$id"
unique_self_cf="fd00::cf00:0:ff:fe00:$id"
self4="192.168.44.$id"

if [ "$id" == "1" ]; then
  ./cdnet_tun --mac="$id" --unique_self="$unique_self" --self4="$self4" &
else
  router4="192.168.44.1"
  ./cdnet_tun --mac="$id" --unique_self="$unique_self" --self4="$self4" --router4="$router4" &
fi



sleep 0.1
echo "bring tun0 to up, and set ip to it"

ip link set tun0 up
rm_ip=$(ip -6 -br addr show dev tun0)
IFS=' ' read -r -a array <<< "$rm_ip"
rm_ip="${array[2]}"
[ "$rm_ip" != "" ] && ip addr del "$rm_ip" dev tun0
ip addr add "$unique_self/64" dev tun0
ip addr add "$unique_self_cd/64" dev tun0
ip addr add "$unique_self_cf/64" dev tun0
ip addr add "$self4/24" dev tun0

if [ "$id" != "1" ]; then
  echo "add default gw: $router4"
  route add default gw "$router4"
fi

echo "set ip6 done:"
ip -6 -br addr show dev tun0
echo "set ip4 done:"
ip -4 -br addr show dev tun0

# type ctrl-c to exit
sleep infinity

