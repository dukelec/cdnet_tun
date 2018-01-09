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

#include "tun.h"
#include "ip.h"
#include "cdnet.h"

/* buffer for reading from tun/tap interface, must be >= 1500 */
#define BUFSIZE 2000

extern cdnet_intf_t net_proxy_intf;
int cdnet_init(void);
void cdnet_task_rx(void);
void cdnet_task_tx(void);

int ip2cdnet(cdnet_intf_t *n_intf, cdnet_packet_t *n_pkt,
        const uint8_t *ip_dat, int ip_len);
int cdnet2ip(cdnet_intf_t *n_intf, cdnet_packet_t *n_pkt,
        uint8_t *ip_dat, int *ip_len);

void hex_dump(char *desc, void *addr, int len);


int main(int argc, char *argv[]) {

    int tun_fd, uart_fd, maxfd;
    int ret;
    int option;
    uint16_t nread, nwrite, plength;
    int flags = IFF_TUN;
    char tun_name[20] = "";
    uint8_t ip_dat[BUFSIZE];

    /* Check command line options */
    while ((option = getopt(argc, argv, "i:")) > 0) {
        switch(option) {
        case 'i':
            // strncpy(uart_name, optarg, sizeof(uart_name));
            break;
        default:
            d_error("Unknown option %c\n", option);
        }
    }

    /* initialize tun interface */
    if ((tun_fd = tun_alloc(tun_name, flags | IFF_NO_PI)) < 0) {
        d_error("Error connecting to tun interface %s!\n", tun_name);
        exit(1);
    }

    {
        int init_addr(void);
        init_addr();
    }

    uart_fd = cdnet_init();
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
                list_put(&net_proxy_intf.tx_head, node);
                d_debug("<<<: write to proxy, tun len: %d\n", nread);
            } else {
                list_put(net_proxy_intf.free_head, node);
                d_debug("<<<: ip2cdnet drop\n");
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
