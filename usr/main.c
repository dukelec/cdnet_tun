/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <duke@dukelec.com>
 */

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
#include "main.h"

#define BUFSIZE 2000

cd_frame_t cd_frame_alloc[CD_FRAME_MAX];
cdnet_packet_t net_packet_alloc[NET_PACKET_MAX];

// frag_pend for rx only, must be: level == CDNET_L2 && is_fragment
static list_head_t frag_pend_head = {0};


static char tun_name[20] = "";
static char cdn_dev[50] = "";
static cdnet_addr_t local_addr = { .net = 0, .mac = 0x01 };
static cdnet_intf_t *cdn_intf = NULL;
static int cdn_fd;
static void (* cdn_task)(void);

typedef enum {
    DEV_BRIDGE = 0,
    DEV_SPI
} dev_type_t;

static dev_type_t dev_type = DEV_BRIDGE;
static int intn_pin = -1;


enum OPT_IDX {
    OPT_MAC = 1000,
    OPT_SELF4,
    OPT_ROUTER4,
    OPT_UNIQUE_SELF,
    OPT_GLOBAL_SELF,
    OPT_ROUTER,
    OPT_TUN,
    OPT_DEV_TYPE,
    OPT_DEV,
    OPT_INTN_PIN
};

static struct option longopts[] = {
        { "mac",            required_argument, NULL, OPT_MAC },
        { "self4",          required_argument, NULL, OPT_SELF4 },
        { "router4",        required_argument, NULL, OPT_ROUTER4 },
        { "unique-self",    required_argument, NULL, OPT_UNIQUE_SELF },
        { "global-self",    required_argument, NULL, OPT_GLOBAL_SELF },
        { "router",         required_argument, NULL, OPT_ROUTER },
        { "tun",            required_argument, NULL, OPT_TUN },
        { "dev-type",       required_argument, NULL, OPT_DEV_TYPE },
        { "dev",            required_argument, NULL, OPT_DEV },
        { "intn-pin",       required_argument, NULL, OPT_INTN_PIN },
        { 0, 0, 0, 0 }
};


int main(int argc, char *argv[]) {

    int tun_fd;
    uint8_t tmp_buf[BUFSIZE];

    while (true) {
        int option = getopt_long(argc, argv, "", longopts, NULL);
        if (option == -1) {
            if (optind < argc) {
                printf ("non-option ARGV-elements: ");
                while (optind < argc)
                    printf ("%s ", argv[optind++]);
                putchar ('\n');
            }
            break;
        }
        switch (option) {
        case OPT_MAC:
            local_addr.mac = atol(optarg);
            d_debug("set local_mac: %d\n", local_addr.mac);
            break;
        case OPT_SELF4:
            if (inet_pton(AF_INET, optarg, &ipv4_self->s_addr) != 1) {
                d_debug("set ipv4_self error: %s\n", optarg);
                return -1;
            }
            d_debug("set ipv4_self: %s\n", optarg);
            break;
        case OPT_ROUTER4:
            if (inet_pton(AF_INET, optarg, &default_router4->s_addr) != 1) {
                d_debug("set default_router4 error: %s\n", optarg);
                return -1;
            }
            d_debug("set default_router4: %s\n", optarg);
            has_router4 = true;
            break;
        case OPT_UNIQUE_SELF:
            if (inet_pton(AF_INET6, optarg, unique_self->s6_addr) != 1) {
                d_debug("set unique_self error: %s\n", optarg);
                return -1;
            }
            d_debug("set unique_self: %s\n", optarg);
            break;
        case OPT_GLOBAL_SELF:
            if (inet_pton(AF_INET6, optarg, global_self->s6_addr) != 1) {
                d_debug("set global_self error: %s\n", optarg);
                return -1;
            }
            d_debug("set global_self: %s\n", optarg);
            break;
        case OPT_ROUTER:
            if (inet_pton(AF_INET6, optarg, default_router6->s6_addr) != 1) {
                d_debug("set default_router6 error: %s\n", optarg);
                return -1;
            }
            d_debug("set default_router6: %s\n", optarg);
            has_router6 = true;
            break;
        case OPT_TUN:
            strncpy(tun_name, optarg, sizeof(tun_name));
            d_debug("set tun_name: %s\n", optarg);
            break;
        case OPT_DEV_TYPE:
            if (strcmp(optarg, "spi") == 0) {
                dev_type = DEV_SPI;
            } else if (strcmp(optarg, "bridge") == 0) {
                dev_type = DEV_BRIDGE;
            } else {
                d_error("un-support dev_type: %s\n", optarg);
                exit(-1);
            }
            d_debug("set dev_type: %s\n", optarg);
            break;
        case OPT_DEV:
            strncpy(cdn_dev, optarg, sizeof(cdn_dev));
            d_debug("set dev: %s\n", cdn_dev);
            break;
        case OPT_INTN_PIN:
            intn_pin = atol(optarg);
            d_debug("set intn_pin: %d\n", intn_pin);
            break;
        case 0:
        case '?':
        default:
            break;
        }
    }

    /* initialize tun interface */
    if ((tun_fd = tun_alloc(tun_name, IFF_TUN | IFF_NO_PI)) < 0) {
        d_error("Error connecting to tun interface: %s!\n", tun_name);
        exit(1);
    }

    if (dev_type == DEV_BRIDGE) {
        cdn_fd = cdbus_bridge_wrapper_init(&local_addr, cdn_dev);
        cdn_intf = &net_proxy_intf;
        cdn_task = cdbus_bridge_wrapper_task;
    } else if (dev_type == DEV_SPI) {
        cdn_fd = cdctl_bx_wrapper_init(&local_addr, cdn_dev, intn_pin);
        cdn_intf = &net_cdctl_bx_intf;
        cdn_task = cdctl_bx_wrapper_task;
    }
    cdn_task();
    sleep(1);

    while (true) {
        int ret;
        fd_set rd_set;
        fd_set exceptfds;
        FD_ZERO(&rd_set);
        FD_ZERO(&exceptfds);

        if (!cdn_intf->rx_head.len) {
            struct timeval tv = { .tv_sec = 0, .tv_usec = 1000 }; //us
            FD_SET(tun_fd, &rd_set);
            if (dev_type == DEV_BRIDGE)
                FD_SET(cdn_fd, &rd_set);
            else if (dev_type == DEV_SPI)
                FD_SET(cdn_fd, &exceptfds);
            ret = select(max(tun_fd, cdn_fd) + 1, &rd_set, NULL, &exceptfds, &tv);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                } else {
                    perror("select()");
                    exit(1);
                }
            }
        } else {
            d_debug("skip select...\n");
        }

        if (FD_ISSET(cdn_fd, &rd_set) || FD_ISSET(cdn_fd, &exceptfds) || cdn_intf->rx_head.len) {
            cdn_task(); // rx

            while (true) {
                cdnet_packet_t *n_pkt = cdnet_packet_get(&cdn_intf->rx_head);
                if (!n_pkt)
                    break;

                list_node_t *pre, *cur;
                cdnet_packet_t *conv_pkt = NULL;

                if (n_pkt->level == CDNET_L2 && n_pkt->frag) {
                    list_for_each(&frag_pend_head, pre, cur) {
                        cdnet_packet_t *t_pkt = list_entry(cur, cdnet_packet_t);
                        if (t_pkt->src_mac == n_pkt->src_mac) {
                            t_pkt->_seq_num = (t_pkt->_seq_num + 1) & 0x7f;
                            if (t_pkt->_seq_num != n_pkt->_seq_num ||
                                    n_pkt->frag == CDNET_FRAG_FIRST) {
                                d_error("wrong frag seq: %d %d, frag: %d\n",
                                        t_pkt->_seq_num, n_pkt->_seq_num, n_pkt->frag);
                                exit(-1);
                            }
                            memcpy(t_pkt->dat + t_pkt->len, n_pkt->dat, n_pkt->len);
                            t_pkt->len += n_pkt->len;
                            if (n_pkt->frag == CDNET_FRAG_LAST) {
                                list_pick(&frag_pend_head, pre, cur);
                                conv_pkt = t_pkt;
                            }
                            list_put(cdn_intf->free_head, &n_pkt->node);
                            break;
                        }
                    }
                    if (!cur) {
                        if (n_pkt->frag != CDNET_FRAG_FIRST) {
                            d_error("first fragment is not CDNET_FRAG_FIRST\n");
                            exit(-1);
                        }
                        list_put(&frag_pend_head, &n_pkt->node);
                    }
                } else { // convert single t_pkt
                    conv_pkt = n_pkt;
                }

                if (conv_pkt) {
                    int ip_len;
                    ret = cdnet2ip(cdn_intf, conv_pkt, tmp_buf, &ip_len);
                    if (ret == 0) {
                        int nwrite = cwrite(tun_fd, tmp_buf, ip_len);
                        d_debug(">>>: write to tun: %d/%d\n", nwrite, ip_len);
                        //hex_dump(tmp_buf, ip_len);
                    } else {
                        d_debug(">>>: cdnet2ip drop\n");
                    }
                    list_put(cdn_intf->free_head, &conv_pkt->node);
                }
            }
        }

        if (FD_ISSET(tun_fd, &rd_set) && cdn_intf->free_head->len > 50) {
            int nread = cread(tun_fd, tmp_buf, BUFSIZE);
            if (nread != 0) {
                cdnet_packet_t *pkt = cdnet_packet_get(cdn_intf->free_head);
                if (!pkt) {
                    d_error("no free pkt for tx\n");
                    exit(-1);
                }

                ret = ip2cdnet(cdn_intf, pkt, tmp_buf, nread);
                if (ret == 0) {
                    d_debug("<<<: write to proxy, tun len: %d\n", nread);
                    //hex_dump(tmp_buf, nread);
                    if (pkt->level == CDNET_L2 && pkt->len > 251) {
                        if (!pkt->seq) {
                            d_error("can't tx fragment without seq\n");
                            exit(-1);
                        }
                        int frag_at = 0;
                        while (true) {
                            cdnet_packet_t *frag_pkt = cdnet_packet_get(cdn_intf->free_head);
                            if (!frag_pkt) {
                                d_error("no free pkt for frag\n");
                                exit(-1);
                            }
                            memcpy(frag_pkt, pkt, offsetof(cdnet_packet_t, len));
                            int cpy_len = min(251, pkt->len - frag_at);
                            memcpy(frag_pkt->dat, pkt->dat + frag_at, cpy_len);
                            frag_pkt->len = cpy_len;
                            if (frag_at == 0)
                                frag_pkt->frag = CDNET_FRAG_FIRST;
                            else if (frag_at + cpy_len == pkt->len)
                                frag_pkt->frag = CDNET_FRAG_LAST;
                            else
                                frag_pkt->frag = CDNET_FRAG_MORE;
                            frag_at += cpy_len;
                            list_put(&cdn_intf->tx_head, &frag_pkt->node);

                            if (frag_at == pkt->len) {
                                list_put(cdn_intf->free_head, &pkt->node);
                                break;
                            }
                        }

                    } else {
                        pkt->frag = CDNET_FRAG_NONE;
                        list_put(&cdn_intf->tx_head, &pkt->node);
                    }
                } else {
                    d_debug("<<<: ip2cdnet drop\n");
                    list_put(cdn_intf->free_head, &pkt->node);
                }
            }
        }

        cdn_task(); // tx
    }

    return 0;
}
