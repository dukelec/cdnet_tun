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

/* buffer for reading from tun/tap interface, must be >= 1500 */
#define BUFSIZE 2000

extern cdnet_intf_t net_proxy_intf;
int cdnet_init(char *uart_dev, uint8_t mac);
void cdnet_task_rx(void);
void cdnet_task_tx(void);

int ip2cdnet(cdnet_intf_t *n_intf, cdnet_packet_t *n_pkt,
        const uint8_t *ip_dat, int ip_len);
int cdnet2ip(cdnet_intf_t *n_intf, cdnet_packet_t *n_pkt,
        uint8_t *ip_dat, int *ip_len);

void hex_dump(char *desc, void *addr, int len);


extern struct in_addr *ipv4_self;
extern struct in_addr *default_router4;
extern bool has_router4;

extern struct in6_addr *unique_self;
extern struct in6_addr *global_self;
extern struct in6_addr *default_router6;
extern bool has_router6;

static uint8_t local_mac = 255;
static char uart_dev[50] = "/dev/ttyUSB0";
static char tun_name[20] = "";


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

int main(int argc, char *argv[]) {

    int tun_fd, uart_fd, maxfd;
    int ret;
    int option;
    uint16_t nread, nwrite, plength;
    int flags = IFF_TUN;
    uint8_t ip_dat[BUFSIZE];

    while (true) {
        option = getopt_long(argc, argv, "s:t:u:g:r:d:", longopts, NULL);
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
            local_mac = atol(optarg);
            d_debug("set local_mac: %d\n", local_mac);
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
    if ((tun_fd = tun_alloc(tun_name, flags | IFF_NO_PI)) < 0) {
        d_error("Error connecting to tun interface %s!\n", tun_name);
        exit(1);
    }

    uart_fd = cdnet_init(uart_dev, local_mac);
    maxfd = max(tun_fd, uart_fd);

    while (true) {
        fd_set rd_set;

        FD_ZERO(&rd_set);
        FD_SET(tun_fd, &rd_set);
        FD_SET(uart_fd, &rd_set);

        ret = select(maxfd + 1, &rd_set, NULL, NULL, NULL);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                perror("select()");
                exit(1);
            }
        }

        if (FD_ISSET(tun_fd, &rd_set)) {
            nread = cread(tun_fd, ip_dat, BUFSIZE);

            printf("nread: %d\n", nread);
            hex_dump(NULL, ip_dat, nread);

            list_node_t *node = list_get(net_proxy_intf.free_head);
            if (!node)
                continue;
            cdnet_packet_t *pkt = container_of(node, cdnet_packet_t, node);

            ret = ip2cdnet(&net_proxy_intf, pkt, ip_dat, nread);

            if (ret == 0) {
                d_debug("<<<: write to proxy, tun len: %d\n", nread);
                list_put(&net_proxy_intf.tx_head, node);
            } else {
                d_debug("<<<: ip2cdnet drop\n");
                list_put(net_proxy_intf.free_head, node);
            }
            cdnet_task_tx();
        }

        if (FD_ISSET(uart_fd, &rd_set)) {
            cdnet_task_rx();
            while (true) {
                int ip_len;

                list_node_t *node = list_get(&net_proxy_intf.rx_head);
                if (!node)
                    break;
                cdnet_packet_t *pkt = container_of(node, cdnet_packet_t, node);

                ret = cdnet2ip(&net_proxy_intf, pkt, ip_dat, &ip_len);
                if (ret == 0) {
                    hex_dump("write to tun", ip_dat, ip_len);
                    nwrite = cwrite(tun_fd, ip_dat, ip_len);
                    d_debug(">>>: write to tun: %d/%d\n", nwrite, ip_len);
                } else {
                    d_debug(">>>: cdnet2ip drop\n");
                }

                list_put(net_proxy_intf.free_head, node);
            }
        }
    }

    return 0;
}
