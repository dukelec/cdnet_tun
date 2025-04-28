/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <d@d-l.io>
 */

#include "main.h"

#define BUFSIZE 2000
static uint8_t tmp_buf[BUFSIZE]; // for ip package

static cdn_pkt_t tmp_packet = {0};

static cd_frame_t frame_alloc[FRAME_MAX];
list_head_t frame_free_head = {0};

cd_dev_t *cd_dev = NULL;
list_head_t *cd_rx_head = NULL;

static int dev_fd;
static void (* dev_task)(void);

typedef enum {
    DEV_TTY = 0,
    DEV_SPI,
    DEV_LD  // linux cdbus device
} dev_type_t;

static dev_type_t dev_type = DEV_TTY;
static int intn_pin = -1;


int main(int argc, char *argv[])
{
    int tun_fd;
    cd_args_t ca;
    cd_args_parse(&ca, argc, argv);
    char tun_name[20] = "";

    const char *self6 = cd_arg_get(&ca, "--self6");
    const char *router6 = cd_arg_get(&ca, "--router6");
    const char *tun_str = cd_arg_get(&ca, "--tun");
    const char *dev_name = cd_arg_get(&ca, "--dev");
    const char *dev_tyte_str = cd_arg_get(&ca, "--dev-type");
    const char *intn_str = cd_arg_get(&ca, "--intn");
    uint32_t tty_baud = strtol(cd_arg_get_def(&ca, "--tty-baud", "115200"), NULL, 0);
    port_offset = strtol(cd_arg_get_def(&ca, "--port-offset", "0"), NULL, 0);

    if (self6 != NULL) {
        if (inet_pton(AF_INET6, self6, ipv6_self->s6_addr) != 1) {
            d_debug("set self6 error: %s\n", self6);
            return -1;
        }
        d_debug("set self6: %s\n", self6);
    } else {
        d_debug("error: --self6 not set\n");
        return -1;
    }

    if (router6 != NULL) {
        if (inet_pton(AF_INET6, router6, default_router6->s6_addr) != 1) {
            d_debug("set router6 error: %s\n", router6);
            return -1;
        }
        d_debug("set router6: %s\n", router6);
        has_router6 = true;
    }

    if (dev_tyte_str && strcmp(dev_tyte_str, "tty") == 0) {
        dev_type = DEV_TTY;
#ifdef USE_SPI
    } else if (dev_tyte_str && strcmp(dev_tyte_str, "spi") == 0) {
        dev_type = DEV_SPI;
#endif
    } else if (dev_tyte_str && strcmp(dev_tyte_str, "ld") == 0) {
        dev_type = DEV_LD;
    } else {
        d_error("un-support dev_type: %s\n", optarg);
        exit(-1);
    }

    if (intn_str != NULL) {
        intn_pin = atol(intn_str);
        d_debug("set intn_pin: %d\n", intn_pin);
    }

    // initialize tun interface
    if (tun_str)
        strncpy(tun_name, tun_str, sizeof(tun_name));
    if ((tun_fd = tun_alloc(tun_name, IFF_TUN | IFF_NO_PI)) < 0) {
        d_error("error connecting to tun interface: %s!\n", tun_name);
        exit(1);
    }

    for (int i = 0; i < FRAME_MAX; i++)
        list_put(&frame_free_head, &frame_alloc[i].node);

    if (dev_type == DEV_TTY) {
        dev_fd = cdbus_tty_wrapper_init(dev_name, &frame_free_head, tty_baud);
        dev_task = cdbus_tty_wrapper_task;
#ifdef USE_SPI
    } else if (dev_type == DEV_SPI) {
        dev_fd = cdctl_spi_wrapper_init(dev_name, &frame_free_head, intn_pin);
        dev_task = cdctl_spi_wrapper_task;
#endif
    } else if (dev_type == DEV_LD) {
        dev_fd = linux_dev_wrapper_init(dev_name, &frame_free_head);
        dev_task = linux_dev_wrapper_task;
    }
    dev_task();
    l0dev_init();
    sleep(1);

    while (true) {
        int ret;
        fd_set rd_set;
        FD_ZERO(&rd_set);

        if (cd_rx_head->len == 0) {
            struct timeval tv = { .tv_sec = 0, .tv_usec = 1000 }; //us
            FD_SET(tun_fd, &rd_set);
            FD_SET(dev_fd, &rd_set);
            ret = select(max(tun_fd, dev_fd) + 1, &rd_set, NULL, NULL, &tv);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                } else {
                    perror("select()");
                    exit(1);
                }
            }
        } else {
            d_verbose("skip select...\n");
        }

        if (FD_ISSET(dev_fd, &rd_set) || cd_rx_head->len) {
            dev_task(); // rx

            while (true) {
                // cdbus -> cdnet
                cd_frame_t *frm = cd_dev->get_rx_frame(cd_dev);
                if (!frm)
                    break;

                uint8_t frm_hdr = frm->dat[3];
                tmp_packet.frm = frm;
                tmp_packet._l_net = ipv6_self->s6_addr[14];
                if ((frm_hdr & 0b11000000) == 0b01000000) {
                    tmp_packet._l0_lp = l0dev_get_l0_lp(frm->dat[0]);
                    if (tmp_packet._l0_lp == 0xff) {
                        d_debug("->-: from_frame l0_lp error, drop\n");
                        list_put(&frame_free_head, &frm->node);
                        continue;
                    }
                }
                ret = cdn_frame_r(&tmp_packet); // addition in: _l_net
                if (ret) {
                    d_debug("->-: from_frame error, drop\n");
                    list_put(&frame_free_head, &frm->node);
                    continue;
                }
                if ((frm_hdr & 0b11000000) == 0b01000000)
                    l0dev_rx_reply(&tmp_packet);

                int ip_len;
                ret = cdnet2ip(&tmp_packet, tmp_buf, &ip_len);
                list_put(&frame_free_head, &frm->node);
                if (ret == 0) {
                    int nwrite = cwrite(tun_fd, tmp_buf, ip_len);
                    d_debug(">>>: write to tun: %d/%d\n", nwrite, ip_len);
                    //hex_dump(tmp_buf, ip_len);
                } else {
                    d_debug("->-: cdnet2ip drop\n");
                }
            }
        }

        if (FD_ISSET(tun_fd, &rd_set) && frame_free_head.len > 5) {
            int nread = cread(tun_fd, tmp_buf, BUFSIZE);
            if (nread != 0) {

                cd_frame_t *frm = list_get_entry(&frame_free_head, cd_frame_t);
                tmp_packet.frm = frm;
                ret = ip2cdnet(&tmp_packet, tmp_buf, nread);
                if (ret == 0) {
                    d_debug("<<<: write to dev, tun len: %d\n", nread);
                    //hex_dump(tmp_buf, nread);
                    bool l0nr = l0dev_need_reply(&tmp_packet);

                    // cdnet -> cdbus
                    ret = cdn_frame_w(&tmp_packet); // addition in: _s_mac, _d_mac

                    if (ret == 0) {
                        if (l0nr)
                            l0dev_tx_request(frm);
                        else
                            cd_dev->put_tx_frame(cd_dev, frm);
                    } else {
                        list_put(&frame_free_head, &frm->node);
                        d_debug("-<-: to_frame error, drop\n");
                    }
                } else {
                    list_put(&frame_free_head, &frm->node);
                    d_debug("-<-: ip2cdnet drop\n");
                }
            }
        }

        l0dev_routine();
        dev_task(); // tx
    }

    return 0;
}
