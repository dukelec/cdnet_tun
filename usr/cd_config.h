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

#define CDCTL_OSC_CLK       12000000UL // 12MHz

#define CD_ARCH_SPI

#define CD_FRAME_SIZE       261 // last 3 bytes: backup src_port and l0_lp for l0dev

#define CD_DEBUG
//#define CD_VERBOSE
//#define CD_LIST_DEBUG
//#define CD_DBG_STR_LEN    160

//#define CD_LIST_IT
//#define CD_IRQ_SAFE
//#define CDN_IRQ_SAFE

#define CDN_L0_C

#define CDUART_IDLE_TIME    (500000 / CD_SYSTICK_US_DIV) // 500 ms


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
