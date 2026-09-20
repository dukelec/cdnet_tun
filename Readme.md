CDNET TUN
========================

The cdnet_tun tool creates a virtual network on PC for users to communicate with CDNET devices using IP/UDP programming.

Using IP/UDP programming allows multiple software to access the bus simultaneously, avoiding device occupancy concerns.

No IP/UDP protocol stack is required for MCU to communicate with a computer using CDNET.

Currently, only Linux systems are supported, but more systems will be supported in the future.


Build
------------------------

```
git submodule update --init
make                # add USE_SPI=1 to build the cdctl spi backend (needs libgpiod)
```


Run
------------------------

```
./up_tun.sh                                  # /dev/ttyACM0 by default
./up_tun.sh --dev /dev/ttyUSB0 --tty-baud 1000000
./up_tun.sh --dev-type ld --dev /dev/cdbus   # linux cdbus driver
./up_tun.sh --dev-type spi --intn 25         # cdctl over spi, needs USE_SPI=1
```

`up_tun.sh` needs `sudo` only to create and address the tun device. The device is
created persistent and owned by you, so `cdnet_tun` itself attaches to it as a
normal user: the process that parses input coming off the bus never runs as root.

Because the device persists, you can also set it up once and afterwards run the
tool directly, without any privilege at all:

```
./cdnet_tun --tun=tun0 --self6=fdcd::80:00 --dev-type=tty --dev=/dev/ttyACM0
```


Addressing
------------------------

A CDNET address is 3 bytes, `level:net:mac`, and it is mapped onto the last 3
bytes of the IPv6 address. Everything above those 3 bytes must match your own
address, which `up_tun.sh` sets to `fdcd::80:00`, that is `80:00:00`.

| IPv6 address     | CDNET      | meaning |
|------------------|------------|---------|
| `fdcd::00fe`     | `00:00:fe` | level 0, mac fe |
| `fdcd::80:00fe`  | `80:00:fe` | level 1, mac fe on our own net |
| `fdcd::80:01fe`  | `a0:01:fe` | level 1 on another net, sent to `--router6` |
| `fdcd::f0:00ff`  | `f0:00:ff` | level 1 multicast |

The level byte only has to be one of `00`, `80`, `a0` or `f0`; `00` picks level 0
and `f0` picks multicast. Between `80` and `a0` there is no difference: whether a
level 1 packet stays on the local link or is handed to the router is decided by
the net byte, by comparing it with the net byte of our own address.

The UDP port is the CDNET port, so talking to a device is just:

```python
s = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
s.bind(("fdcd::80:00", 50040))
s.sendto(b"...", ("fdcd::80:00fe", 0xcdcd))
```

See the `example` directory for complete programs.

### About the `fdcd::` prefix

`fdcd::` is a hand picked value in the ULA range. RFC 4193 asks for the 40 bit
global ID of a ULA prefix to be randomly generated, which this is not, so it
could collide with another ULA network on the same host. It is only a
convention: change `self6` and `self6_l0` at the top of `up_tun.sh` (and the
addresses your programs use) if it gets in the way.


Sharing the bus with other hosts
------------------------

By default cdnet_tun is the only endpoint: it sends with its own mac, accepts
only frames addressed to that mac, and hands everything it receives to the local
stack. `--gateway` changes that, so other machines on the LAN can use the bus as
if it were attached to them:

* a frame for mac `nn` is delivered to `<prefix>nn` instead of to us, so the
  kernel routes it on to whichever host holds that address
* the mac a packet is sent with comes from its own source address, so replies
  from the bus come back addressed to the sender
* the dst mac filter is turned off, since a gateway has to receive frames
  addressed to the other hosts

Give every host its own mac, and on the gateway add a route per host plus
forwarding. The route has to be more specific than the on-link prefix, otherwise
the packet goes straight back out of the tun device and onto the bus again:

```
# on the gateway, for a host that is mac 04 and lives at 2001:db8::2
sysctl -w net.ipv6.conf.all.forwarding=1
ip -6 route add fdcd::4/128 via 2001:db8::2 dev eth0
./up_tun.sh --gateway

# on that host: take the address, and route the bus through the gateway
ip addr add fdcd::4/128 dev lo
ip -6 route add fdcd::/64 via <gateway> dev eth0
```

Its programs then bind `fdcd::4` and talk to the bus normally, in both
directions: a device sending to mac 04 on its own reaches that host too, not
just replies to what it asked for.

Two limits worth knowing:

* a broadcast (mac `ff`) has no single owner, so it stays with the gateway
  rather than reaching every host
* only the tty backend filters in software. cdctl filters dst mac in hardware
  and `filter_m` only adds 2 macs beyond its own, and the `ld` backend leaves
  the filter to the driver; cdnet_tun warns when `--gateway` is used with them


Packet size limit
------------------------

A CDNET packet has to fit in a single CDBUS frame, so the UDP payload is limited
to what is left of the frame after the headers. There is no fragmentation.

With the shipped `CD_FRAME_SIZE` of 258, a frame carries at most 253 bytes of
CDNET packet, and the CDNET header takes 2 bytes at level 0 and 3 to 9 bytes at
level 1 (depending on the address form and on whether the ports fit in one byte
each):

| | CDNET header | max UDP payload |
|-|--------------|-----------------|
| level 0                          | 2   | 251 |
| level 1, local link              | 3~5 | 250~248 |
| level 1, routed, and multicast   | 7~9 | 246~244 |

**Keeping datagrams at 244 bytes or less is safe in every case.**

The IP stack cannot enforce this for you: IPv6 requires a link MTU of at least
1280, far above a CDBUS frame, so lowering the MTU of the tun device is not an
option (Linux drops IPv6 from an interface whose MTU goes below 1280). An
oversized datagram is therefore dropped by cdnet_tun, which reports it as:

```
W: < ip: udp dat_len 252 > 251, skip...
```

If you see that line, split the message in your application.
