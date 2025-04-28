/*
 * Software License Agreement (MIT License)
 *
 * Copyright (c) 2018, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <d@d-l.io>
 */

#include "main.h"

static l0dev_t l0dev_tb[256] = {0};
list_head_t l0dev_active = {0}; // active l0dev uni-cast
list_head_t l0dev_active_m = {0}; // active l0dev m-cast


uint8_t l0dev_get_l0_lp(uint8_t mac)
{
    l0dev_t *l0dev = &l0dev_tb[mac];
    if (!l0dev->active)
        return 0xff;
    return l0dev->l0_lp;
}

static l0dev_t *l0dev_search(list_head_t *head, uint8_t mac, list_node_t **pre)
{
    list_node_t *_pre;
    list_node_t *pos;
    list_for_each(head, _pre, pos) {
        l0dev_t *l0dev = list_entry(pos, l0dev_t);
        if (l0dev->mac == mac) {
            if (pre != NULL)
                *pre = _pre;
            return l0dev;
        }
    }
    return NULL;
}

static void l0dev_activate(list_head_t *head, uint8_t mac)
{
    l0dev_t *l0dev = &l0dev_tb[mac];
    l0dev->active = true;
    list_put(head, &l0dev->node);
}

static void l0dev_deactivate(list_head_t *head, uint8_t mac)
{
    list_node_t *pre;
    l0dev_t *l0dev = l0dev_search(head, mac, &pre);
    if (l0dev) {
        list_pick(head, pre, &l0dev->node);
        l0dev->active = false;
    } else {
        d_error("l0dev_deactivate: %02x already deactivated\n");
    }
}


// check & replace port
bool l0dev_need_reply(cdn_pkt_t *pkt)
{
    if (pkt->dst.port > 0x3f || (pkt->dat[0] & 0b11000000) != 0)
        return false;
    pkt->frm->dat[260] = pkt->dst.port;
    memcpy(pkt->frm->dat + 258, &pkt->src.port, 2); // backup src port
    pkt->src.port = CDN_DEF_PORT;
    return true;
}

// delete cur pending, restore port, send next frame
void l0dev_rx_reply(cdn_pkt_t *pkt)
{
    l0dev_t *l0dev = &l0dev_tb[pkt->_s_mac];
    if (!l0dev->active)
        return;
    pkt->dst.port = l0dev->src_port;

    if (!l0dev->pend_frm.len) {
        l0dev_deactivate(&l0dev_active, l0dev->mac);
        return;
    }
    cd_frame_t *frm = list_get_entry(&l0dev->pend_frm, cd_frame_t);
    l0dev->t_last = get_systick();
    l0dev->l0_lp = frm->dat[260];
    memcpy(&l0dev->src_port, frm->dat + 258, 2);
    cd_dev->put_tx_frame(cd_dev, frm);
}


// return true when should deactivate
static bool l0dev_tx_mcast(uint8_t mac)
{
    l0dev_t *d = &l0dev_tb[mac];
    if (!d->active)
        return true;
    if (!d->pend_frm.len) {
        d->active = false;
        return true;
    }

    bool allow_send = true;
    for (int i = 0; i < d->mcast_len; i++) {
        l0dev_t *m = &l0dev_tb[d->mcast_mb[i]];
        if (m->active) {
            allow_send = false;
            break;
        }
    }
    if (!allow_send)
        return false;

    cd_frame_t *frm = list_get_entry(&d->pend_frm, cd_frame_t);
    for (int i = 0; i < d->mcast_len; i++) {
        l0dev_t *m = &l0dev_tb[d->mcast_mb[i]];
        m->t_last = get_systick();
        m->l0_lp = frm->dat[260];
        memcpy(&m->src_port, frm->dat + 258, 2);
        l0dev_activate(&l0dev_active, m->mac);
    }
    cd_dev->put_tx_frame(cd_dev, frm);

    if (!d->pend_frm.len) {
        d->active = false;
        return true;
    } else {
        return false;
    }
}


// send frm or append frm
void l0dev_tx_request(cd_frame_t *frm)
{
    l0dev_t *l0dev = &l0dev_tb[frm->dat[1]];
    if (l0dev->mcast_len) {
        list_put(&l0dev->pend_frm, &frm->node);
        if (l0dev->active)
            return;
        l0dev->active = true;
        bool deactivate = l0dev_tx_mcast(l0dev->mac);
        if (!deactivate)
            l0dev_activate(&l0dev_active_m, l0dev->mac);
        return;
    }

    if (l0dev->active) {
        list_put(&l0dev->pend_frm, &frm->node);
        return;
    }

    l0dev->t_last = get_systick();
    l0dev->l0_lp = frm->dat[260];
    memcpy(&l0dev->src_port, frm->dat + 258, 2);
    cd_dev->put_tx_frame(cd_dev, frm);
    l0dev_activate(&l0dev_active, l0dev->mac);
}


// delete timeout frm and send next frm
void l0dev_routine(void)
{
    list_node_t *pre;
    list_node_t *pos;

    list_for_each(&l0dev_active, pre, pos) {
        l0dev_t *l0dev = list_entry(pos, l0dev_t);
        if (get_systick() - l0dev->t_last > CDN_L0DEV_TIMEOUT) {
            d_error("l0dev_routine: mac %02x timeout, left: %d\n", l0dev->mac, l0dev->pend_frm.len);
            if (l0dev->pend_frm.len) {
                cd_frame_t *frm = list_get_entry(&l0dev->pend_frm, cd_frame_t);
                l0dev->t_last = get_systick();
                l0dev->l0_lp = frm->dat[260];
                memcpy(&l0dev->src_port, frm->dat + 258, 2);
                cd_dev->put_tx_frame(cd_dev, frm);
            } else {
                list_pick(&l0dev_active, pre, &l0dev->node);
                l0dev->active = false;
                pos = pre;
            }
        }
    }

    list_for_each(&l0dev_active_m, pre, pos) {
        l0dev_t *l0dev = list_entry(pos, l0dev_t);
        bool deactivate = l0dev_tx_mcast(l0dev->mac);
        if (deactivate) {
            list_pick(&l0dev_active_m, pre, &l0dev->node);
            l0dev->active = false;
            pos = pre;
        }
    }
}

void l0dev_init(void)
{
    // TODO: load mcast config from file
    for (int i = 0; i < 256; i++)
        l0dev_tb[i].mac = i;
}
