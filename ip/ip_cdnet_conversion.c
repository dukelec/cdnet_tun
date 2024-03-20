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
    if (ipv6->dst_ip.s6_addr[13] != 0x80 && ipv6->dst_ip.s6_addr[13] != 0xa0
            && ipv6->dst_ip.s6_addr[13] != 0xf0) {
        d_debug("< ip: cdnet match failed, skip...\n");
        return -1;
    }

    pkt->_s_mac = ipv6_self->s6_addr[15];
    pkt->src.addr[1] = ipv6_self->s6_addr[14];
    pkt->src.addr[2] = pkt->_s_mac;

    pkt->dst.addr[1] = ipv6->dst_ip.s6_addr[14];
    pkt->dst.addr[2] = ipv6->dst_ip.s6_addr[15];

    if (ipv6->dst_ip.s6_addr[13] == 0xf0) {
        // l1 multicast
        pkt->src.addr[0] = 0xa0;
        pkt->dst.addr[0] = 0xf0;
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
    pkt->src.port = ntohs(udp->src_port);
    pkt->dst.port = ntohs(udp->dst_port);
    pkt->len = ntohs(udp->len) - 8; // 8: udp header
    memcpy(pkt->dat, ip_dat + 40 + 8, pkt->len);
    d_verbose("< ip: l1: udp port: %d -> %d, dat_len: %d\n", pkt->src.port, pkt->dst.port, pkt->len);
    return 0;
}

int cdnet2ip(cdn_pkt_t *pkt, uint8_t *ip_dat, int *ip_len)
{
    struct ipv6 *ipv6 = (struct ipv6 *)ip_dat;
    struct udp *udp = (struct udp *)(ip_dat + 40);

    ipv6->version = 6;
    ipv6->traffic_class_hi = 0;
    ipv6->traffic_class_lo = 0;
    ipv6->flow_label_hi = 0;
    ipv6->flow_label_lo = htons(0);
    ipv6->hop_limit = 255;

    memcpy(ipv6->src_ip.s6_addr, ipv6_self->s6_addr, 13);
    cdn_set_addr(pkt->src.addr, ipv6->src_ip.s6_addr[13], ipv6->src_ip.s6_addr[14], ipv6->src_ip.s6_addr[15]);
    memcpy(ipv6->dst_ip.s6_addr, ipv6_self->s6_addr, 16);

    ipv6->next_header = IPPROTO_UDP;
    udp->src_port = htons(pkt->src.port);
    udp->dst_port = htons(pkt->dst.port);
    udp->check = 0;
    udp->len = htons(pkt->len + 8);
    ipv6->payload_len = udp->len;
    *ip_len = pkt->len + 8 + 40;

    memcpy(ip_dat + 40 + 8, pkt->dat, pkt->len);

    udp->check = tcp_udp_v6_checksum(&ipv6->src_ip, &ipv6->dst_ip,
            ipv6->next_header, ip_dat + 40, ntohs(ipv6->payload_len));

    d_verbose("> ip: l1: udp port: %d -> %d, dat_len: %d, cksum: %04x\n",
            pkt->src.port, pkt->dst.port, pkt->len, udp->check);
    return 0;
}
