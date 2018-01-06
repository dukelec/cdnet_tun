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

uint8_t local_ip[16] = {0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

int ip2cdnet(cdnet_intf_t *n_intf, cdnet_packet_t *n_pkt,
        const uint8_t *ip_dat, int ip_len)
{
    struct ipv6 *ipv6 = (struct ipv6 *)ip_dat;
    uint8_t *src_ip = ipv6->src_ip.__in6_u.__u6_addr8;
    uint8_t *dst_ip = ipv6->dst_ip.__in6_u.__u6_addr8;

    if (ipv6->version != 6) {
        d_error("< ip: version != 6: %d\n", ipv6->version);
        return -1;
    }

    d_verbose("< ip: checksum: %04x\n",
            tcp_udp_v6_checksum(&ipv6->src_ip, &ipv6->dst_ip,
                    ipv6->next_header, ip_dat + 40, ntohs(ipv6->payload_len)));

    // avoid global unicast and unspecified address
    if ((*src_ip & 0xfc) != 0xfc || (*dst_ip & 0xfc) != 0xfc) {
        d_debug("< ip: skip: src_ip[0]: %02x, dst_ip[0]: %02x\n",
                *src_ip, *dst_ip);
        return -1;
    }
    n_pkt->src_mac = *(src_ip + 15);
    if (n_pkt->src_mac != n_intf->mac) {
        d_error("< ip: src_mac != intf->mac\n");
        return -1;
    }
    if (*dst_ip == 0xff) {
        n_pkt->dst_mac = 0xff;
        if (*(dst_ip + 12) != 0) {
            d_debug("< ip: skip solicited multicast\n");
            return -1;
        }
        d_debug("< ip: multicast (treat as broadcast)\n");
    } else {
        n_pkt->dst_mac = *(dst_ip + 15);
        if (n_pkt->dst_mac == n_intf->mac || n_pkt->dst_mac == 0xff) {
            d_error("< ip: wrong dst_mac: %02x\n", n_pkt->dst_mac);
            return -1;
        }
    }

    n_pkt->is_local = true;
    n_pkt->is_multicast = false;
    n_pkt->in_fragment = false;
    n_pkt->is_compressed = false;

    if (ipv6->next_header == IPPROTO_UDP) {
        struct udp *udp = (struct udp *)(ip_dat + 40);
        n_pkt->pkt_type = PKT_TYPE_UDP;
        n_pkt->src_port = ntohs(udp->src_port);
        n_pkt->dst_port = ntohs(udp->dst_port);
        n_pkt->dat_len = ntohs(udp->len) - 8; // 8: udp header
        memcpy(n_pkt->dat, ip_dat + 40 + 8, n_pkt->dat_len);
        d_verbose("< udp port: %d -> %d, dat_len: %d\n",
                n_pkt->src_port, n_pkt->dst_port, n_pkt->dat_len);
        return 0;

    } else if (ipv6->next_header == IPPROTO_TCP) {
        struct tcp *tcp = (struct tcp *)(ip_dat + 40);
        n_pkt->pkt_type = PKT_TYPE_TCP;
        n_pkt->src_port = ntohs(tcp->src_port);
        n_pkt->dst_port = ntohs(tcp->dst_port);
        n_pkt->dat_len = ntohs(ipv6->payload_len) - 4; // 4: src & dst port
        memcpy(n_pkt->dat, ip_dat + 40 + 4, n_pkt->dat_len);
        d_verbose("< tcp port: %d -> %d, dat_len: %d\n",
                n_pkt->src_port, n_pkt->dst_port, n_pkt->dat_len);
        return 0;

    } else if (ipv6->next_header == IPPROTO_ICMPV6) {
        struct icmpv6 *icmp = (struct icmpv6 *)(ip_dat + 40);
        n_pkt->pkt_type = PKT_TYPE_ICMP;
        n_pkt->src_port = icmp->type;
        n_pkt->dst_port = icmp->code;
        n_pkt->dat_len = ntohs(ipv6->payload_len) - 4; // 4: icmp header
        memcpy(n_pkt->dat, ip_dat + 40 + 4, n_pkt->dat_len);
        d_verbose("< icmp: type: %d; code: %d\n", icmp->type, icmp->code);
        return 0;

    } else if (ipv6->next_header == IPPROTO_HOPOPTS) {
        d_warn("< skip Hop-by-Hop\n");
        return -1;
    } else {
        d_warn("< un-support header!!!\n");
        return -1;
    }

    return -1;
}

int cdnet2ip(cdnet_intf_t *n_intf, cdnet_packet_t *n_pkt,
        uint8_t *ip_dat, int *ip_len)
{
    struct ipv6 *ipv6 = (struct ipv6 *)ip_dat;
    uint8_t *src_ip = ipv6->src_ip.__in6_u.__u6_addr8;
    uint8_t *dst_ip = ipv6->dst_ip.__in6_u.__u6_addr8;

    ipv6->version = 6;
    ipv6->traffic_class_hi = 0;
    ipv6->traffic_class_lo = 0;
    ipv6->flow_label_hi = 0;
    ipv6->flow_label_lo = htons(0);

    ipv6->hop_limit = 255;

    if (!n_pkt->is_local || n_pkt->in_fragment
            || n_pkt->is_compressed || n_pkt->in_fragment
            || n_pkt->is_multicast) {
        d_error("> ip: un-support package\n");
        return -1;
    }

    memcpy(src_ip, local_ip, 15);
    *(src_ip + 15) = n_pkt->src_mac;
    if (n_pkt->src_mac == n_intf->mac) {
        d_error("> ip: src_mac == intf->mac\n");
        return -1;
    }
    if (n_pkt->src_mac == 255) {
        d_error("> ip: src_mac == 255\n");
        return -1;
    }
    memcpy(dst_ip, local_ip, 15);
    if (n_pkt->dst_mac == 255) {
        *src_ip = 0xff;
        *(src_ip + 1) = 0x02;
        *(src_ip + 15) = 0x01;
        d_debug("> ip: broadcast\n");
    } else {
        *(src_ip + 15) = n_pkt->dst_mac;
        if (n_pkt->dst_mac != n_intf->mac) {
            d_error("> ip: dst_mac != intf->mac\n");
            return -1;
        }
    }

    if (n_pkt->pkt_type == IPPROTO_UDP) {
        struct udp *udp = (struct udp *)(ip_dat + 40);
        ipv6->next_header = PKT_TYPE_UDP;
        udp->src_port = htons(n_pkt->src_port);
        udp->dst_port = htons(n_pkt->dst_port);
        udp->check = 0;
        udp->len = htons(n_pkt->dat_len + 8);
        ipv6->payload_len = udp->len;
        *ip_len = n_pkt->dat_len + 8 + 40;

        memcpy(ip_dat + 40 + 8, n_pkt->dat, n_pkt->dat_len);

        udp->check = tcp_udp_v6_checksum(&ipv6->src_ip, &ipv6->dst_ip,
                ipv6->next_header, ip_dat + 40, ntohs(ipv6->payload_len));

        d_verbose("> udp port: %d -> %d, dat_len: %d, cksum: %04x\n",
                n_pkt->src_port, n_pkt->dst_port, n_pkt->dat_len, udp->check);
        return 0;

    } else if (n_pkt->pkt_type == IPPROTO_TCP) {
        struct tcp *tcp = (struct tcp *)(ip_dat + 40);
        ipv6->next_header = PKT_TYPE_TCP;
        tcp->src_port = htons(n_pkt->src_port);
        tcp->dst_port = htons(n_pkt->dst_port);
        tcp->check = 0;
        ipv6->payload_len = htons(n_pkt->dat_len + 4); // 4: src & dst port
        *ip_len = n_pkt->dat_len + 4 + 40;

        memcpy(ip_dat + 40 + 4, n_pkt->dat, n_pkt->dat_len);

        tcp->check = tcp_udp_v6_checksum(&ipv6->src_ip, &ipv6->dst_ip,
                ipv6->next_header, ip_dat + 40, ntohs(ipv6->payload_len));

        d_verbose("> tcp port: %d -> %d, dat_len: %d, cksum: %04x\n",
                n_pkt->src_port, n_pkt->dst_port, n_pkt->dat_len, tcp->check);
        return 0;

    } else if (n_pkt->pkt_type == IPPROTO_ICMP) {
        struct icmpv6 *icmp = (struct icmpv6 *)(ip_dat + 40);
        ipv6->next_header = PKT_TYPE_ICMP;
        icmp->type = n_pkt->src_port;
        icmp->code = n_pkt->dst_port;
        icmp->checksum = 0;
        ipv6->payload_len = htons(n_pkt->dat_len + 4);
        *ip_len = n_pkt->dat_len + 4 + 40;

        memcpy(ip_dat + 40 + 4, n_pkt->dat, n_pkt->dat_len);

        icmp->checksum = tcp_udp_v6_checksum(&ipv6->src_ip, &ipv6->dst_ip,
                ipv6->next_header, ip_dat + 40, ntohs(ipv6->payload_len));

        d_verbose("> icmp type: %d, code: %d, dat_len: %d, cksum: %04x\n",
                n_pkt->src_port, n_pkt->dst_port, n_pkt->dat_len, icmp->checksum);
        return 0;

    } else {
        d_error("> un-support cdnet packet\n");
        return -1;
    }

    return -1;
}
