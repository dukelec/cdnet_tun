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
#include <termios.h>
#include <fcntl.h>

#include "cdbus_uart.h"
#include "main.h"

int uart_init(int fd, int speed);


static int uart_fd = -1;

static list_head_t cd_free_head = {0};
static list_head_t net_free_head = {0};

static cduart_intf_t cdshare_intf = {0};

static list_head_t cd_setting_head = {0};
static cd_intf_t cd_setting_intf = {0};
static list_head_t cd_proxy_head = {0};
static cd_intf_t cd_proxy_intf = {0};

cdnet_intf_t net_proxy_intf = {0};
static cdnet_intf_t net_setting_intf = {0};

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

int cdbus_bridge_wrapper_init(cdnet_addr_t *addr, const char *dev)
{
    int i;
    const char *def_dev = "/dev/ttyACM0";
    if (dev && *dev)
        def_dev = dev;
    uart_fd = open(def_dev, O_RDWR | O_NOCTTY);
    if(uart_fd < 0) {
        d_error("open %s failed\n", def_dev);
        exit(-1);
    }
    if (uart_init(uart_fd, 115200)) {
        d_error("init uart: %s faild!\n", def_dev);
        exit(-1);
    }

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

    return uart_fd;
}


static void cdbus_bridge_tx(void)
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

static void cdbus_bridge_rx(void)
{
#define BUFSIZE 2000
    uint8_t tmp_buf[BUFSIZE];

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
    //cdbus_bridge_tx();

    cdnet_packet_t *s_pkt = cdnet_packet_get(&net_setting_intf.rx_head);
    if (s_pkt) {
        d_debug("setting_intf: get rx, free\n");
        list_put(net_setting_intf.free_head, &s_pkt->node);
    }
}

void cdbus_bridge_wrapper_task(void)
{
    cdbus_bridge_rx();
    cdbus_bridge_tx();
}
