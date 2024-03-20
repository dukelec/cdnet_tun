/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <d@d-l.io>
 */

/*
 * Send and receive data to/from remote raw mode cdbus_bridge.
 */

#include <stdio.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>
#include <pthread.h>

#define CDNET_DEF_PORT  0xcdcd
#define DBG_RX_PORT     9
#define CMD_TGT_PORT    1

#define LOCAL_IP        "fdcd::80:00"   // 80:00:00
#define TARGET_IP       "fdcd::80:00fe" // 80:00:fe

static int socket_cmd, socket_dbg;

static void *dbg_rx_thread(void *_unused)
{
    int len;
    uint8_t msg[300];
    printf("dbg_rx_thread start ...\n");

    while (true) {
        len = recvfrom(socket_dbg, msg, sizeof(msg), 0, (struct sockaddr *)NULL, (int *)NULL);
        msg[len] = '\0';
        printf("dbg_rx: len %d: %x, %s", len, msg[0], msg + 1);
    }

    return NULL;
}


int main(int argc, char *argv[])
{
    pthread_t thread_id;
    struct sockaddr_in6 bind_cmd_port_addr = {0};
    struct sockaddr_in6 bind_dbg_port_addr = {0};
    struct sockaddr_in6 remote_addr = {0};
    uint8_t msg[300];
    int len;

    if ((socket_cmd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
        perror("socket_out:");
        return -1;
    } else {
        printf("socket_out created ...\n");
    }

    if ((socket_dbg = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
        perror("socket_in:");
        return -1;
    } else {
        printf("socket_in created ...\n");
    }

    bind_cmd_port_addr.sin6_family = AF_INET6;
    bind_cmd_port_addr.sin6_port = htons(CDNET_DEF_PORT);
    //bind_cmd_port_addr.sin6_addr = in6addr_any;
    inet_pton(AF_INET6, LOCAL_IP, &bind_cmd_port_addr.sin6_addr);

    bind_dbg_port_addr.sin6_family = AF_INET6;
    bind_dbg_port_addr.sin6_port = htons(DBG_RX_PORT);
    //bind_dbg_port_addr.sin6_addr = in6addr_any;
    inet_pton(AF_INET6, LOCAL_IP, &bind_dbg_port_addr.sin6_addr);


    if (bind(socket_dbg, (struct sockaddr *)&bind_dbg_port_addr, sizeof(struct sockaddr_in6)) < 0) {
        perror("bind_dbg_port_addr");
        return -1;
    } else {
        printf("bind_dbg_port_addr ok\n");
    }

    pthread_create(&thread_id, NULL, dbg_rx_thread, NULL);
    sleep(1);


    // send cmd query target's dev_info

    remote_addr.sin6_family = AF_INET6;
    remote_addr.sin6_port = htons(CMD_TGT_PORT);
    inet_pton(AF_INET6, TARGET_IP, &remote_addr.sin6_addr);

    if (bind(socket_cmd, (struct sockaddr *)&bind_cmd_port_addr, sizeof(struct sockaddr_in6)) < 0) {
        perror("bind_cmd_port_addr");
        return -1;
    } else {
        printf("bind_cmd_port_addr ok\n");
    }

    msg[0] = 0x00;
    len = 1;
    if (sendto(socket_cmd, msg, 1, 0, (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0) {
        printf("error");
        return -1;
    }
    printf("send msg ok\n");

    while (true) {
        len = recvfrom(socket_cmd, msg, sizeof(msg), 0, (struct sockaddr *)NULL, (int *)NULL);
        msg[len] = '\0';
        printf("dev_info len %d: %x, %s\n", len, msg[0], msg + 1);
    }
}
