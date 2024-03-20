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
#include <asm/termbits.h>

#include "cdbus_uart.h"
#include "main.h"

static uint32_t tty_baud = 115200;
static const char *def_dev = "/dev/ttyACM0";
static int uart_fd = -1;

static cduart_dev_t cduart_dev = {0};


// fd = open("/dev/ttyACM0", O_RDWR | O_NOCTTY);

static int uart_init(int fd, int speed)
{
    struct termios2 tio = {0};
    tio.c_cflag = BOTHER | CS8 | CLOCAL | CREAD;
    tio.c_iflag = IGNPAR;
    tio.c_oflag = 0;
    tio.c_ispeed = speed;
    tio.c_ospeed = speed;
    return ioctl(fd, TCSETS2, &tio);
}


static void cdbus_tty_tx(void)
{
    while (true) {
        cd_frame_t *frm = list_get_entry(&cduart_dev.tx_head, cd_frame_t);
        if (!frm)
            break;
        cduart_fill_crc(frm->dat);

#ifdef VERBOSE
        char pbuf[52];
        hex_dump_small(pbuf, frm->dat, frm->dat[2] + 3, 16);
        d_verbose("<- uart tx [%s]\n", pbuf);
#endif
        int ret = write(uart_fd, frm->dat, frm->dat[2] + 5);
        if (ret != frm->dat[2] + 5) {
            d_error("err: write uart len: %d, ret: %d\n", frm->dat[2] + 5, ret);
            exit(1);
        }
        list_put(cduart_dev.free_head, &frm->node);
    }
}


#define BUFSIZE 2000
static uint8_t tmp_buf[BUFSIZE];

static void cdbus_tty_rx(void)
{
    int uart_len = read(uart_fd, tmp_buf, BUFSIZE);
    if (uart_len < 0) {
        d_error("err: read uart");
        exit(1);
    }
    if (uart_len != 0) {
        //d_verbose("uart get len: %d\n", uart_len);
        cduart_rx_handle(&cduart_dev, tmp_buf, uart_len);
    }
}

void cdbus_tty_wrapper_task(void)
{
    cdbus_tty_rx();
    cdbus_tty_tx();
}

int cdbus_tty_wrapper_init(const char *dev_name, list_head_t *free_head)
{
    if (dev_name && *dev_name)
        def_dev = dev_name;

    uart_fd = open(def_dev, O_RDWR | O_NOCTTY);
    if(uart_fd < 0) {
        d_error("open %s failed\n", def_dev);
        exit(-1);
    }
    if (uart_init(uart_fd, tty_baud)) {
        d_error("init uart: %s faild!\n", def_dev);
        exit(-1);
    }

    cduart_dev_init(&cduart_dev, free_head);
    cd_dev = &cduart_dev.cd_dev;
    cd_rx_head = &cduart_dev.rx_head;
    return uart_fd;
}
