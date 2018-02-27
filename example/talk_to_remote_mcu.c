/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <duke@dukelec.com>
 */

/*
 * Send and receive data to/from remote raw mode cdbus_bridge.
 */

#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>

#define CDNET_DEF_PORT  0xcdcd
#define RAW_SER_PORT    20

#define LOCAL_IP        "fd00::cf00:0:ff:fe00:1"
#define REMOTE_IP       "fd00::cf00:0:ff:fe00:2"


int main(int argc, char *argv[])
{
    int socket_out, socket_in;
    struct sockaddr_in6 bind_out_port_addr = {0};
    struct sockaddr_in6 bind_app_port_addr = {0};
    struct sockaddr_in6 remote_addr = {0};
    uint8_t msg[300];
    int len;

    if ((socket_out = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
        perror("socket_out:");
        return -1;
    } else {
        printf("socket_out created ...\n");
    }

    if ((socket_in = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
        perror("socket_in:");
        return -1;
    } else {
        printf("socket_in created ...\n");
    }

    bind_out_port_addr.sin6_family = AF_INET6;
    bind_out_port_addr.sin6_port = htons(CDNET_DEF_PORT);
    //bind_out_port_addr.sin6_addr = in6addr_any;
    inet_pton(AF_INET6, LOCAL_IP, &bind_out_port_addr.sin6_addr);

    bind_app_port_addr.sin6_family = AF_INET6;
    bind_app_port_addr.sin6_port = htons(RAW_SER_PORT);
    //bind_app_port_addr.sin6_addr = in6addr_any;
    inet_pton(AF_INET6, LOCAL_IP, &bind_app_port_addr.sin6_addr);

    remote_addr.sin6_family = AF_INET6;
    remote_addr.sin6_port = htons(RAW_SER_PORT);
    inet_pton(AF_INET6, REMOTE_IP, &remote_addr.sin6_addr);


    if (bind(socket_out, (struct sockaddr *)&bind_out_port_addr,
            sizeof(struct sockaddr_in6)) < 0) {
        perror("bind_out_port_addr");
        return -1;
    } else {
        printf("bind_out_port_addr ok\n");
    }

    if (bind(socket_in, (struct sockaddr *)&bind_app_port_addr,
            sizeof(struct sockaddr_in6)) < 0) {
        perror("bind_app_port_addr");
        return -1;
    } else {
        printf("bind_app_port_addr ok\n");
    }

    msg[0] = 0x78;
    len = 1;
    if (sendto(socket_out, msg, 1, 0,
            (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0) {
        printf("error");
        return -1;
    }
    printf("send msg ok\n");

    while (1) {
        len = recvfrom(socket_in, msg, sizeof(msg), 0,
                (struct sockaddr *)NULL, (int *)NULL);
        printf("Received message len %d: %x\n", len, msg[0]);
    }
}
