/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <duke@dukelec.com>
 */

#if 0
#include <asm/termios.h>
#include <stropts.h>
#endif

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <time.h>
#include "common.h"
#include "arch_wrapper.h"


int uart_set_attribs(int fd, int speed)
{
#if 0
    struct termios2 tio;

    ioctl(fd, TCGETS2, &tio);
    tio.c_cflag &= ~CBAUD;
    tio.c_cflag |= BOTHER;
    tio.c_ispeed = 12345;
    tio.c_ospeed = 12345;
    ioctl(fd, TCSETS2, &tio);
#else
    struct termios tty;

    if (tcgetattr(fd, &tty) < 0) {
        d_error("cduart: tcgetattr error: %s\n", strerror(errno));
        return -1;
    }

    cfsetospeed(&tty, (speed_t)speed);
    cfsetispeed(&tty, (speed_t)speed);

    tty.c_cflag |= (CLOCAL | CREAD);    /* ignore modem controls */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;         /* 8-bit characters */
    tty.c_cflag &= ~PARENB;     /* no parity bit */
    tty.c_cflag &= ~CSTOPB;     /* only need 1 stop bit */
    tty.c_cflag &= ~CRTSCTS;    /* no hardware flowcontrol */

    /* setup for non-canonical mode */
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;

    /* fetch bytes as they become available */
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        d_error("cduart: tcsetattr error: %s\n", strerror(errno));
        return -1;
    }
    return 0;
#endif
}

void uart_set_mincount(int fd, int mcount)
{
    struct termios tty;

    if (tcgetattr(fd, &tty) < 0) {
        d_error("cduart: tcgetattr error: %s\n", strerror(errno));
        return;
    }

    tty.c_cc[VMIN] = mcount ? 1 : 0;
    tty.c_cc[VTIME] = 1;        /* x 0.1 second timer */

    if (tcsetattr(fd, TCSANOW, &tty) < 0)
        d_error("cduart: tcsetattr error: %s\n", strerror(errno));
}
