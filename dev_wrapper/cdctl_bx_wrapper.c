/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <duke@dukelec.com>
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <linux/types.h>
#include <linux/spi/spidev.h>

#include "common.h"
#include "cdctl_bx.h"
#include "cdctl_bx_regs.h"
#include "main.h"

#define CDCTL_MASK (BIT_FLAG_RX_PENDING |           \
            BIT_FLAG_RX_LOST | BIT_FLAG_RX_ERROR |  \
            BIT_FLAG_TX_CD | BIT_FLAG_TX_ERROR)

#define SYSFS_GPIO_DIR  "/sys/class/gpio"
#define GPIO_BUF_SIZE   64

#define BAUD_L          1000000
#define BAUD_H          10000000

static uint32_t spi_speed = 20000000; // HZ

static list_head_t cd_free_head = {0};
static list_head_t net_free_head = {0};

static spi_t spi_dev = {0};
static gpio_t intn_pin = {0};

static cdctl_intf_t r_intf = {0};
cdnet_intf_t net_cdctl_bx_intf = {0};


static bool gpio_get_value(gpio_t *gpio)
{
    char ch;
    lseek(gpio->fd, 0, SEEK_SET);
    if (read(gpio->fd, &ch, 1) != 1) {
        d_error("read gpio faild\n");
        exit(-1);
    }
    return ch == '1';
}

static int gpio_fd_open(unsigned int gpio)
{
        int fd;
        char buf[GPIO_BUF_SIZE];
        snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%d/value", gpio);
        fd = open(buf, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            d_error("gpio_fd_open faild\n");
            exit(-1);
        }
        return fd;
}


void spi_mem_write(spi_t *spi, uint8_t mem_addr, const uint8_t *buf, int len)
{
    int status;
    struct spi_ioc_transfer xfer[2] = {0};
    xfer[0].tx_buf = (unsigned long)&mem_addr;
    xfer[0].len = 1;
    xfer[1].tx_buf = (unsigned long)buf;
    xfer[1].len = len;
    status = ioctl(spi->fd, SPI_IOC_MESSAGE(2), xfer);
    if (status < 0) {
        d_error("SPI_IOC_MESSAGE wr\n");
        exit(-1);
    }
}

void spi_mem_read(spi_t *spi, uint8_t mem_addr, uint8_t *buf, int len)
{
    int status;
    struct spi_ioc_transfer xfer[2] = {0};
    xfer[0].tx_buf = (unsigned long)&mem_addr;
    xfer[0].len = 1;
    xfer[1].rx_buf = (unsigned long)buf;
    xfer[1].len = len;
    status = ioctl(spi->fd, SPI_IOC_MESSAGE(2), xfer);
    if (status < 0) {
        d_error("SPI_IOC_MESSAGE rd\n");
        exit(-1);
    }
}

static void spi_dumpstat(spi_t *spi)
{
    __u8    lsb, bits;
    __u32   mode, speed;

    if (ioctl(spi->fd, SPI_IOC_RD_MODE32, &mode) < 0) {
        perror("SPI rd_mode");
        return;
    }
    if (ioctl(spi->fd, SPI_IOC_RD_LSB_FIRST, &lsb) < 0) {
        perror("SPI rd_lsb_fist");
        return;
    }
    if (ioctl(spi->fd, SPI_IOC_RD_BITS_PER_WORD, &bits) < 0) {
        perror("SPI bits_per_word");
        return;
    }
    if (ioctl(spi->fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed) < 0) {
        perror("SPI max_speed_hz");
        return;
    }

    printf("spi mode 0x%x, %d bits %sper word, %d Hz max\n",
        mode, bits, lsb ? "(lsb first) " : "", speed);
}

static inline void cdctl_write_reg(cdctl_intf_t *intf, uint8_t reg, uint8_t val)
{
    spi_mem_write(intf->spi, reg | 0x80, &val, 1);
}


int cdctl_bx_wrapper_init(cdnet_addr_t *addr, const char *dev, int intn)
{
    int i;
    const char *def_dev = "/dev/spidev0.0";
    if (dev && *dev)
        def_dev = dev;
    spi_dev.fd = open(def_dev, O_RDWR | O_NOCTTY);
    if(spi_dev.fd < 0) {
        d_error("open %s failed\n", def_dev);
        exit(-1);
    }
    if (ioctl(spi_dev.fd, SPI_IOC_WR_MAX_SPEED_HZ, &spi_speed) == -1) {
        d_error("can't set spi speed hz");
        exit(-1);
    }
    spi_dumpstat(&spi_dev);
    intn_pin.fd = gpio_fd_open(intn);

    for (i = 0; i < CD_FRAME_MAX; i++)
        list_put(&cd_free_head, &cd_frame_alloc[i].node);
    for (i = 0; i < NET_PACKET_MAX; i++)
        list_put(&net_free_head, &net_packet_alloc[i].node);

    cdctl_intf_init(&r_intf, &cd_free_head, addr->mac, BAUD_L, BAUD_H, &spi_dev, NULL);
    cdnet_intf_init(&net_cdctl_bx_intf, &net_free_head, &r_intf.cd_intf, addr);
    cdctl_write_reg(&r_intf, REG_INT_MASK, CDCTL_MASK);
    return intn_pin.fd;
}

void cdctl_bx_wrapper_task(void)
{
    cdnet_tx(&net_cdctl_bx_intf);
    while (true) {
        cdctl_task(&r_intf);
        if (gpio_get_value(&intn_pin) && !r_intf.tx_head.len && !r_intf.is_pending)
            break;
    }
    cdnet_rx(&net_cdctl_bx_intf);
}
