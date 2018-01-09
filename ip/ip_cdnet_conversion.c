/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <duke@dukelec.com>
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

#include "cdnet.h"
#include "ip.h"
#include "ip_checksum.h"

// ipv4:

// ipv4_self
// default_router4
// has_router4

// ipv4 addr: 192.168.xx.xx -> first xx is net, second xx is id
// ipv4 only for level2


// ipv6:

// default_router6: route to the addr if dst_addr is for different net
// has_router6

// local link:
//   fe80::0000:0000:00ff:fe00:00xx -> level 2
//   fe80::cd00:0000:00ff:fe00:00xx -> level 0 / 1
// unique local:
//   subnet-id + iid: 00xx + 0000:00ff:fe00:00xx -> level 2
//   subnet-id + iid: cdxx + 0000:00ff:fe00:00xx -> level 0 / 1
// global unicast:
//   iid -> 0000:00ff:fe00:00xx -> level 2
//
// Notes:
//   src and dst should be same level except multicast
//   multicast level is depend on src level


static struct in_addr _ipv4_self = {0};
static struct in_addr _default_router4 = {0};

struct in_addr *ipv4_self = &_ipv4_self;
struct in_addr *default_router4 = &_default_router4;
bool has_router4 = false;


static struct in6_addr _unique_self = {0};
static struct in6_addr _global_self = {0};
static struct in6_addr _default_router6 = {0};

struct in6_addr *unique_self = &_unique_self;
struct in6_addr *global_self = &_global_self;
struct in6_addr *default_router6 = &_default_router6;
bool has_router6 = false;


static inline bool is_cdnet_link_local(struct in6_addr *addr)
{
    if (addr->s6_addr32[0] == 0x000080fe &&
            (addr->s6_addr32[1] & 0xff00ffff) == 0 &&
            addr->s6_addr32[2] == 0xff000000 &&
            (addr->s6_addr32[3] & 0x00ffffff) == 0x000000fe &&
            (addr->s6_addr[6] == 0 || addr->s6_addr[6] == 0xcd)) {
        return true;
    }
    return false;
}

static inline
bool is_cdnet_unique_local(struct in6_addr *addr, struct in6_addr *self)
{
    if ((self->s6_addr[0] & 0xfe) != 0xfc)
        return false;

    if (addr->s6_addr32[0] == self->s6_addr32[0] &&
            (addr->s6_addr32[1] & 0x0000ffff) == self->s6_addr32[1] &&
            addr->s6_addr32[2] == 0xff000000 &&
            (addr->s6_addr32[3] & 0x00ffffff) == 0x000000fe &&
            (addr->s6_addr[6] == 0 || addr->s6_addr[6] == 0xcd)) {
        return true;
    }
    return false;
}

static inline
bool is_cdnet_global_local(struct in6_addr *addr, struct in6_addr *self)
{
    if (addr->s6_addr32[0] == self->s6_addr32[0] &&
            addr->s6_addr32[1] == self->s6_addr32[1] &&
            addr->s6_addr32[2] == 0xff000000 &&
            self->s6_addr32[2] == 0xff000000 &&
            (addr->s6_addr32[3] & 0x00ffffff) == 0x000000fe &&
            (self->s6_addr32[3] & 0x00ffffff) == 0x000000fe) {
        return true;
    }
    return false;
}

static inline bool is_cdnet_multicast(struct in6_addr *addr)
{
    // support only ff02::1 and ff05::1 at now
    if ((addr->s6_addr32[0] & 0xffff00ff) == 0x000000ff &&
            addr->s6_addr32[1] == 0 &&
            addr->s6_addr32[2] == 0 &&
            addr->s6_addr32[3] == 0x01000000 &&
            (addr->s6_addr[1] == 5 || addr->s6_addr[1] == 2)) {
        return true;
    }
    return false;
}


int ip2cdnet(cdnet_intf_t *intf, cdnet_packet_t *pkt,
        const uint8_t *ip_dat, int ip_len)
{
    struct ipv6 *ipv6 = (struct ipv6 *)ip_dat;
    struct ipv4 *ipv4 = (struct ipv4 *)ip_dat;

    pkt->in_fragment = false;
    pkt->is_multi_net = false;
    pkt->is_multicast = false;
    pkt->is_compressed = false;

    if (ipv4->version == 4) {
        pkt->is_level2 = true;
        pkt->src_mac = ipv4_self->s_addr >> 24;

        if ((ipv4->dst_ip.s_addr & 0x00ffffff) ==
                (ipv4_self->s_addr & 0x00ffffff)) {
            // local net
            d_debug("< ip4: direct send...\n");
            pkt->dst_mac = ipv4->dst_ip.s_addr >> 24;
        } else if (has_router4) {
            d_debug("< ip4: send to router...\n");
            pkt->dst_mac = default_router4->s_addr >> 24;
        } else {
            d_debug("< ip4: no router, skip...\n");
            return -1;
        }
        memcpy(pkt->dat, ip_dat, ip_len);
        pkt->dat_len = ip_len;
        return 0;
    }

    if (ipv6->version != 6) {
        d_error("< ip: wrong ip version: %d\n", ipv6->version);
        return -1;
    }
    if (IN6_IS_ADDR_UNSPECIFIED(&ipv6->src_ip)) {
        d_verbose("< ip: skip UNSPECIFIED ADDR...\n");
        return -1;
    }
    if (IN6_IS_ADDR_MULTICAST(&ipv6->dst_ip) &&
            !is_cdnet_multicast(&ipv6->dst_ip)) {
        d_verbose("< ip: skip un-support multicast...\n");
        return -1;
    }

    if (is_cdnet_link_local(&ipv6->src_ip)) {
        // link local
        pkt->src_mac = ipv6->src_ip.s6_addr[15];
        pkt->is_level2 = (ipv6->src_ip.s6_addr[6] == 0);

        if (is_cdnet_link_local(&ipv6->dst_ip)) {
            d_verbose("< ip: link_local: direct...\n");
            pkt->dst_mac = ipv6->dst_ip.s6_addr[15];
        } else if (is_cdnet_multicast(&ipv6->dst_ip)) {
            d_verbose("< ip: link_local: multicast...\n");
            pkt->dst_mac = 255;
        } else {
            d_error("< ip: link_local: wrong dst addr...\n");
            return -1;
        }

    } else if (is_cdnet_unique_local(&ipv6->src_ip, unique_self)) {
        // unique local
        pkt->src_mac = unique_self->s6_addr[15];
        pkt->is_level2 = (ipv6->src_ip.s6_addr[6] == 0);
        pkt->is_multi_net = true;
        pkt->src_addr[0] = ipv6->src_ip.s6_addr[7];
        pkt->src_addr[1] = ipv6->src_ip.s6_addr[15];

        if (is_cdnet_unique_local(&ipv6->dst_ip, unique_self)) {
            pkt->dst_addr[0] = ipv6->dst_ip.s6_addr[7];
            pkt->dst_addr[1] = ipv6->dst_ip.s6_addr[15];

            if (ipv6->dst_ip.s6_addr[7] == unique_self->s6_addr[7]) {
                d_debug("< ip: unique_local: direct...\n");
                pkt->dst_mac = ipv6->dst_ip.s6_addr[15];
            } else if (has_router6) {
                d_debug("< ip: unique_local: send to router...\n");
                pkt->dst_mac = default_router6->s6_addr[15];
            } else {
                d_debug("< ip: unique_local: no router, skip...\n");
                return -1;
            }

        } else if (is_cdnet_multicast(&ipv6->dst_ip)) {
            d_verbose("< ip: unique_local: not support multicast yet...\n");
            return -1;
        } else {
            d_error("< ip: unique_local: wrong dst addr...\n");
            return -1;
        }

    } else {
        pkt->src_mac = unique_self->s6_addr[15];
        pkt->is_level2 = true;

        if (is_cdnet_multicast(&ipv6->dst_ip)) {
            d_verbose("< ip: other: multicast...\n");
            pkt->dst_mac = 255;
        } if (is_cdnet_global_local(&ipv6->dst_ip, global_self)) {
            d_verbose("< ip: other: direct...\n");
            pkt->dst_mac = ipv6->dst_ip.s6_addr[15];
        } else if (has_router6) {
            d_debug("< ip: other: send to router...\n");
            pkt->dst_mac = default_router6->s6_addr[15];
        } else {
            d_debug("< ip: other: no router, skip...\n");
            return -1;
        }
    }

    if (pkt->is_level2) {
        memcpy(pkt->dat, ip_dat, ip_len);
        pkt->dat_len = ip_len;
        return 0;
    }

    if (ipv6->next_header != IPPROTO_UDP) {
        d_warn("< ip: not UDP, skip...\n");
        return -1;
    }

    struct udp *udp = (struct udp *)(ip_dat + 40);
    pkt->src_port = ntohs(udp->src_port);
    pkt->dst_port = ntohs(udp->dst_port);
    pkt->dat_len = ntohs(udp->len) - 8; // 8: udp header
    memcpy(pkt->dat, ip_dat + 40 + 8, pkt->dat_len);
    d_verbose("< ip: l0_l1: udp port: %d -> %d, dat_len: %d\n",
            pkt->src_port, pkt->dst_port, pkt->dat_len);
    return 0;
}

int cdnet2ip(cdnet_intf_t *intf, cdnet_packet_t *pkt,
        uint8_t *ip_dat, int *ip_len)
{
    struct ipv6 *ipv6 = (struct ipv6 *)ip_dat;
    struct udp *udp = (struct udp *)(ip_dat + 40);

    if (pkt->is_level2) {
        *ip_len = pkt->dat_len;
        memcpy(ip_dat, pkt->dat, pkt->dat_len);
        return 0;
    }

    ipv6->version = 6;
    ipv6->traffic_class_hi = 0;
    ipv6->traffic_class_lo = 0;
    ipv6->flow_label_hi = 0;
    ipv6->flow_label_lo = htons(0);
    ipv6->hop_limit = 255;

    if (pkt->dst_mac == 255)
        inet_pton(AF_INET6, "ff02::01", ipv6->dst_ip.s6_addr);

    if (pkt->is_multi_net) { // unique local
        memcpy(ipv6->src_ip.s6_addr, unique_self->s6_addr, 16);
        ipv6->src_ip.s6_addr[6] = 0xcd;
        ipv6->src_ip.s6_addr[7] = pkt->src_addr[0];
        ipv6->src_ip.s6_addr[15] = pkt->src_addr[1];

        if (pkt->is_multicast) {
            d_error("> ip: multicast not support yet\n");
            return -1;
        }

        if (pkt->dst_mac != 255) {
            memcpy(ipv6->dst_ip.s6_addr, unique_self->s6_addr, 16);
            ipv6->dst_ip.s6_addr[6] = 0xcd;
            ipv6->dst_ip.s6_addr[7] = pkt->dst_addr[0];
            ipv6->dst_ip.s6_addr[15] = pkt->dst_addr[1];
        }
    } else { // local link
        inet_pton(AF_INET6, "fe80::cd00:0:ff:fe00:0", ipv6->src_ip.s6_addr);
        ipv6->src_ip.s6_addr[15] = pkt->src_mac;
        if (pkt->dst_mac != 255) {
            inet_pton(AF_INET6, "fe80::cd00:0:ff:fe00:0", ipv6->dst_ip.s6_addr);
            ipv6->dst_ip.s6_addr[15] = pkt->dst_mac;
        }
    }

    ipv6->next_header = IPPROTO_UDP;
    udp->src_port = htons(pkt->src_port);
    udp->dst_port = htons(pkt->dst_port);
    udp->check = 0;
    udp->len = htons(pkt->dat_len + 8);
    ipv6->payload_len = udp->len;
    *ip_len = pkt->dat_len + 8 + 40;

    memcpy(ip_dat + 40 + 8, pkt->dat, pkt->dat_len);

    udp->check = tcp_udp_v6_checksum(&ipv6->src_ip, &ipv6->dst_ip,
            ipv6->next_header, ip_dat + 40, ntohs(ipv6->payload_len));

    d_verbose("> ip: l0_l1: udp port: %d -> %d, dat_len: %d, cksum: %04x\n",
            pkt->src_port, pkt->dst_port, pkt->dat_len, udp->check);
    return 0;
}
