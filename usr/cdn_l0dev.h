/*
 * Software License Agreement (MIT License)
 *
 * Copyright (c) 2018, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <d@d-l.io>
 */

#ifndef __CDN_L0DEV_H__
#define __CDN_L0DEV_H__

#include "cd_utils.h"
#include "cd_list.h"

#define CDN_L0DEV_TIMEOUT   3000 // ms


typedef struct {
    list_node_t node;
    uint8_t     mac;
    uint8_t     mcast_len; // mcast_mb len
    uint8_t     *mcast_mb; // mcast members
    bool        active;
    uint8_t     l0_lp;     // for uni-cast only
    uint16_t    src_port;  // for uni-cast only
    uint32_t    t_last;    // for uni-cast only
    list_head_t pend_frm;
} l0dev_t;


uint8_t l0dev_get_l0_lp(uint8_t mac);

// check & replace port
bool l0dev_need_reply(cdn_pkt_t *pkt);

// delete cur pending, restore port, send next frame
void l0dev_rx_reply(cdn_pkt_t *pkt);

// send frm or append frm
void l0dev_tx_request(cd_frame_t *frm);

// delete timeout frm and send next frm
void l0dev_routine(void);

void l0dev_init(void);

#endif
