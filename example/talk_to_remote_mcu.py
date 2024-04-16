#!/usr/bin/env python3
# Software License Agreement (BSD License)
#
# Copyright (c) 2017, DUKELEC, Inc.
# All rights reserved.
#
# Author: Duke Fong <d@d-l.io>

import socket
import time
import _thread

CDNET_DEF_PORT = 0xcdcd
DBG_RX_PORT    = 9
CMD_TGT_PORT   = 1

LOCAL_IP  = "fdcd::80:00"    # 80:00:00
TARGET_IP = "fdcd::80:00fe"  # 80:00:fe


def dbg_rx_thread():
    print("dbg_rx_thread start ...")
    while True:
        msg, addr = socket_dbg.recvfrom(300)
        msg_str = msg[1:].decode('utf-8').rstrip()
        print(f"dbg_rx: len {len(msg)}: {msg[0]:02x}, {msg_str}")


def main():
    global socket_dbg
    socket_cmd = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
    socket_dbg = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)

    socket_dbg.bind((LOCAL_IP, DBG_RX_PORT))
    _thread.start_new_thread(dbg_rx_thread, ())
    time.sleep(1)


    # send cmd query target's dev_info

    socket_cmd.bind((LOCAL_IP, CDNET_DEF_PORT))

    msg = b'\x00'
    socket_cmd.sendto(msg, (TARGET_IP, CMD_TGT_PORT))
    print("send msg ok")

    while True:
        msg, addr = socket_cmd.recvfrom(300)
        msg_str = msg[1:].decode('utf-8')
        print(f"dev_info len {len(msg)}: {msg[0]:02x}, {msg_str}")

if __name__ == "__main__":
    main()

