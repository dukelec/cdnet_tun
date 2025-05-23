#
# Software License Agreement (BSD License)
#
# Copyright (c) 2017, DUKELEC, Inc.
# All rights reserved.
#
# Author: Duke Fong <d@d-l.io>
#

INCLUDES = \
cdnet/parser \
cdnet/core \
cdnet/utils \
cdnet/dev \
cdnet/arch/pc \
ip \
tun \
usr

C_SOURCES = \
usr/main.c \
usr/cd_args.c \
cdnet/parser/cdnet.c \
cdnet/parser/cdnet_l0.c \
cdnet/parser/cdnet_l1.c \
cdnet/dev/cdbus_uart.c \
cdnet/arch/pc/arch_wrapper.c \
cdnet/utils/cd_list.c \
cdnet/utils/modbus_crc.c \
cdnet/utils/hex_dump.c \
dev_wrapper/cdbus_tty_wrapper.c \
dev_wrapper/linux_dev_wrapper.c \
ip/ip_cdnet_conversion.c \
ip/ip_checksum.c \
tun/tun.c


GIT_VERSION := $(shell git describe --dirty --always --tags)

CC = gcc

BUILD_DIR = build
TARGET = cdnet_tun

OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))

I_INCLUDES = $(foreach includedir,$(INCLUDES),-I$(includedir))
CFLAGS = $(I_INCLUDES) -DSW_VER=\"$(GIT_VERSION)\"
LDFLAGS =

ifeq ($(USE_SPI),1)
    C_SOURCES += dev_wrapper/cdctl_spi_wrapper.c \
                 cdnet/dev/cdctl_pll_cal.c \
                 cdnet/dev/cdctl.c
    LDFLAGS += -lgpiod
    CFLAGS += -DUSE_SPI
endif


DEPS = $(foreach includedir,$(INCLUDES),$(wildcard $(includedir)/*.h))

$(BUILD_DIR)/%.o: %.c $(DEPS) Makefile | $(BUILD_DIR)
	$(CC) -c -o $@ $< $(CFLAGS)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR):
	mkdir $@

.PHONY: clean

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

