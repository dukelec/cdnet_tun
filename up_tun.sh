#!/bin/bash

if [ $UID -ne 0 ]; then echo "restart as root"; sudo "$0" "$@"; exit; fi
cd "$(dirname "$0")"
trap 'kill $(jobs -p)' EXIT


# options may be followed by one colon to indicate they have a required argument
if ! options=$(getopt -o '' -l dev-type:,intn: -- "$@")
then
  exit 1
fi

eval set -- "$options"

while [ $# -gt 0 ]
do
  case $1 in
  --dev-type) dev_type="$2"; shift ;;
  --intn) intn="$2"; shift ;;
  (--) shift; break;;
  (*) echo "Incorrect parameter: $1"; exit 1;;
  esac
  shift
done

self6="fdcd::80:00" # 80:00:00

params="--self6=$self6"

[ "$dev_type" == "" ] && dev_type="tty"
[ "$dev_type" != "" ] && params="$params --dev-type=$dev_type"
[ "$intn" != "" ] && params="$params --intn=$intn_pin"

echo "invoke: ./cdnet_tun $params"
./cdnet_tun $params &


sleep 0.1
echo "bring tun0 to up, and set ip to it"

ip link set tun0 up
rm_ip=$(ip -6 -br addr show dev tun0)
IFS=' ' read -r -a array <<< "$rm_ip"
rm_ip="${array[2]}"
[ "$rm_ip" != "" ] && ip addr del "$rm_ip" dev tun0
ip addr add "$self6/64" dev tun0

if [ "$router6" != "" ]; then
  echo "add default gw: $router6"
  route add default gw "$router6"
fi

echo "set ip6 done:"
ip -6 -br addr show dev tun0

# type ctrl-c to exit
sleep infinity

