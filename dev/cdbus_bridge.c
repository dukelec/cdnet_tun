/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <duke@dukelec.com>
 */

#include <unistd.h>
#include <time.h>

#include "common.h"
#include "cdbus_uart.h"

#define CD_FRAME_MAX 100
static cd_frame_t cd_frame_alloc[CD_FRAME_MAX];
static list_head_t cd_free_head = {0};

#define NET_PACKET_MAX 100
static cdnet_packet_t net_packet_alloc[NET_PACKET_MAX];
static list_head_t net_free_head = {0};

cduart_intf_t cdshare_intf = {0};

static list_head_t cd_setting_head = {0};
static cd_intf_t cd_setting_intf = {0};
static list_head_t cd_proxy_head = {0};
static cd_intf_t cd_proxy_intf = {0};

cdnet_intf_t net_proxy_intf = {0};
cdnet_intf_t net_setting_intf = {0};


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
            d_debug("dummy: skip rx from !0x55 && !0x56\n");
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
        pkt->level = CDNET_L0;
        cdnet_fill_src_addr(&net_setting_intf, pkt);
        pkt->dst_mac = 0x55;
        pkt->src_port = CDNET_DEF_PORT;
        pkt->dst_port = 3; // set device addr
        pkt->len = 3;
        pkt->dat[0] = 0x08; // set mac address for interface
        pkt->dat[1] = 0;    // 0: INTF_RS485
        pkt->dat[2] = filter;
        list_put(&net_setting_intf.tx_head, node);
        d_info("sent set filter frame\n");
    }
}

void cdbus_bridge_init(cdnet_addr_t *addr)
{
    int i;
    for (i = 0; i < CD_FRAME_MAX; i++)
        list_put(&cd_free_head, &cd_frame_alloc[i].node);
    for (i = 0; i < NET_PACKET_MAX; i++)
        list_put(&net_free_head, &net_packet_alloc[i].node);

    cduart_intf_init(&cdshare_intf, &cd_free_head);

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

    cdnet_addr_t addr_aa = { .net = 0, .mac = 0xaa };
    cdnet_intf_init(&net_setting_intf, &net_free_head, &cd_setting_intf, &addr_aa);
    cdnet_intf_init(&net_proxy_intf, &net_free_head, &cd_proxy_intf, addr);

    net_proxy_intf.cd_intf->set_filter(net_proxy_intf.cd_intf, addr->mac);
}
