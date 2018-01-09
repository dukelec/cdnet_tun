#
# Software License Agreement (BSD License)
#
# Copyright (c) 2017, DUKELEC, Inc.
# All rights reserved.
#
# Author: Duke Fong <duke@dukelec.com>
#

# Copy this Makefile to the same path of 6locd library,
# and create the main.c file

INCLUDES = \
cdnet/net \
cdnet/utils \
cdnet/dev \
cdnet/arch/pc \
ip \
tun

C_SOURCES = \
main.c \
cdnet/net/cdnet.c \
cdnet/dev/cdbus_uart.c \
cdnet/arch/pc/arch_wrapper.c \
cdnet/utils/list.c \
cdnet/utils/modbus_crc.c \
dev/cdbus_bridge.c \
ip/ip_cdnet_conversion.c \
ip/ip_checksum.c \
tun/tun.c \
utils/hex_dump.c


GIT_VERSION := $(shell git describe --dirty --always --tags)

CC = gcc

BUILD_DIR = build
TARGET = cdnet_tun

OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))

I_INCLUDES = $(foreach includedir,$(INCLUDES),-I$(includedir))
CFLAGS = $(I_INCLUDES) -DSW_VER=\"$(GIT_VERSION)\" -DVERBOSE
LDFLAGS =

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

