# kj-httpd

`busybox httpd` workalike in a single freestanding static binary.

This talks to the kernel directly, so it compiles to a true static binary with no dynamic linker underneath. It runs on any x86_64, aarch64 (arm64) or 32-bit armv6/armv7 linux no matter what is installed there, up to and including a broken or rescue system that has nothing to speak of.

**Deliberate deviations from busybox:** ipv4 only, no ipv6 sockets, and the server runs in the foreground by default with `-b` to send it to the background.

![usage screen screenshot](https://kj-media.online/pics/kj-httpd-scrot/kj-httpd-scrot.png)

## features

- supports most of the busybox httpd flag set: `-i -v[v] -p -u -r -h -c -m -e -d`
- static files with mime types, single byte ranges, etag 304s, last modified and gzip passthrough
- full httpd.conf support: acl, basic auth, custom error pages, index files, mime types, script interpreters and reverse proxy
- per directory httpd.conf, merged per request, same as busybox
- cgi with the usual environment, PATH_INFO, POST forwarding and Status: headers
- basic auth with plaintext, md5 crypt (`$1$`, what `-m` hands you), sha256 and sha512 crypt, and system passwords from /etc/passwd and /etc/shadow
- inetd mode and daemon mode, SIGHUP reloads the config
- one shot helpers: md5 crypt, html encode, url decode
- dns through /etc/hosts and /etc/resolv.conf, no libc needed
- builds with `gcc`, `clang` or `tcc` on x86_64, and with `gcc` or `clang` on aarch64 and armv7
- the bundled index.html is a browser native test page, handy for a quick smoke test

## dependencies

- a c compiler; `gcc`, `clang` and `tcc` all work
- `make`

## build and run

```sh
make
./kj-httpd -p 8080
```

Then open `http://127.0.0.1:8080/` and the bundled test page shows up.

The compiler is picked the usual way:

```sh
make CC=clang
make CC=tcc
```

tcc has its own linker and skips the tiny layout flags, so its build comes out a bit bigger (about 66k vs 37k). tcc is x86_64 only.

The architecture is detected from the compiler and can be overridden, so a plain `make` on an arm device just works. Optional cpu tuning:

```sh
make NATIVE=1
```

uses `-march=native -mtune=native` where the toolchain takes it (`-mcpu=native` on arm gcc). The untuned build sticks to the compiler default so the binary stays portable.

Cross compilation from x86_64:

```sh
make ARCH=aarch64 CC=aarch64-linux-gnu-gcc
make ARCH=arm CC=arm-linux-gnueabihf-gcc
make ARCH=arm CC=arm-linux-gnueabi-gcc
make ARCH=arm CC='clang --target=armv7-linux-gnueabihf'
```

clang cross builds route through lld automatically, gcc cross compilers use their own linker. Hard float and soft float arm toolchains both work, there is no floating point in here. Cross builds skip the final strip step since that needs a matching strip, `STRIP=` pins it when you have one.

## install

```sh
make install
```

installs to `~/.local/bin` by default, `PREFIX=` overrides the install location.

## uninstall

```sh
make remove
```

## usage

```
Usage: kj-httpd [-ibv[v]] [-c CONFFILE] [-p [IP:]PORT] [-u USER[:GRP]] [-r REALM] [-h HOME]
or kj-httpd -d/-e/-m STRING

Listen for incoming HTTP requests

	-i		        Inetd mode
	-b		        Run in background
	-v[v]		    Verbose
	-p [IP:]PORT	Bind to IP:PORT (default *:80)
	-u USER[:GRP]	Set uid/gid after binding to port
	-r REALM	    Authentication Realm for Basic Authentication
	-h HOME		    Home directory (default .)
	-c FILE		    Configuration file (default {/etc,HOME}/httpd.conf)
	-m STRING	    MD5 crypt STRING
	-e STRING	    HTML encode STRING
	-d STRING	    URL decode STRING
```

## httpd.conf

Same format as busybox httpd, searched at /etc/httpd.conf then ./httpd.conf unless `-c` is given.

```
H:/serverroot                    # change server root, overrides -h, chdirs into it
A:172.20.                        # allow address from 172.20.0.0/16
A:10.0.0.0/25                    # allow from 10.0.0.0-10.0.0.127
D:1.2.3.4/24                     # deny from 1.2.3.0-1.2.3.255
D:*                              # deny everything not allowed above
E404:/e404.html                  # custom 404 page, any supported error code works
I:index.html                     # index page for directory urls
P:/url:127.0.0.1:8080/new/path   # reverse proxy /urlXXX to the backend
.odd:application/x-odd           # extra mime type
*.php:/usr/bin/php               # run .php files through an interpreter
/cgi-bin:foo:bar                 # require user foo, pass bar on /cgi-bin
/secret:foo:$1$salt$hash         # password generated with kj-httpd -m
/wiki:*:*                        # any system user with the system password
```

H:, E: and P: are only honored from /etc/httpd.conf or a `-c` file, a ./httpd.conf fallback does not apply them.

Auth passwords accept plaintext, `$1$` (md5 crypt), `$5$` (sha256 crypt) and `$6$` (sha512 crypt). A user of `*` means any system user, a password of `*` means use that user's /etc/shadow password.

Subdirectories can carry their own httpd.conf, it is parsed when a request walks into the directory and merged for that request only, with auth and mime rules relative to the subdirectory.

## tests

```sh
make tests
```

Builds a docroot, runs busybox httpd and kj-httpd side by side and diffs the responses byte for byte, from plain gets to cgi, acl, auth, proxying, sighup reloads and a parent stability check. Needs busybox and nc, root only cases are skipped automatically.

## license

0BSD, see LICENSE.

The md5, sha256 and sha512 primitives follow the public domain rfc1321 and fips180-3 reference structures as used in musl libc, the crypt schemes follow the public domain sha-crypt spec and Poul-Henning Kamp's md5 crypt design.
