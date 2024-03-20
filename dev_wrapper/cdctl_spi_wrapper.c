/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <d@d-l.io>
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

#include "cdctl.h"
#include "main.h"

#define SYSFS_GPIO_DIR  "/sys/class/gpio"
#define GPIO_BUF_SIZE   64

static uint32_t spi_speed = 20000000; // HZ
const char *def_dev = "/dev/spidev0.0";
static spi_t spi_dev = {0};
static gpio_t intn_pin = {0};

static cdctl_dev_t cdctl_dev = {0};

static cdctl_cfg_t bus_cfg = {
        .mac = 0x00,
        .baud_l = 1000000,
        .baud_h = 10000000,
        .filter_m = { 0xff, 0xff },
        .mode = 0,
        .tx_permit_len = 0x14,
        .max_idle_len = 0xc8,
        .tx_pre_len = 0x01
};


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

    printf("spi mode 0x%x, %d bits %sper word, %d Hz max\n", mode, bits, lsb ? "(lsb first) " : "", speed);
}


void cdctl_spi_wrapper_task(void)
{
    while (true) {
        cdctl_routine(&cdctl_dev);
        if (gpio_get_value(&intn_pin) && !cdctl_dev.tx_head.len && !cdctl_dev.is_pending)
            break;
    }
}

int cdctl_spi_wrapper_init(const char *dev_name, list_head_t *free_head, int intn)
{
    if (dev_name && *dev_name)
        def_dev = dev_name;

    spi_dev.fd = open(def_dev, O_RDWR | O_NOCTTY);
    if(spi_dev.fd < 0) {
        d_error("open %s failed\n", def_dev);
        exit(-1);
    }
    if (ioctl(spi_dev.fd, SPI_IOC_WR_MAX_SPEED_HZ, &spi_speed) == -1) {
        d_error("can't set spi speed hz\n");
        exit(-1);
    }
    spi_dumpstat(&spi_dev);
    intn_pin.fd = gpio_fd_open(intn);

    cdctl_dev_init(&cdctl_dev, free_head, &bus_cfg, &spi_dev, NULL);
    cd_dev = &cdctl_dev.cd_dev;
    cd_rx_head = &cdctl_dev.rx_head;
    return intn_pin.fd;
}
