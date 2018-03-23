/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <duke@dukelec.com>
 */

#ifndef __MAIN_H__
#define __MAIN_H__

#define CD_FRAME_MAX    100
#define NET_PACKET_MAX  200

extern cd_frame_t cd_frame_alloc[];
extern cdnet_packet_t net_packet_alloc[];

extern cdnet_intf_t net_proxy_intf;
int cdbus_bridge_wrapper_init(cdnet_addr_t *addr, const char *dev);
void cdbus_bridge_wrapper_task(void);

extern cdnet_intf_t net_cdctl_bx_intf;
int cdctl_bx_wrapper_init(cdnet_addr_t *addr, const char *dev, int intn);
void cdctl_bx_wrapper_task(void);


int ip2cdnet(cdnet_intf_t *n_intf, cdnet_packet_t *n_pkt,
        const uint8_t *ip_dat, int ip_len);
int cdnet2ip(cdnet_intf_t *n_intf, cdnet_packet_t *n_pkt,
        uint8_t *ip_dat, int *ip_len);


extern struct in_addr *ipv4_self;
extern struct in_addr *default_router4;
extern bool has_router4;

extern struct in6_addr *unique_self;
extern struct in6_addr *global_self;
extern struct in6_addr *default_router6;
extern bool has_router6;

#endif
