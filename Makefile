
PREFIX ?= $(HOME)/.local

CC = cc

ifneq (,$(findstring tcc,$(CC)))
LAYOUTFLAGS =
else
LAYOUTFLAGS = -Wl,-n -Wl,--no-warn-rwx-segments \
	  -Wl,--gc-sections -Wl,--build-id=none
endif

kj-httpd: start.S httpd.c
	$(CC) -Wall -Wextra -pedantic \
	  -s -Os -static -nostdlib -ffreestanding \
	  -fno-stack-protector -fdata-sections -ffunction-sections \
	  -fno-unwind-tables -fno-asynchronous-unwind-tables \
	  $(LAYOUTFLAGS) \
	  start.S httpd.c -o kj-httpd
	strip -R .comment kj-httpd

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
