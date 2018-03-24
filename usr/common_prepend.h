/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <duke@dukelec.com>
 */

#ifndef __CDNET_CONFIG_H__
#define __CDNET_CONFIG_H__

#define DEBUG
//#define VERBOSE

#define CDUART_IDLE_TIME    (500000 / SYSTICK_US_DIV)

#define CDNET_USE_L2
#define CDNET_DAT_SIZE      2000 // MTU size + compress overhead
#define SEQ_TIMEOUT         (500000 / SYSTICK_US_DIV)


typedef struct {
    int fd;
} gpio_t;

#define gpio_set_value(...)

static inline void debug_flush(void)
{
}

typedef struct {
    int fd;
} spi_t;

void spi_mem_write(spi_t *spi, uint8_t mem_addr, const uint8_t *buf, int len);
void spi_mem_read(spi_t *spi, uint8_t mem_addr, uint8_t *buf, int len);

#endif
