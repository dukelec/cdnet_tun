#!/bin/bash

if [ $UID -ne 0 ]; then echo "restart as root"; sudo "$0" "$@"; exit; fi
cd "$(dirname "$0")"
set -euo pipefail # exit on error

intn_pin=25

if [ ! -d /sys/class/gpio/gpio$intn_pin ]; then
  echo "init intn_pin: $intn_pin"
  echo $intn_pin > /sys/class/gpio/export
  echo in > /sys/class/gpio/gpio$intn_pin/direction
  echo falling > /sys/class/gpio/gpio$intn_pin/edge
fi

#echo "start up_tun.sh"
#./up_tun.sh --dev-type=spi --intn=$intn_pin $@

echo "exit"

