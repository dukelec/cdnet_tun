echo "bring tun0 to up, and set ip to it"

ip link set tun0 up
rm_ip=$(ip -6 -br addr show dev tun0)
IFS=' ' read -r -a array <<< "$rm_ip"
rm_ip="${array[2]}"
ip addr del "$rm_ip" dev tun0
ip addr add fc00::00/64 dev tun0

echo "set ip done:"
ip -6 -br addr show dev tun0

