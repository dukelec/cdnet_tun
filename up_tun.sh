#!/bin/bash

cd "$(dirname "$0")"


# options may be followed by one colon to indicate they have a required argument
if ! options=$(getopt -o '' -l dev:,dev-type:,intn:,tty-baud:,port-offset:,router6:,tun:,gateway -- "$@")
then
    exit 1
fi

eval set -- "$options"

while [ $# -gt 0 ]
do
    case $1 in
    --dev) dev_name="$2"; shift ;;
    --dev-type) dev_type="$2"; shift ;;
    --intn) intn="$2"; shift ;;
    --tty-baud) tty_baud="$2"; shift ;;
    --port-offset) port_offset="$2"; shift ;;
    --router6) router6="$2"; shift ;;
    --tun) tun="$2"; shift ;;
    --gateway) gateway=1 ;;
    (--) shift; break;;
    (*) echo "Incorrect parameter: $1"; exit 1;;
    esac
    shift
done

[ "$tun" == "" ] && tun="tun0"

# fdcd:: is a hand picked ULA prefix, see Readme.md before changing it
self6="fdcd::80:00" # 80:00:00
self6_l0="fdcd::00" # 00:00:00


# Only the tun device itself needs privilege. It is created persistent and
# owned by the invoking user, so cdnet_tun can attach to it as a normal user
# and the process that parses bus input never runs as root.

SUDO=""
[ "$(id -u)" -ne 0 ] && SUDO="sudo"

if ! ip link show "$tun" > /dev/null 2>&1; then
    echo "create $tun, owner: $(id -un)"
    $SUDO ip tuntap add mode tun "$tun" user "$(id -un)" || exit 1
fi

$SUDO ip link set "$tun" up || exit 1
$SUDO ip -6 addr flush dev "$tun" scope global
$SUDO ip addr add "$self6/64" dev "$tun" || exit 1
$SUDO ip addr add "$self6_l0/64" dev "$tun" || exit 1

if [ "$router6" != "" ]; then
    echo "add default gw: $router6"
    $SUDO ip -6 route replace default via "$router6" dev "$tun" || exit 1
fi

echo "set ip6 done:"
ip -6 -br addr show dev "$tun"


params="--self6=$self6 --tun=$tun"

[ "$dev_type" == "" ] && dev_type="tty"
params="$params --dev-type=$dev_type"
if [ "$dev_type" == "spi" ]; then
    [ "$intn" == "" ] && intn="25"
    params="$params --intn=$intn"
fi
[ "$dev_name" != "" ] && params="$params --dev=$dev_name"
[ "$port_offset" != "" ] && params="$params --port-offset=$port_offset"
[ "$router6" != "" ] && params="$params --router6=$router6"
[ "$gateway" != "" ] && params="$params --gateway"
[ "$tty_baud" != "" ] && params="$params --tty-baud=$tty_baud"

# runs in the foreground as the invoking user, type ctrl-c to exit
echo "invoke: ./cdnet_tun $params"
exec ./cdnet_tun $params
