# kj-httpd: freestanding multi arch build, no libc

# examples:
#   make                                               native build with gcc
#   make NATIVE=1                                      tune for this exact cpu
#   make CC=clang                                      build with clang
#   make CC=tcc                                        tcc builds x86_64 only
#   make ARCH=aarch64 CC=aarch64-linux-gnu-gcc         cross aarch64
#   make ARCH=arm CC=arm-linux-gnueabihf-gcc           cross 32-bit arm
#   make ARCH=arm CC='clang --target=armv7-linux-gnueabihf'   clang cross

PREFIX ?= $(HOME)/.local

CC ?= cc
NATIVE ?= 0
STRIP ?= strip

# arch detect: compiler triple first, uname as fallback, then normalize
TRIPLE := $(shell $(CC) -dumpmachine 2>/dev/null)
TARCH := $(firstword $(subst -, ,$(TRIPLE)))

ifeq ($(ARCH),)
ARCH := $(TARCH)
endif
ifeq ($(ARCH),)
ARCH := $(shell uname -m)
endif

ifeq ($(ARCH),amd64)
ARCH := x86_64
else ifeq ($(ARCH),arm64)
ARCH := aarch64
endif

# armv4 up to armv8l all build the same 32-bit arm binary
ifneq (,$(filter armv4 armv4l armv5 armv5l armv6 armv6l armv7 armv7l armv8l,$(ARCH)))
ARCH := arm
endif

ifeq ($(filter x86_64 aarch64 arm,$(ARCH)),)
$(error unsupported arch "$(ARCH)", set ARCH=x86_64, aarch64 or arm)
endif

HOST := $(shell uname -m)
ifeq ($(HOST),amd64)
HOST := x86_64
endif
ifeq ($(HOST),arm64)
HOST := aarch64
endif
ifneq (,$(filter armv4 armv4l armv5 armv5l armv6 armv6l armv7 armv7l armv8l,$(HOST)))
HOST := arm
endif

# ld -n is an x86_64 thing, the rwx warning flag comes with it
ifeq ($(ARCH),x86_64)
LAYOUTFLAGS = -Wl,-n -Wl,--no-warn-rwx-segments \
		-Wl,--gc-sections -Wl,--build-id=none
EXTRAFLAGS =
else
LAYOUTFLAGS = -Wl,--gc-sections -Wl,--build-id=none
endif

ifeq ($(ARCH),aarch64)
EXTRAFLAGS = -mno-outline-atomics
else ifeq ($(ARCH),arm)
EXTRAFLAGS = -marm
endif

# gcc deeds -mcpu=native on the arm side, clang wants -march=native
NATIVEFLAGS =
ifeq ($(NATIVE),1)
ifeq ($(ARCH),x86_64)
NATIVEFLAGS = -march=native -mtune=native
else ifeq ($(ARCH),aarch64)
ifneq (,$(findstring clang,$(CC)))
NATIVEFLAGS = -march=native -mtune=native
else
NATIVEFLAGS = -mcpu=native -mtune=native
endif
else ifeq ($(ARCH),arm)
NATIVEFLAGS = -mcpu=native -mtune=native
endif
endif

# clang defaults to the host linker, cross builds need lld
LINKFLAGS =
ifneq (,$(findstring clang,$(CC)))
ifneq ($(ARCH),$(HOST))
LINKFLAGS = -fuse-ld=lld
endif
endif

# tcc ships its own linker and only knows x86_64, so layout flags go
# but the build stays a single call
ifneq (,$(findstring tcc,$(CC)))
ifneq ($(ARCH),x86_64)
$(error tcc only builds x86_64)
endif
LAYOUTFLAGS =
endif

kj-httpd: httpd.c
	$(CC) -Wall -Wextra -pedantic \
	  -s -Os -static -nostdlib -ffreestanding \
	  -fno-stack-protector -fdata-sections -ffunction-sections \
	  -fno-unwind-tables -fno-asynchronous-unwind-tables \
	  $(NATIVEFLAGS) $(EXTRAFLAGS) $(LINKFLAGS) $(LAYOUTFLAGS) \
	  httpd.c -o kj-httpd
ifeq ($(ARCH),$(HOST))
	$(STRIP) -R .comment kj-httpd
endif

tests: kj-httpd
	./tests/run.sh

install: kj-httpd
	mkdir -p $(PREFIX)/bin
	install -Dm755 kj-httpd $(PREFIX)/bin/kj-httpd

remove:
	rm -f $(PREFIX)/bin/kj-httpd

clean:
	rm -f kj-httpd

.PHONY: install remove clean tests
