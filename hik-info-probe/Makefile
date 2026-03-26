CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11

LIBUSB_CFLAGS := $(shell $(PKG_CONFIG) --cflags libusb-1.0)
LIBUSB_LIBS := $(shell $(PKG_CONFIG) --libs libusb-1.0)
BUILD_DIR := build
TARGET := $(BUILD_DIR)/hik-info-probe
SRCS := hik_info_probe.c hik_cli.c hik_profiles.c hik_transport.c hik_enhancement.c hik_workloads.c
OBJS := $(addprefix $(BUILD_DIR)/,$(SRCS:.c=.o))

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LIBUSB_CFLAGS) -o $@ $(OBJS) $(LIBUSB_LIBS)

$(BUILD_DIR)/%.o: %.c hik_probe.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LIBUSB_CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean
