/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <d@d-l.io>
 */

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "main.h"
#include "ip.h"
#include "ip_checksum.h"

// ipv6:
//   map 3 bytes CDNET address format to ipv6 last 3 bytes

static struct in6_addr _ipv6_self = {0};
static struct in6_addr _default_router6 = {0};

struct in6_addr *ipv6_self = &_ipv6_self;
struct in6_addr *default_router6 = &_default_router6;
bool has_router6 = false;
bool gateway = false;
uint16_t port_offset = 0xcd00;


int ip2cdnet(cdn_pkt_t *pkt, const uint8_t *ip_dat, int ip_len)
{
    struct ipv6 *ipv6 = (struct ipv6 *)ip_dat;

    if (ipv6->version != 6) {
        d_error("< ip: wrong ip version: %d\n", ipv6->version);
        return -1;
    }
    if (IN6_IS_ADDR_UNSPECIFIED(&ipv6->src_ip)) {
        d_verbose("< ip: skip UNSPECIFIED ADDR...\n");
        return -1;
    }
    if (IN6_IS_ADDR_MULTICAST(&ipv6->dst_ip)) {
        d_verbose("< ip: skip un-support multicast...\n");
        return -1;
    }

    if (memcmp(ipv6->dst_ip.s6_addr, ipv6_self->s6_addr, 13) != 0) {
        d_debug("< ip: /104 not match, skip...\n");
        return -1;
    }
    // type byte: 00 level 0, 80 level 1 (a0 is accepted as an alias of it),
    // 90 local multicast, b0 cross net multicast
    uint8_t type = ipv6->dst_ip.s6_addr[13];
    if (type != 0x00 && type != 0x80 && type != 0xa0 && type != 0x90 && type != 0xb0) {
        d_debug("< ip: cdnet match failed, skip...\n");
        return -1;
    }

    if (gateway) {
        // the sending host's own address picks its mac, so that replies from
        // the bus are addressed back to it instead of to us
        if (memcmp(ipv6->src_ip.s6_addr, ipv6_self->s6_addr, 13) != 0) {
            d_debug("< ip: src /104 not match, skip...\n");
            return -1;
        }
        pkt->_s_mac = ipv6->src_ip.s6_addr[15];
        pkt->src.addr[1] = ipv6->src_ip.s6_addr[14];
    } else {
        pkt->_s_mac = ipv6_self->s6_addr[15];
        pkt->src.addr[1] = ipv6_self->s6_addr[14];
    }
    pkt->src.addr[2] = pkt->_s_mac;

    pkt->dst.addr[1] = ipv6->dst_ip.s6_addr[14];
    pkt->dst.addr[2] = ipv6->dst_ip.s6_addr[15];

    if (type == 0x00) {
        // l0 local link
        pkt->src.addr[0] = 0x00;
        pkt->dst.addr[0] = 0x00;
        pkt->_d_mac = pkt->dst.addr[2];

    } else if (type == 0x90 || type == 0xb0) {
        // l1 multicast, the scope is in the type byte
        pkt->src.addr[0] = type & 0xa0;
        pkt->dst.addr[0] = type;
        pkt->_d_mac = pkt->dst.addr[2];

    } else if (ipv6->dst_ip.s6_addr[14] == ipv6_self->s6_addr[14]) {
        // l1 local link
        pkt->src.addr[0] = 0x80;
        pkt->dst.addr[0] = 0x80;
        pkt->_d_mac = pkt->dst.addr[2];

    } else {
        // l1 unique local
        pkt->src.addr[0] = 0xa0;
        pkt->dst.addr[0] = 0xa0;

        if (!has_router6) {
            d_debug("< ip: no router, skip...\n");
            return -1;
        }
        pkt->_d_mac = default_router6->s6_addr[15];
    }

    if (ipv6->next_header != IPPROTO_UDP) {
        d_warn("< ip: not UDP, skip...\n");
        return -1;
    }

    struct udp *udp = (struct udp *)(ip_dat + 40);
    if (ntohs(udp->src_port) < port_offset) {
        d_warn("< ip: udp src_port < port_offset, skip...\n");
        return -1;
    }
    pkt->src.port = ntohs(udp->src_port) - port_offset;
    pkt->dst.port = ntohs(udp->dst_port);

    int dat_len = ntohs(udp->len) - 8; // 8: udp header
    if (dat_len < 0 || dat_len > ip_len - 48) { // 48: ipv6 + udp header
        d_warn("< ip: bad udp len: %d, ip_len: %d, skip...\n", ntohs(udp->len), ip_len);
        return -1;
    }

    // frame: src, dst, len, hdr..., dat..., crc_l, crc_h
    // so dat_len <= CD_FRAME_SIZE - 5 - hdr_size, and the len byte must not overflow
    int hdr_size = cdn_hdr_size_pkt(pkt);
    int max_dat = min(CD_FRAME_SIZE - 5, 255) - hdr_size;
    if (dat_len > max_dat) {
        d_warn("< ip: udp dat_len %d > %d, skip...\n", dat_len, max_dat);
        return -1;
    }

    pkt->len = dat_len;
    pkt->dat = pkt->frm->dat + 3 + hdr_size;
    memcpy(pkt->dat, ip_dat + 40 + 8, pkt->len);
    d_verbose("< ip2cdnet: udp port: %d - %d -> %d, dat_len: %d\n",
            ntohs(udp->src_port), port_offset, pkt->dst.port, pkt->len);
    return 0;
}

int cdnet2ip(cdn_pkt_t *pkt, uint8_t *ip_dat, int *ip_len)
{
    struct ipv6 *ipv6 = (struct ipv6 *)ip_dat;
    struct udp *udp = (struct udp *)(ip_dat + 40);

    if (pkt->dst.port + port_offset > 0xffff) {
        d_warn("> cdnet: dst_port %d + port_offset > 0xffff, skip...\n", pkt->dst.port);
        return -1;
    }

    ipv6->version = 6;
    ipv6->traffic_class_hi = 0;
    ipv6->traffic_class_lo = 0;
    ipv6->flow_label_hi = 0;
    ipv6->flow_label_lo = htons(0);
    ipv6->hop_limit = 255;

    // a device has one level 1 address, fdcd::80:NNMM, whether it is on our
    // net or not: a0 (cross net) is folded into 80 so that a reply comes from
    // the address the request was sent to
    memcpy(ipv6->src_ip.s6_addr, ipv6_self->s6_addr, 13);
    ipv6->src_ip.s6_addr[13] = pkt->src.addr[0] & 0x80;
    ipv6->src_ip.s6_addr[14] = pkt->src.addr[1];
    ipv6->src_ip.s6_addr[15] = pkt->src.addr[2];
    if (gateway && !(pkt->dst.addr[0] & 0x10) && pkt->dst.addr[2] != 0xff) {
        // hand it to the host the frame is really addressed to, so the kernel
        // can route it on. a multicast (type 90 / b0) or a broadcast (mac ff,
        // the level 0 form) has no single owner, so it stays with us
        memcpy(ipv6->dst_ip.s6_addr, ipv6_self->s6_addr, 13);
        ipv6->dst_ip.s6_addr[13] = pkt->dst.addr[0] & 0x80;
        ipv6->dst_ip.s6_addr[14] = pkt->dst.addr[1];
        ipv6->dst_ip.s6_addr[15] = pkt->dst.addr[2];
    } else {
        memcpy(ipv6->dst_ip.s6_addr, ipv6_self->s6_addr, 16);
        if (pkt->src.addr[0] == 0)
            ipv6->dst_ip.s6_addr[13] = 0; // l0 address
    }

    ipv6->next_header = IPPROTO_UDP;
    udp->src_port = htons(pkt->src.port);
    udp->dst_port = htons(pkt->dst.port + port_offset);
    udp->check = 0;
    udp->len = htons(pkt->len + 8);
    ipv6->payload_len = udp->len;
    *ip_len = pkt->len + 8 + 40;

    memcpy(ip_dat + 40 + 8, pkt->dat, pkt->len);

    udp->check = tcp_udp_v6_checksum(&ipv6->src_ip, &ipv6->dst_ip,
            ipv6->next_header, ip_dat + 40, ntohs(ipv6->payload_len));

    d_verbose("> cdnet2ip: udp port: %d -> %d + %d, dat_len: %d, cksum: %04x\n",
            pkt->src.port, pkt->dst.port, port_offset, pkt->len, udp->check);
    return 0;
}
