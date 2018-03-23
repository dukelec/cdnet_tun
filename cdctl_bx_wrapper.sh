#!/bin/bash

if [ $UID -ne 0 ]; then echo "restart as root"; sudo "$0" "$@"; exit; fi
cd "$(dirname "$0")"
set -euo pipefail # exit on error

rstn_pin=24
intn_pin=25

if [ ! -d /sys/class/gpio/gpio$intn_pin ]; then
  echo "init intn_pin: $intn_pin"
  echo $intn_pin > /sys/class/gpio/export
  echo in > /sys/class/gpio/gpio$intn_pin/direction
fi

if [ ! -d /sys/class/gpio/gpio$rstn_pin ]; then
  echo "init rstn_pin: $rstn_pin"
  echo $rstn_pin > /sys/class/gpio/export
  echo out > /sys/class/gpio/gpio$rstn_pin/direction
fi

echo "reset cdctl_bx"
echo 0 > /sys/class/gpio/gpio$rstn_pin/value
sleep 0.1
echo 1 > /sys/class/gpio/gpio$rstn_pin/value

echo "start up_tun.sh"
./up_tun.sh --dev-type=spi --intn-pin=$intn_pin $@

echo "exit"

