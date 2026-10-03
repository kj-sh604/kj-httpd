PREFIX ?= $(HOME)/.local

kj-httpd: start.S httpd.c
	gcc -Wall -Wextra -pedantic \
	  -s -Os -no-pie -nostdlib -ffreestanding \
	  -fno-stack-protector -fdata-sections -ffunction-sections \
	  -fno-unwind-tables -fno-asynchronous-unwind-tables \
	  -Wl,-n -Wl,--no-warn-rwx-segments \
	  -Wl,--gc-sections -Wl,--build-id=none \
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
