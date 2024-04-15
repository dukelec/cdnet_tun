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
cdnet/dispatch \
cdnet/utils \
cdnet/dev \
cdnet/arch/pc \
ip \
tun \
usr

C_SOURCES = \
usr/main.c \
usr/cd_args.c \
cdnet/parser/cdnet_l1.c \
cdnet/dev/cdbus_uart.c \
cdnet/dev/cdctl.c \
cdnet/arch/pc/arch_wrapper.c \
cdnet/utils/cd_list.c \
cdnet/utils/modbus_crc.c \
cdnet/utils/hex_dump.c \
dev_wrapper/cdbus_tty_wrapper.c \
dev_wrapper/cdctl_spi_wrapper.c \
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
LDFLAGS = -lgpiod

DEPS = $(foreach includedir,$(INCLUDES),$(wildcard $(includedir)/*.h))

$(BUILD_DIR)/%.o: %.c $(DEPS) Makefile | $(BUILD_DIR)
	$(CC) -c -o $@ $< $(CFLAGS)

$(TARGET): $(OBJECTS)
	gcc -o $@ $^ $(LDFLAGS)

$(BUILD_DIR):
	mkdir $@

.PHONY: clean

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

