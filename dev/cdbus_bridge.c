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

#define NET_PACKET_MAX 200
static cdnet_packet_t net_packet_alloc[NET_PACKET_MAX];
static list_head_t net_free_head = {0};

cduart_intf_t cdshare_intf = {0};

static list_head_t cd_setting_head = {0};
static cd_intf_t cd_setting_intf = {0};
static list_head_t cd_proxy_head = {0};
static cd_intf_t cd_proxy_intf = {0};

cdnet_intf_t net_proxy_intf = {0};
cdnet_intf_t net_setting_intf = {0};

static cd_frame_t *conv_frame = NULL;


static cd_frame_t *dummy_get_free_frame(cd_intf_t *_)
{
    return cdshare_intf.cd_intf.get_free_frame(&cdshare_intf.cd_intf);
}

static void dummy_put_free_frame(cd_intf_t *_, cd_frame_t *frame)
{
    cdshare_intf.cd_intf.put_free_frame(&cdshare_intf.cd_intf, frame);
}

static cd_frame_t *dummy_get_rx_frame(cd_intf_t *intf)
{
    while (true) {
        cd_frame_t *frame = cdshare_intf.cd_intf.get_rx_frame(&cdshare_intf.cd_intf);
        if (!frame)
            break;

        if (frame->dat[0] == 0x55) {
            //d_debug("dummy: 55 rx done\n");
            list_put(&cd_setting_head, &frame->node);
        } else if (frame->dat[0] == 0x56) {
            memcpy(conv_frame->dat, frame->dat + 3, 2);
            conv_frame->dat[2] = frame->dat[2] - 2;
            memcpy(conv_frame->dat + 3, frame->dat + 5, conv_frame->dat[2]);

            list_put(&cd_proxy_head, &conv_frame->node);
            conv_frame = frame;
            //d_debug("dummy: 56 rx done\n");
        } else {
            d_debug("dummy: skip rx from !0x55 && !0x56\n");
            dummy_put_free_frame(NULL, frame);
        }
    }

    if (intf == &cd_setting_intf)
        return list_get_entry(&cd_setting_head, cd_frame_t);
    if (intf == &cd_proxy_intf)
    return list_get_entry(&cd_proxy_head, cd_frame_t);
    return NULL;
}

static void dummy_put_tx_frame(cd_intf_t *intf, cd_frame_t *frame)
{
    if (intf == &cd_setting_intf) {
        cdshare_intf.cd_intf.put_tx_frame(&cdshare_intf.cd_intf, frame);

    } else if (intf == &cd_proxy_intf) {
        conv_frame->dat[0] = 0xaa;
        conv_frame->dat[1] = 0x56;
        conv_frame->dat[2] = frame->dat[2] + 2;
        memcpy(conv_frame->dat + 3, frame->dat, 2);
        memcpy(conv_frame->dat + 5, frame->dat + 3, conv_frame->dat[2]);
        cdshare_intf.cd_intf.put_tx_frame(&cdshare_intf.cd_intf, conv_frame);
        conv_frame = frame;
    }
}

static void dummy_set_filter(cd_intf_t *intf, uint8_t filter)
{
    if (intf == &cd_proxy_intf) {
        cdnet_packet_t *pkt = cdnet_packet_get(net_setting_intf.free_head);
        if (!pkt)
            return;
        pkt->level = CDNET_L0;
        cdnet_fill_src_addr(&net_setting_intf, pkt);
        pkt->dst_mac = 0x55;
        pkt->src_port = CDNET_DEF_PORT;
        pkt->dst_port = 3; // set device addr
        pkt->len = 3;
        pkt->dat[0] = 0x08; // set mac address for interface
        pkt->dat[1] = 0;    // 0: INTF_RS485
        pkt->dat[2] = filter;
        list_put(&net_setting_intf.tx_head, &pkt->node);
        cdnet_tx(&net_setting_intf);
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

    conv_frame = list_get_entry(&cd_free_head, cd_frame_t);

    cduart_intf_init(&cdshare_intf, &cd_free_head);

    cdshare_intf.local_filter[0] = 0xaa;
    cdshare_intf.local_filter_len = 1;
    cdshare_intf.remote_filter[0] = 0x55;
    cdshare_intf.remote_filter[1] = 0x56;
    cdshare_intf.remote_filter_len = 2;

    cd_setting_intf.get_free_frame = dummy_get_free_frame;
    cd_setting_intf.get_rx_frame = dummy_get_rx_frame;
    cd_setting_intf.put_free_frame = dummy_put_free_frame;
    cd_setting_intf.put_tx_frame = dummy_put_tx_frame;
    cd_setting_intf.set_filter = dummy_set_filter;

    cd_proxy_intf.get_free_frame = dummy_get_free_frame;
    cd_proxy_intf.get_rx_frame = dummy_get_rx_frame;
    cd_proxy_intf.put_free_frame = dummy_put_free_frame;
    cd_proxy_intf.put_tx_frame = dummy_put_tx_frame;
    cd_proxy_intf.set_filter = dummy_set_filter;

    cdnet_addr_t addr_aa = { .net = 0, .mac = 0xaa };
    net_setting_intf.name = "cdnet_setting";
    net_proxy_intf.name = "cdnet_proxy";
    cdnet_intf_init(&net_setting_intf, &net_free_head, &cd_setting_intf, &addr_aa);
    cdnet_intf_init(&net_proxy_intf, &net_free_head, &cd_proxy_intf, addr);

    net_proxy_intf.cd_intf->set_filter(net_proxy_intf.cd_intf, addr->mac);
}
