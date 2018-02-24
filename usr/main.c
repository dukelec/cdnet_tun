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
#include <termios.h>

#include "tun.h"
#include "ip.h"
#include "cdnet.h"
#include "cdbus_uart.h"

/* buffer for reading from tun/tap interface, must be >= 1500 */
#define BUFSIZE 2000

extern cduart_intf_t cdshare_intf;
extern cdnet_intf_t net_proxy_intf;
extern cdnet_intf_t net_setting_intf;
void cdbus_bridge_init(cdnet_addr_t *addr);

int ip2cdnet(cdnet_intf_t *n_intf, cdnet_packet_t *n_pkt,
        const uint8_t *ip_dat, int ip_len);
int cdnet2ip(cdnet_intf_t *n_intf, cdnet_packet_t *n_pkt,
        uint8_t *ip_dat, int *ip_len);
int uart_init(int fd, int speed);


extern struct in_addr *ipv4_self;
extern struct in_addr *default_router4;
extern bool has_router4;

extern struct in6_addr *unique_self;
extern struct in6_addr *global_self;
extern struct in6_addr *default_router6;
extern bool has_router6;

static cdnet_addr_t local_addr = { .net = 0, .mac = 0x01 };
static char uart_dev[50] = "/dev/ttyACM0";
static char tun_name[20] = "";
static int uart_fd;

// frag_pend for rx only, must be: level == CDNET_L2 && is_fragment
static list_head_t frag_pend_head = {0};

static struct option longopts[] = {
        { "self4",          required_argument, NULL,    's' },
        { "router4",        required_argument, NULL,    't' },
        { "unique_self",    required_argument, NULL,    'u' },
        { "global_self",    required_argument, NULL,    'g' },
        { "router6",        required_argument, NULL,    'r' },
        { "dev",            required_argument, NULL,    'd' },
        { "mac",            required_argument, NULL,    'm' },
        { "tun",            required_argument, NULL,    'n' },
        { 0, 0, 0, 0 }
};

static void uart_flush(void)
{
    cdnet_tx(&net_setting_intf);
    cdnet_tx(&net_proxy_intf);

    while (cdshare_intf.tx_head.first != NULL) {
        cd_frame_t *frame = list_get_entry(&cdshare_intf.tx_head, cd_frame_t);
        cduart_fill_crc(frame->dat);
#ifdef VERBOSE
        char pbuf[52];
        hex_dump_small(pbuf, frame->dat, frame->dat[2] + 3, 16);
        d_verbose("<- uart tx [%s]\n", pbuf);
#endif
        int ret = write(uart_fd, frame->dat, frame->dat[2] + 5);
        if (ret != frame->dat[2] + 5) {
            d_error("err: write uart len: %d, ret: %d\n", frame->dat[2] + 5, ret);
            exit(1);
        }
        list_put(cdshare_intf.free_head, &frame->node);
    }
}

int main(int argc, char *argv[]) {

    int tun_fd;
    uint8_t tmp_buf[BUFSIZE];

    while (true) {
        int option = getopt_long(argc, argv, "s:t:u:g:r:d:", longopts, NULL);
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
        case 's':
            if (inet_pton(AF_INET, optarg, &ipv4_self->s_addr) != 1) {
                d_debug("set ipv4_self error: %s\n", optarg);
                return -1;
            }
            d_debug("set ipv4_self: %s\n", optarg);
            break;
        case 't':
            if (inet_pton(AF_INET, optarg, &default_router4->s_addr) != 1) {
                d_debug("set default_router4 error: %s\n", optarg);
                return -1;
            }
            d_debug("set default_router4: %s\n", optarg);
            has_router4 = true;
            break;
        case 'u':
            if (inet_pton(AF_INET6, optarg, unique_self->s6_addr) != 1) {
                d_debug("set unique_self error: %s\n", optarg);
                return -1;
            }
            d_debug("set unique_self: %s\n", optarg);
            break;
        case 'g':
            if (inet_pton(AF_INET6, optarg, global_self->s6_addr) != 1) {
                d_debug("set global_self error: %s\n", optarg);
                return -1;
            }
            d_debug("set global_self: %s\n", optarg);
            break;
        case 'r':
            if (inet_pton(AF_INET6, optarg, default_router6->s6_addr) != 1) {
                d_debug("set default_router6 error: %s\n", optarg);
                return -1;
            }
            d_debug("set default_router6: %s\n", optarg);
            has_router6 = true;
            break;
        case 'd':
            strncpy(uart_dev, optarg, sizeof(uart_dev));
            d_debug("set uart_dev: %s\n", optarg);
            break;
        case 'm':
            local_addr.mac = atol(optarg);
            d_debug("set local_mac: %d\n", local_addr.mac);
            break;
        case 'n':
            strncpy(tun_name, optarg, sizeof(tun_name));
            d_debug("set tun_name: %s\n", optarg);
            break;
        case 0:
            break;
        case '?':
        default:
            fprintf(stderr, "%s: option `-%c' is invalid: ignored\n",
                    argv[0], optopt);
            break;
        }
    }

    /* initialize tun interface */
    if ((tun_fd = tun_alloc(tun_name, IFF_TUN | IFF_NO_PI)) < 0) {
        d_error("Error connecting to tun interface: %s!\n", tun_name);
        exit(1);
    }

    uart_fd = open(uart_dev, O_RDWR | O_NOCTTY);
    if (uart_init(uart_fd, 115200)) {
        d_error("Error init uart: %s!\n", uart_dev);
        exit(-1);
    }

    cdbus_bridge_init(&local_addr);
    uart_flush();

    while (true) {
        int ret;
        fd_set rd_set;
        struct timeval tv = { .tv_sec = 0, .tv_usec = 1 }; //ms

        FD_ZERO(&rd_set);
        FD_SET(tun_fd, &rd_set);
        FD_SET(uart_fd, &rd_set);

        ret = select(max(tun_fd, uart_fd) + 1, &rd_set, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                perror("select()");
                exit(1);
            }
        }

        if (FD_ISSET(tun_fd, &rd_set) && net_proxy_intf.free_head->len > 10) {
            int nread = cread(tun_fd, tmp_buf, BUFSIZE);
            if (nread != 0) {
                cdnet_packet_t *pkt = cdnet_packet_get(net_proxy_intf.free_head);
                if (!pkt) {
                    d_error("no free pkt for tx\n");
                    exit(-1);
                }

                ret = ip2cdnet(&net_proxy_intf, pkt, tmp_buf, nread);
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
                            cdnet_packet_t *frag_pkt = cdnet_packet_get(net_proxy_intf.free_head);
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
                            list_put(&net_proxy_intf.tx_head, &frag_pkt->node);

                            if (frag_at == pkt->len) {
                                list_put(net_proxy_intf.free_head, &pkt->node);
                                break;
                            }
                        }

                    } else {
                        pkt->frag = CDNET_FRAG_NONE;
                        list_put(&net_proxy_intf.tx_head, &pkt->node);
                    }
                } else {
                    d_debug("<<<: ip2cdnet drop\n");
                    list_put(net_proxy_intf.free_head, &pkt->node);
                }

                uart_flush();
            }
        }

        if (FD_ISSET(uart_fd, &rd_set)) {
            int uart_len = read(uart_fd, tmp_buf, BUFSIZE);
            if (uart_len < 0) {
                d_error("read uart");
                exit(1);
            }
            if (uart_len != 0) {
                //d_verbose("uart get len: %d\n", uart_len);
                cduart_rx_handle(&cdshare_intf, tmp_buf, uart_len);
            }
            cdnet_rx(&net_setting_intf);
            cdnet_rx(&net_proxy_intf);
            uart_flush();

            cdnet_packet_t *s_pkt = cdnet_packet_get(&net_setting_intf.rx_head);
            if (s_pkt) {
                d_debug("setting_intf: get rx, free\n");
                list_put(net_setting_intf.free_head, &s_pkt->node);
            }

            cdnet_packet_t *n_pkt = cdnet_packet_get(&net_proxy_intf.rx_head);
            if (n_pkt) {
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
                            list_put(net_proxy_intf.free_head, &n_pkt->node);
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
                    ret = cdnet2ip(&net_proxy_intf, conv_pkt, tmp_buf, &ip_len);
                    if (ret == 0) {
                        int nwrite = cwrite(tun_fd, tmp_buf, ip_len);
                        d_debug(">>>: write to tun: %d/%d\n", nwrite, ip_len);
                        //hex_dump(tmp_buf, ip_len);
                    } else {
                        d_debug(">>>: cdnet2ip drop\n");
                    }
                    list_put(net_proxy_intf.free_head, &conv_pkt->node);
                }
            }
        }

        uart_flush();
    }

    return 0;
}
