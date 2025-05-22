/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <d@d-l.io>
 */

#include <unistd.h>
#include <time.h>

#include "cdbus.h"
#include "main.h"

#define CDBUS_MAGIC_NUM             'C'

#define CDBUS_GET_MODE              _IOR(CDBUS_MAGIC_NUM, 0x01, uint8_t)
#define CDBUS_SET_MODE              _IOW(CDBUS_MAGIC_NUM, 0x01, uint8_t)
#define CDBUS_GET_RATE              _IOR(CDBUS_MAGIC_NUM, 0x02, uint32_t *)
#define CDBUS_SET_RATE              _IOW(CDBUS_MAGIC_NUM, 0x02, uint32_t *)
#define CDBUS_GET_FILTER            _IOR(CDBUS_MAGIC_NUM, 0x03, uint8_t)
#define CDBUS_SET_FILTER            _IOW(CDBUS_MAGIC_NUM, 0x03, uint8_t)
#define CDBUS_GET_FILTERM           _IOR(CDBUS_MAGIC_NUM, 0x04, uint32_t)
#define CDBUS_SET_FILTERM           _IOW(CDBUS_MAGIC_NUM, 0x04, uint32_t)

#define CDBUS_GET_TX_PERMIT_LEN     _IOR(CDBUS_MAGIC_NUM, 0x0a, uint16_t)
#define CDBUS_SET_TX_PERMIT_LEN     _IOW(CDBUS_MAGIC_NUM, 0x0a, uint16_t)
#define CDBUS_GET_MAX_IDLE_LEN      _IOR(CDBUS_MAGIC_NUM, 0x0b, uint16_t)
#define CDBUS_SET_MAX_IDLE_LEN      _IOW(CDBUS_MAGIC_NUM, 0x0b, uint16_t)
#define CDBUS_GET_TX_PRE_LEN        _IOR(CDBUS_MAGIC_NUM, 0x0c, uint8_t)
#define CDBUS_SET_TX_PRE_LEN        _IOW(CDBUS_MAGIC_NUM, 0x0c, uint8_t)

static const char *def_dev = "/dev/cdbus";
static int ld_fd = -1;

static cd_dev_t            ld_dev;
static list_head_t         *ld_free_head;
static list_head_t         ld_rx_head;
static list_head_t         ld_tx_head;


// member functions

static cd_frame_t *ld_get_rx_frame(cd_dev_t *cd_dev)
{
    return list_get_entry(&ld_rx_head, cd_frame_t);
}

static void ld_put_tx_frame(cd_dev_t *cd_dev, cd_frame_t *frame)
{
    list_put(&ld_tx_head, &frame->node);
}


static uint8_t tmp_buf[256];

void linux_dev_wrapper_task(void)
{
    long int rx_len = read(ld_fd, tmp_buf, 256);
    
    if (rx_len < 0) {
        //d_verbose("dl: read err, len: %d\n", rx_len);

    } else if (rx_len >= 3 && rx_len == tmp_buf[2] + 3) {
        cd_frame_t *frame = list_get_entry(ld_free_head, cd_frame_t);
        if (frame) {
            memcpy(frame->dat, tmp_buf, min(rx_len, 256));
#ifdef VERBOSE
            char pbuf[52];
            hex_dump_small(pbuf, frame->dat, frame->dat[2] + 3, 16);
            d_verbose("dl: -> [%s]\n", pbuf);
#endif
            list_put(&ld_rx_head, &frame->node);
        } else {
            d_error("dl: get_rx, no free frame\n");
        }
    } else {
        d_error("dl: get_rx, wrong size: %ld\n", rx_len);
    }
    
    cd_frame_t *frame = list_get_entry(&ld_tx_head, cd_frame_t);
    if (frame) {
#ifdef VERBOSE
        char pbuf[52];
        hex_dump_small(pbuf, frame->dat, frame->dat[2] + 3, 16);
        d_verbose("dl: <- [%s]\n", pbuf);
#endif
        write(ld_fd, frame->dat, frame->dat[2] + 3);
        list_put(ld_free_head, &frame->node);
    }
}

int linux_dev_wrapper_init(const char *dev_name, list_head_t *free_head)
{
    if (dev_name && *dev_name)
        def_dev = dev_name;

    ld_fd = open(def_dev, O_RDWR);
    if(ld_fd < 0) {
        d_error("open %s failed\n", def_dev);
        exit(-1);
    }
    
    uint8_t filter;
    if (ioctl(ld_fd, CDBUS_GET_FILTER, &filter) < 0) {
            d_error("ioctl get_filter error");
            exit(-1);
    }
    d_info("ioctl get_filter: %02x\n", filter);
    
    //if (ioctl(ld_fd, CDBUS_SET_FILTER, 0) < 0) {
    //        d_error("ioctl set_filter error");
    //        exit(-1);
    //}

    ld_free_head = free_head;
    ld_dev.get_rx_frame = ld_get_rx_frame;
    ld_dev.put_tx_frame = ld_put_tx_frame;

    cd_dev = &ld_dev;
    cd_rx_head = &ld_rx_head;
    return ld_fd;
}
