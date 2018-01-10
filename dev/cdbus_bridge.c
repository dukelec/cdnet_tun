/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <duke@dukelec.com>
 */

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>

#include "common.h"
#include "cdbus_uart.h"
#include "port_dispatcher.h"

#define PASSTHRU_CONF_PORT  15

uart_t debug_uart = { .is_stdout = true };
static uart_t share_uart = { .fd = -1, .port = NULL, .is_stdout = false };

#define CD_FRAME_MAX 200
static cd_frame_t cd_frame_alloc[CD_FRAME_MAX];
static list_head_t cd_free_head = {0};

#define NET_PACKET_MAX 10
static cdnet_packet_t net_packet_alloc[NET_PACKET_MAX];
static list_head_t net_free_head = {0};

static cduart_intf_t cdshare_intf = {0};

static list_head_t cd_setting_head = {0};
static cd_intf_t cd_setting_intf = {0};
static list_head_t cd_proxy_head = {0};
static cd_intf_t cd_proxy_intf = {0};

cdnet_intf_t net_proxy_intf = {0};
static cdnet_intf_t net_setting_intf = {0};


static list_node_t *dummy_get_free_node(cd_intf_t *_)
{
    return cdshare_intf.cd_intf.get_free_node(&cdshare_intf.cd_intf);
}

static void dummy_put_free_node(cd_intf_t *_, list_node_t *node)
{
    cdshare_intf.cd_intf.put_free_node(&cdshare_intf.cd_intf, node);
}

static list_node_t *dummy_get_rx_node(cd_intf_t *intf)
{
    while (true) {
        list_node_t *node =
                cdshare_intf.cd_intf.get_rx_node(&cdshare_intf.cd_intf);
        if (!node)
            break;
        cd_frame_t *frame = container_of(node, cd_frame_t, node);

        if (frame->dat[0] == 0x55) {
            d_debug("dummy: 55 rx done\n");
            list_put(&cd_setting_head, node);
        } else if (frame->dat[0] == 0x56) {
            uint8_t i;
            frame->dat[2] -= 2;
            memcpy(frame->dat, frame->dat + 3, 2);
            for (i = 0; i < frame->dat[2]; i++)
                frame->dat[i + 3] = frame->dat[i + 5];
            d_debug("dummy: 56 rx done\n");
            list_put(&cd_proxy_head, node);
        } else {
            dummy_put_free_node(NULL, node);
        }
    }

    if (intf == &cd_setting_intf)
        return list_get(&cd_setting_head);
    if (intf == &cd_proxy_intf)
        return list_get(&cd_proxy_head);
    return NULL;
}

static void dummy_put_tx_node(cd_intf_t *intf, list_node_t *node)
{
    if (intf == &cd_setting_intf) {
        cdshare_intf.cd_intf.put_tx_node(&cdshare_intf.cd_intf, node);

    } else if (intf == &cd_proxy_intf) {
        cd_frame_t *frame = container_of(node, cd_frame_t, node);
        int i, l = frame->dat[2] + 2;
        for (i = l; i >= 3; i--)
            frame->dat[i + 2] = frame->dat[i];
        memcpy(frame->dat + 3, frame->dat, 2);
        frame->dat[0] = 0xaa;
        frame->dat[1] = 0x56;
        frame->dat[2] = l;
        cdshare_intf.cd_intf.put_tx_node(&cdshare_intf.cd_intf, node);
    }
}

static void dummy_set_filter(cd_intf_t *intf, uint8_t filter)
{
    if (intf == &cd_proxy_intf) {
        list_node_t *node = list_get(net_setting_intf.free_head);
        if (!node)
            return;
        cdnet_packet_t *pkt = container_of(node, cdnet_packet_t, node);
        pkt->is_level2 = false;
        pkt->is_multi_net = false;
        pkt->is_multicast = false;
        cdnet_fill_src_addr(&net_setting_intf, pkt);
        pkt->dst_mac = 0x55;
        pkt->src_port = CDNET_DEF_PORT;
        pkt->dst_port = PASSTHRU_CONF_PORT; // set filter
        pkt->dat_len = 1;
        pkt->dat[0] = filter;
        list_put(&net_setting_intf.tx_head, node);
        d_info("sent set filter frame\n");
    }
}

int cdnet_init(char *uart_dev, uint8_t mac)
{
    int i;
    for (i = 0; i < CD_FRAME_MAX; i++)
        list_put(&cd_free_head, &cd_frame_alloc[i].node);
    for (i = 0; i < NET_PACKET_MAX; i++)
        list_put(&net_free_head, &net_packet_alloc[i].node);

    share_uart.port = uart_dev;
    share_uart.fd = open(share_uart.port, O_RDWR | O_NOCTTY | O_SYNC);
    if (uart_set_attribs(share_uart.fd, B115200))
        exit(-1);
    uart_set_mincount(share_uart.fd, 0);
    cduart_intf_init(&cdshare_intf, &cd_free_head, &share_uart);

    cdshare_intf.local_filter[0] = 0xaa;
    cdshare_intf.local_filter_len = 1;
    cdshare_intf.remote_filter[0] = 0x55;
    cdshare_intf.remote_filter[1] = 0x56;
    cdshare_intf.remote_filter_len = 2;

    cd_setting_intf.get_free_node = dummy_get_free_node;
    cd_setting_intf.get_rx_node = dummy_get_rx_node;
    cd_setting_intf.put_free_node = dummy_put_free_node;
    cd_setting_intf.put_tx_node = dummy_put_tx_node;
    cd_setting_intf.set_filter = dummy_set_filter;

    cd_proxy_intf.get_free_node = dummy_get_free_node;
    cd_proxy_intf.get_rx_node = dummy_get_rx_node;
    cd_proxy_intf.put_free_node = dummy_put_free_node;
    cd_proxy_intf.put_tx_node = dummy_put_tx_node;
    cd_proxy_intf.set_filter = dummy_set_filter;

    cdnet_intf_init(&net_setting_intf, &net_free_head, &cd_setting_intf, 0xaa);
    cdnet_intf_init(&net_proxy_intf, &net_free_head, &cd_proxy_intf, mac);

    net_proxy_intf.cd_intf->set_filter(net_proxy_intf.cd_intf, mac);

    return share_uart.fd;
}

void cdnet_task_rx(void)
{
    cduart_task(&cdshare_intf);
    cdnet_rx(&net_setting_intf);
    cdnet_rx(&net_proxy_intf);
}

void cdnet_task_tx(void)
{
    cdnet_tx(&net_setting_intf);
    cdnet_tx(&net_proxy_intf);
    while (cdshare_intf.tx_head.first != NULL)
        cduart_tx_task(&cdshare_intf);
}

