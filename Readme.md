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
