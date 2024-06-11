/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <d@d-l.io>
 */

#ifndef __MAIN_H__
#define __MAIN_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <getopt.h>

#include "tun.h"
#include "ip.h"
#include "cdnet.h"
#include "cdbus_uart.h"
#include "cd_args.h"
#include "cd_debug.h"

#define FRAME_MAX   200

int cdctl_spi_wrapper_init(const char *dev_name, list_head_t *free_head, int intn);
void cdctl_spi_wrapper_task(void);

int cdbus_tty_wrapper_init(const char *dev_name, list_head_t *free_head);
void cdbus_tty_wrapper_task(void);

int linux_dev_wrapper_init(const char *dev_name, list_head_t *free_head);
void linux_dev_wrapper_task(void);

int ip2cdnet(cdn_pkt_t *pkt, const uint8_t *ip_dat, int ip_len);
int cdnet2ip(cdn_pkt_t *pkt, uint8_t *ip_dat, int *ip_len);

extern struct in6_addr *ipv6_self;
extern struct in6_addr *default_router6;
extern bool has_router6;

extern cd_dev_t *cd_dev;
extern list_head_t *cd_rx_head;

#endif
