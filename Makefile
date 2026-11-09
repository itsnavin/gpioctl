# gpioctl Makefile
#
# Native build (on the Pi):
#   make
#
# Cross compile (e.g. from an x86_64 PC for Raspberry Pi 5, 64-bit OS):
#   make CROSS_COMPILE=aarch64-linux-gnu-
#   make CROSS_COMPILE=aarch64-linux-gnu- STATIC=1      # fully static binary
#   make CROSS_COMPILE=arm-linux-gnueabihf-             # 32-bit Raspberry Pi OS
#   make CC=clang CFLAGS="--target=aarch64-linux-gnu -O2"
#
# Staged install (packaging / rootfs):
#   make install DESTDIR=/path/to/rootfs PREFIX=/usr

CROSS_COMPILE ?=

# Make predefines CC=cc, so "?=" would never apply. Only override the
# default value; respect CC given on the command line or environment.
ifeq ($(origin CC),default)
CC := $(CROSS_COMPILE)gcc
endif
STRIP ?= $(CROSS_COMPILE)strip

CFLAGS   ?= -O2 -Wall
CPPFLAGS ?=
LDFLAGS  ?=
LDLIBS   ?=

ifeq ($(STATIC),1)
LDFLAGS += -static
endif

PREFIX ?= /usr/local
BINDIR := $(PREFIX)/bin
TARGET := gpioctl

# Version: from git tag (v1.2.3 -> 1.2.3) if available, else 1.0.0
VERSION ?= $(shell git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//')
ifeq ($(strip $(VERSION)),)
VERSION := 1.0.0
endif
CPPFLAGS += -DGPIOCTL_VERSION=\"$(VERSION)\"

DIST := dist

.PHONY: all install install-strip strip clean info release

all: $(TARGET)

$(TARGET): gpioctl.c gpio_pins.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ gpioctl.c $(LDLIBS)

# Static, stripped release binaries for 64-bit and 32-bit Raspberry Pi OS,
# packaged as tarballs with SHA-256 checksums in ./dist
# Needs: gcc-aarch64-linux-gnu gcc-arm-linux-gnueabihf
release:
	rm -rf $(DIST) && mkdir -p $(DIST)
	@set -e; for t in aarch64:aarch64-linux-gnu- armhf:arm-linux-gnueabihf-; do \
	  arch=$${t%%:*}; cc=$${t#*:}; \
	  name=gpioctl-$(VERSION)-linux-$$arch; \
	  mkdir -p $(DIST)/$$name; \
	  echo "==> $$name"; \
	  $${cc}gcc $(CPPFLAGS) -O2 -Wall -static -o $(DIST)/$$name/gpioctl gpioctl.c; \
	  $${cc}strip $(DIST)/$$name/gpioctl; \
	  cp README.md LICENSE $(DIST)/$$name/ 2>/dev/null || true; \
	  tar -C $(DIST) -czf $(DIST)/$$name.tar.gz $$name; \
	  cp $(DIST)/$$name/gpioctl $(DIST)/gpioctl-linux-$$arch; \
	  rm -rf $(DIST)/$$name; \
	done
	cd $(DIST) && sha256sum *.tar.gz gpioctl-linux-* > SHA256SUMS
	@ls -l $(DIST)

strip: $(TARGET)
	$(STRIP) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

install-strip: strip install

info:
	@echo "VERSION = $(VERSION)"
	@echo "CC      = $(CC)"
	@echo "CFLAGS  = $(CFLAGS)"
	@echo "LDFLAGS = $(LDFLAGS)"
	@echo "Target  = $$($(CC) -dumpmachine)"

clean:
	rm -f $(TARGET)
	rm -rf $(DIST)
