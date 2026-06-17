CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11

LIBUSB_CFLAGS := $(shell $(PKG_CONFIG) --cflags libusb-1.0)
LIBUSB_LIBS := $(shell $(PKG_CONFIG) --libs libusb-1.0)
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LIBS := $(shell $(PKG_CONFIG) --libs sdl2)

CPPFLAGS += -Iinclude

BUILD_DIR := build
TARGET := hti-usb-view
SRCS := \
	src/main.c \
	src/usb_device.c \
	src/frame_decode.c \
	src/ffc_state.c \
	src/thermo.c \
	src/palette.c \
	src/render_sdl.c \
	src/frame_save.c
OBJS := $(addprefix $(BUILD_DIR)/,$(SRCS:.c=.o))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LIBUSB_CFLAGS) $(SDL_CFLAGS) -o $@ $(OBJS) $(LIBUSB_LIBS) $(SDL_LIBS) -lm

$(BUILD_DIR)/%.o: %.c include/hti_usb_view.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIBUSB_CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean
