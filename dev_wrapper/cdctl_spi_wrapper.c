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
#include <gpiod.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

#include "cdctl.h"
#include "main.h"

#define GPIO_CHIP_PATH  "/dev/gpiochip0"
#define CDCTL_MASK (CDBIT_FLAG_RX_PENDING | CDBIT_FLAG_RX_LOST | \
                    CDBIT_FLAG_RX_ERROR | CDBIT_FLAG_TX_CD | CDBIT_FLAG_TX_ERROR)

static int intn_pin = -1;
static struct gpiod_line_request *intn_request = NULL;
static struct gpiod_edge_event_buffer *event_buffer = NULL;

static uint32_t spi_speed = 20000000; // HZ
const char *def_dev = "/dev/spidev0.0";
static spi_t spi_dev = {0};

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


static bool gpio_get_intn(void)
{
    enum gpiod_line_value value = gpiod_line_request_get_value(intn_request, intn_pin);
    return value == GPIOD_LINE_VALUE_ACTIVE;
}


// Request a line as input with edge detection
static struct gpiod_line_request *request_input_line(const char *chip_path, unsigned int offset, const char *consumer)
{
    struct gpiod_request_config *req_cfg = NULL;
    struct gpiod_line_request *request = NULL;
    struct gpiod_line_settings *settings;
    struct gpiod_line_config *line_cfg;
    struct gpiod_chip *chip;
    int ret;

    chip = gpiod_chip_open(chip_path);
    if (!chip)
        return NULL;

    settings = gpiod_line_settings_new();
    if (!settings)
        goto close_chip;

    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_FALLING);

    line_cfg = gpiod_line_config_new();
    if (!line_cfg)
        goto free_settings;

    ret = gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);
    if (ret)
        goto free_line_config;

    if (consumer) {
        req_cfg = gpiod_request_config_new();
        if (!req_cfg)
            goto free_line_config;
        gpiod_request_config_set_consumer(req_cfg, consumer);
    }

    request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
    gpiod_request_config_free(req_cfg);

free_line_config:
    gpiod_line_config_free(line_cfg);
free_settings:
    gpiod_line_settings_free(settings);
close_chip:
    gpiod_chip_close(chip);
    return request;
}



static int gpio_fd_open(unsigned int offset)
{
    intn_request = request_input_line(GPIO_CHIP_PATH, offset, "cdctl-irq");

    if (!intn_request) {
        d_error("failed to request line: %s\n", strerror(errno));
        exit(-1);
    }

    int fd = gpiod_line_request_get_fd(intn_request);
    if (fd < 0) {
        d_error("gpiod_line_request_get_fd faild\n");
        exit(-1);
    }

    event_buffer = gpiod_edge_event_buffer_new(1); // size: 1
    if (!event_buffer) {
        d_error("gpiod_edge_event_buffer_new faild\n");
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
        int ret = gpiod_line_request_wait_edge_events(intn_request, 0);
        if (ret == 1) {
            ret = gpiod_line_request_read_edge_events(intn_request, event_buffer, 1); // size: 1
            if (ret == -1) {
                d_error("error reading edge events: %s\n", strerror(errno));
            }
        }
        cdctl_routine(&cdctl_dev);
        if (gpio_get_intn() && !cdctl_dev.tx_head.len && !cdctl_dev.is_pending)
            break;
    }
}

int cdctl_spi_wrapper_init(const char *dev_name, list_head_t *free_head, int intn)
{
    if (dev_name && *dev_name)
        def_dev = dev_name;

    spi_dev.fd = open(def_dev, O_RDWR);
    if(spi_dev.fd < 0) {
        d_error("open %s failed\n", def_dev);
        exit(-1);
    }
    if (ioctl(spi_dev.fd, SPI_IOC_WR_MAX_SPEED_HZ, &spi_speed) == -1) {
        d_error("can't set spi speed hz\n");
        exit(-1);
    }
    spi_dumpstat(&spi_dev);
    intn_pin = intn;
    int intn_pin_fd = gpio_fd_open(intn);

    cdctl_dev_init(&cdctl_dev, free_head, &bus_cfg, &spi_dev);
    cd_dev = &cdctl_dev.cd_dev;
    cd_rx_head = &cdctl_dev.rx_head;
    cdctl_reg_w(&cdctl_dev, CDREG_INT_MASK, CDCTL_MASK);

    return intn_pin_fd;
}
