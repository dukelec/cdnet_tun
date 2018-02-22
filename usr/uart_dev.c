/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <duke@dukelec.com>
 */

// for termios2
#include <asm/termios.h>
#include <stropts.h>

#include "common.h"

// fd = open("/dev/ttyACM0", O_RDWR | O_NOCTTY);

int uart_init(int fd, int speed)
{
    struct termios2 tio = {0};

    tio.c_cflag = BOTHER | CS8 | CLOCAL | CREAD;
    tio.c_iflag = IGNPAR;
    tio.c_oflag = 0;
    tio.c_ispeed = speed;
    tio.c_ospeed = speed;
    return ioctl(fd, TCSETS2, &tio);
}
