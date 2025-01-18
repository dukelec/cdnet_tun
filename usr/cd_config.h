/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <d@d-l.io>
 */

#ifndef __CD_CONFIG_H__
#define __CD_CONFIG_H__

#define CDCTL_SYS_CLK       150000000UL // 150MHz for CDCTL01A

#define ARCH_SPI
#define CD_FRAME_SIZE       258

#define DEBUG
//#define VERBOSE
//#define LIST_DEBUG
//#define DBG_STR_LEN         160

//#define CD_LIST_IT
//#define CDUART_IRQ_SAFE // free_head requires irq safe
//#define CDN_IRQ_SAFE

#define CDN_L0_C

#define CDUART_IDLE_TIME    (500000 / SYSTICK_US_DIV) // 500 ms


typedef struct {
    int fd;
} gpio_t;

#define gpio_set_val(...)
#define gpio_set_high(...)
#define gpio_set_low(...)


static inline void debug_flush(bool wait_empty)
{
}

static inline void delay_systick(uint32_t val)
{
}

typedef struct {
    int fd;
} spi_t;

void spi_mem_write(spi_t *spi, uint8_t mem_addr, const uint8_t *buf, int len);
void spi_mem_read(spi_t *spi, uint8_t mem_addr, uint8_t *buf, int len);

#endif
