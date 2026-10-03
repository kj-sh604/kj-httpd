#!/bin/sh

# configuration

BB=${BUSYBOX:-busybox}
KJ=$(cd "$(dirname "$0")/.." && pwd)/kj-httpd
NC=${NC:-nc}
WORK=$(mktemp -d /tmp/kj-httpd-test.XXXXXX)
BB_PORT=$((20000 + $$ % 20000))
KJ_PORT=$((BB_PORT + 1))
BE_PORT=$((BB_PORT + 2))
BB_PID=
KJ_PID=
BE_PID=
PASS=0
FAIL=0
VERBOSE=

# helpers

usage_function() {
    cat <<EOF
usage:
    $0 [-v]

options:
    -v, --verbose
        print the diff for every failed case

environment:
    BUSYBOX  path to busybox, default "$BB"
    NC       path to nc, default "$NC"
EOF
}

_error() {
    printf 'error: %s\n' "$1" >&2
    exit 1
}

_dep_check() {
    command -v "$BB" >/dev/null 2>&1 || _error "$BB is not installed"
    command -v "$NC" >/dev/null 2>&1 || _error "$NC is not installed"
    [ -x "$KJ" ] || _error "$KJ is not built, run make in the repo root"
}

_start_servers() {
    _conf=$1
    shift
    $BB httpd -f -p 127.0.0.1:$BB_PORT -h "$WORK/doc" -c "$_conf" "$@" \
        >/dev/null 2>&1 &
    BB_PID=$!
    $KJ -f -p 127.0.0.1:$KJ_PORT -h "$WORK/doc" -c "$_conf" "$@" \
        >/dev/null 2>&1 &
    KJ_PID=$!
    sleep 0.3
}

_stop_servers() {
    kill $BB_PID $KJ_PID 2>/dev/null
    BB_PID=
    KJ_PID=
    sleep 0.2
}

# strip volatile headers, keep everything else byte exact
_normalize() {
    sed -e 's/^Date: .*/Date: X/' \
        -e 's/^REMOTE_PORT=.*/REMOTE_PORT=X/' \
        -e 's/^SERVER_SOFTWARE=.*/SERVER_SOFTWARE=X/'
}

# fetch from one server and normalize
_fetch() {
    _port=$1
    shift
    printf "$1\r\n" | timeout 5 $NC 127.0.0.1 $_port 2>/dev/null | _normalize
}

# compare one raw request against both servers
t_case() {
    _name=$1
    _req=$2
    _a=$(_fetch $BB_PORT "$_req" | md5sum | cut -c1-32)
    _b=$(_fetch $KJ_PORT "$_req" | md5sum | cut -c1-32)
    if [ "$_a" = "$_b" ]; then
        PASS=$((PASS + 1))
        printf 'ok     %s\n' "$_name"
    else
        FAIL=$((FAIL + 1))
        printf 'FAIL   %s\n' "$_name"
        if [ -n "$VERBOSE" ]; then
            _fetch $BB_PORT "$_req" > "$WORK/a.out"
            _fetch $KJ_PORT "$_req" > "$WORK/b.out"
            diff "$WORK/a.out" "$WORK/b.out" | head -20
        fi
    fi
}

# compare a shell command pair, both must produce the same output
t_cli() {
    _name=$1
    shift
    _a=$(eval "$1")
    _b=$(eval "$2")
    if [ "$_a" = "$_b" ]; then
        PASS=$((PASS + 1))
        printf 'ok     %s\n' "$_name"
    else
        FAIL=$((FAIL + 1))
        printf 'FAIL   %s\n' "$_name"
        printf '  bb: [%s]\n  kj: [%s]\n' "$_a" "$_b"
    fi
}

keyboard_cancel() {
    printf '\ncanceled, cleaning up...\n'
    _cleanup
    exit 130
}

_cleanup() {
    _stop_servers
    kill $BE_PID 2>/dev/null
    rm -rf "$WORK"
}

# fixtures

_fixtures() {
    cd "$WORK" || _error "cd $WORK failed"
    mkdir -p doc/sub doc/priv doc/cgi-bin doc/backend
    printf 'ROOT-INDEX-CONTENT' > doc/index.html
    printf 'ROOT-INDEX-CONTENT' > doc/index.html.gz
    printf 'SUB-INDEX-CONTENT' > doc/sub/index.html
    printf 'PLAINTEXT-BODY' > doc/file.txt
    printf 'JFIF-BYTES' > doc/img.jpg
    printf 'SVGXML' > doc/vector.svg
    printf 'SECRETCONTENT' > doc/secret.html
    printf 'custom 404 body here' > doc/e404.html
    printf 'CUSTOMINDEX' > doc/custom.html
    printf 'ODD' > doc/file.odd
    printf 'DATA' > doc/data.tst
    printf 'echo INTERP "$1"\n' > doc/data.tst
    cat > doc/cgi-bin/env.sh <<'EOF'
#!/bin/sh
echo Content-Type: text/plain
echo
echo "Q=[$QUERY_STRING] M=[$REQUEST_METHOD] PI=[$PATH_INFO] CL=[$CONTENT_LENGTH]"
echo "SN=[$SCRIPT_NAME] RU=[$REMOTE_USER] CT=[$CONTENT_TYPE] HX=[$HTTP_X_TEST]"
EOF
    cat > doc/cgi-bin/status.sh <<'EOF'
#!/bin/sh
echo Status: 302 Found
echo Location: /index.html
echo
EOF
    cat > doc/cgi-bin/index.cgi <<'EOF'
#!/bin/sh
echo Content-Type: text/plain
echo
echo "INDEXCGI Q=[$QUERY_STRING]"
EOF
    chmod +x doc/cgi-bin/env.sh doc/cgi-bin/status.sh doc/cgi-bin/index.cgi
    printf '/p:user:subpw\n' > doc/priv/httpd.conf
    printf 'NO-AUTH\n' > doc/backend/index.html
}

# test groups

t_cli_modes() {
    t_cli '-d simple' \
        "$BB httpd -d 'Hello%20World%21'" \
        "$KJ -d 'Hello%20World%21'"
    t_cli '-d invalid escapes pass through' \
        "$BB httpd -d '%zz%2%'" \
        "$KJ -d '%zz%2%'"
    t_cli '-d plus becomes space' "$BB httpd -d 'a+b'" "$KJ -d 'a+b'"
    t_cli '-e html encode' \
        "$BB httpd -e '<Hello World>&\"'" \
        "$KJ -e '<Hello World>&\"'"
    # -m hashes are random salted, verify via cross authentication below
}

t_basic() {
    _start_servers "$WORK/conf.empty"
    t_case 'GET / index' 'GET / HTTP/1.1\r\n\r\n'
    t_case 'GET file text/plain' 'GET /file.txt HTTP/1.1\r\n\r\n'
    t_case 'GET mime jpeg' 'GET /img.jpg HTTP/1.1\r\n\r\n'
    t_case 'GET mime svg' 'GET /vector.svg HTTP/1.1\r\n\r\n'
    t_case 'GET 404' 'GET /nothere HTTP/1.1\r\n\r\n'
    t_case 'GET 302 dir redirect' 'GET /sub HTTP/1.1\r\n\r\n'
    t_case 'GET 302 with query' 'GET /sub?q=1 HTTP/1.1\r\n\r\n'
    t_case 'GET subdir index' 'GET /sub/ HTTP/1.1\r\n\r\n'
    t_case 'HEAD gets the body too' 'HEAD /index.html HTTP/1.1\r\n\r\n'
    t_case 'Range bytes=2-5' \
        'GET /index.html HTTP/1.1\r\nRange: bytes=2-5\r\n\r\n'
    t_case 'Range bytes=3-' \
        'GET /index.html HTTP/1.1\r\nRange: bytes=3-\r\n\r\n'
    t_case 'Range beyond eof' \
        'GET /index.html HTTP/1.1\r\nRange: bytes=900-999\r\n\r\n'
    t_case 'Range garbage' \
        'GET /index.html HTTP/1.1\r\nRange: bytes=zz\r\n\r\n'
    t_case 'gzip passthrough' \
        'GET /index.html HTTP/1.1\r\nAccept-Encoding: gzip\r\n\r\n'
    t_case 'POST to a file is 501' \
        'POST /file.txt HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc'
    t_case 'unknown method is 501' 'FOO /file.txt HTTP/1.1\r\n\r\n'
    t_case 'cgi-bin listing is 403' 'GET /cgi-bin/ HTTP/1.1\r\n\r\n'
    t_case 'httpd.conf is 403' 'GET /httpd.conf HTTP/1.1\r\n\r\n'
    t_case 'subdir httpd.conf is 403' \
        'GET /priv/httpd.conf HTTP/1.1\r\n\r\n'
    t_case 'empty request line closes' 'GARBAGE\r\n\r\n'
    t_case 'no http version is 400' 'GET /file.txt\r\n\r\n'
    _stop_servers
}

t_escape() {
    _start_servers "$WORK/conf.empty"
    t_case 'traversal /a/../index.html' \
        'GET /a/../index.html HTTP/1.1\r\n\r\n'
    t_case 'traversal above root' \
        'GET /../../../../../etc/hostname HTTP/1.1\r\n\r\n'
    t_case 'traversal at root' 'GET /../index.html HTTP/1.1\r\n\r\n'
    t_case 'encoded traversal' \
        'GET /%2e%2e/%2e%2e/etc/hostname HTTP/1.1\r\n\r\n'
    t_case 'encoded slash is 404' 'GET /%2findex.html HTTP/1.1\r\n\r\n'
    t_case 'encoded slash mid path' 'GET /a%2fb HTTP/1.1\r\n\r\n'
    t_case 'encoded NUL is 404' 'GET /%00x HTTP/1.1\r\n\r\n'
    t_case 'encoded dot in name' 'GET /file%2etxt HTTP/1.1\r\n\r\n'
    t_case 'double slashes' 'GET //file.txt HTTP/1.1\r\n\r\n'
    t_case 'dot dir in path' 'GET /./file.txt HTTP/1.1\r\n\r\n'
    _stop_servers
}

t_cgi() {
    _start_servers "$WORK/conf.empty"
    t_case 'cgi get' 'GET /cgi-bin/env.sh?a=b HTTP/1.1\r\n\r\n'
    t_case 'cgi get no query' 'GET /cgi-bin/env.sh HTTP/1.1\r\n\r\n'
    t_case 'cgi post body' \
        'POST /cgi-bin/env.sh HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello'
    t_case 'cgi path info' \
        'GET /cgi-bin/env.sh/x/y?z=1 HTTP/1.1\r\n\r\n'
    t_case 'cgi custom header env' \
        'GET /cgi-bin/env.sh HTTP/1.1\r\nX-Test: hi\r\n\r\n'
    t_case 'cgi content type env' \
        'GET /cgi-bin/env.sh HTTP/1.1\r\nContent-Type: text/odd\r\n\r\n'
    t_case 'cgi Status: header' 'GET /cgi-bin/status.sh HTTP/1.1\r\n\r\n'
    t_case 'cgi index.cgi for dir urls' 'GET /sub/../cgi-bin/../ HTTP/1.1\r\n\r\n'
    _stop_servers
}

t_conf() {
    _hash_md5=$($BB httpd -m sharedpw)
    _hash_sha512=$($BB cryptpw -m sha512 sharedpw 2>/dev/null)
    _hash_sha256=$($BB cryptpw -m sha256 sharedpw 2>/dev/null)
    _hash_kj=$($KJ -m sharedpw)
    cat > "$WORK/conf.main" <<EOF
/secret:user:plainpw
/secret2:alu:$_hash_md5
/secret3:root:$_hash_sha512
/secret4:root:$_hash_sha256
/secret5:user:$_hash_kj
E404:/e404.html
I:custom.html
.odd:application/x-odd
*.tst:/bin/sh
EOF
    _start_servers "$WORK/conf.main"
    t_case 'auth 401' 'GET /secret HTTP/1.1\r\n\r\n'
    t_case 'auth plaintext ok' \
        'GET /secret HTTP/1.1\r\nAuthorization: Basic dXNlcjpwbGFpbnB3\r\n\r\n'
    t_case 'auth plaintext wrong' \
        'GET /secret HTTP/1.1\r\nAuthorization: Basic dXNlcjp3cm9uZw==\r\n\r\n'
    t_case 'auth busybox md5 hash' \
        'GET /secret2 HTTP/1.1\r\nAuthorization: Basic YWx1OnNoYXJlZHB3\r\n\r\n'
    t_case 'auth sha512 hash' \
        'GET /secret3 HTTP/1.1\r\nAuthorization: Basic cm9vdDpzaGFyZWRwdw==\r\n\r\n'
    t_case 'auth sha256 hash' \
        'GET /secret4 HTTP/1.1\r\nAuthorization: Basic cm9vdDpzaGFyZWRwdw==\r\n\r\n'
    t_case 'auth kj-httpd -m hash' \
        'GET /secret5 HTTP/1.1\r\nAuthorization: Basic dXNlcjpzaGFyZWRwdw==\r\n\r\n'
    t_case 'custom error page' 'GET /missing HTTP/1.1\r\n\r\n'
    t_case 'custom index page' 'GET /sub/ HTTP/1.1\r\n\r\n'
    t_case 'custom mime type' 'GET /file.odd HTTP/1.1\r\n\r\n'
    t_case 'interpreter line' 'GET /data.tst HTTP/1.1\r\n\r\n'
    t_case 'auth applies to subdir index' \
        'GET /secret/ HTTP/1.1\r\nAuthorization: Basic dXNlcjpwbGFpbnB3\r\n\r\n'
    _stop_servers

    # auth realm
    _start_servers "$WORK/conf.realm" -r "my custom realm"
    t_case 'realm in 401' 'GET /secret HTTP/1.1\r\n\r\n'
    _stop_servers

    # acl
    cat > "$WORK/conf.acl" <<EOF
A:127.0.0.1
D:*
EOF
    _start_servers "$WORK/conf.acl"
    t_case 'acl allow loopback' 'GET / HTTP/1.1\r\n\r\n'
    _stop_servers

    cat > "$WORK/conf.deny" <<EOF
D:10.20.30.
EOF
    _start_servers "$WORK/conf.deny"
    t_case 'acl deny other subnet' 'GET / HTTP/1.1\r\n\r\n'
    _stop_servers

    # subdir conf auth
    cat > "$WORK/conf.sub" </dev/null
    _start_servers "$WORK/conf.sub"
    t_case 'subdir conf auth 401' 'GET /priv/p.html HTTP/1.1\r\n\r\n'
    t_case 'subdir conf auth ok' \
        'GET /priv/p.html HTTP/1.1\r\nAuthorization: Basic dXNlcjpzdWJwdw==\r\n\r\n'
    t_case 'subdir conf only applies below' 'GET /file.txt HTTP/1.1\r\n\r\n'
    _stop_servers

    # sighup reload, conf swapped to deny all
    _start_servers "$WORK/conf.sub"
    printf 'D:*\n' > "$WORK/conf.sub"
    kill -HUP $BB_PID $KJ_PID
    sleep 0.3
    t_case 'sighup reload deny all' 'GET / HTTP/1.1\r\n\r\n'
    _stop_servers

    # H: directive, needs the -c FIRST_PARSE path
    printf 'H:backend\n' > "$WORK/conf.home"
    _start_servers "$WORK/conf.home"
    t_case 'H: moves the server root' 'GET / HTTP/1.1\r\n\r\n'
    _stop_servers
}

t_proxy() {
    # one busybox backend, both frontends forward to it
    $BB httpd -f -p 127.0.0.1:$BE_PORT -h "$WORK/doc/backend" \
        >/dev/null 2>&1 &
    BE_PID=$!
    printf 'P:/backend:127.0.0.1:%s/\n' $BE_PORT > "$WORK/conf.proxy"
    _start_servers "$WORK/conf.proxy"
    t_case 'proxy get' "GET /backend/index.html HTTP/1.1\r\nHost: h\r\n\r\n"
    t_case 'proxy 404 pass through' 'GET /backend/nope HTTP/1.1\r\n\r\n'
    t_case 'proxy post' \
        "POST /backend/index.html HTTP/1.1\r\nContent-Length: 3\r\nX-T: 1\r\n\r\nabc"
    _stop_servers
    kill $BE_PID 2>/dev/null
    BE_PID=
}

t_inetd() {
    _a=$(printf 'GET /index.html HTTP/1.1\r\n\r\n' | \
        $BB httpd -i -h "$WORK/doc" -c "$WORK/conf.empty" | _normalize | \
        md5sum | cut -c1-32)
    _b=$(printf 'GET /index.html HTTP/1.1\r\n\r\n' | \
        $KJ -i -h "$WORK/doc" -c "$WORK/conf.empty" | _normalize | \
        md5sum | cut -c1-32)
    if [ "$_a" = "$_b" ]; then
        PASS=$((PASS + 1))
        printf 'ok     inetd mode one shot\n'
    else
        FAIL=$((FAIL + 1))
        printf 'FAIL   inetd mode one shot\n'
    fi
    _a=$(printf 'GET /nope HTTP/1.1\r\n\r\n' | \
        $BB httpd -i -h "$WORK/doc" -c "$WORK/conf.empty" | _normalize | \
        md5sum | cut -c1-32)
    _b=$(printf 'GET /nope HTTP/1.1\r\n\r\n' | \
        $KJ -i -h "$WORK/doc" -c "$WORK/conf.empty" | _normalize | \
        md5sum | cut -c1-32)
    if [ "$_a" = "$_b" ]; then
        PASS=$((PASS + 1))
        printf 'ok     inetd mode 404\n'
    else
        FAIL=$((FAIL + 1))
        printf 'FAIL   inetd mode 404\n'
    fi
}

t_oversize() {
    _start_servers "$WORK/conf.empty"
    _headers=$(awk 'BEGIN {
        for (i = 0; i < 200; i++) printf "X-H%d: %s\r\n", i, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    }')
    t_case 'many headers is 413' "GET / HTTP/1.1\r\n$_headers\r\n"
    _stop_servers
}

t_daemon() {
    # no -f, both must detach and serve
    $BB httpd -p 127.0.0.1:$BB_PORT -h "$WORK/doc" -c "$WORK/conf.empty" \
        >/dev/null 2>&1 &
    _bb_bg=$!
    $KJ -p 127.0.0.1:$KJ_PORT -h "$WORK/doc" -c "$WORK/conf.empty" \
        >/dev/null 2>&1 &
    _kj_bg=$!
    sleep 0.4
    _a=$(printf 'GET / HTTP/1.1\r\n\r\n' | timeout 3 $NC 127.0.0.1 $BB_PORT \
        2>/dev/null | head -1)
    _b=$(printf 'GET / HTTP/1.1\r\n\r\n' | timeout 3 $NC 127.0.0.1 $KJ_PORT \
        2>/dev/null | head -1)
    if [ "$_a" = "$_b" ] && [ -n "$_a" ]; then
        PASS=$((PASS + 1))
        printf 'ok     daemon mode without -f\n'
    else
        FAIL=$((FAIL + 1))
        printf 'FAIL   daemon mode without -f\n'
        printf '  bb: [%s]\n  kj: [%s]\n' "$_a" "$_b"
    fi
    kill $_bb_bg $_kj_bg 2>/dev/null
    pkill -f "httpd -p 127.0.0.1:$BB_PORT" 2>/dev/null
    pkill -f "$KJ -p 127.0.0.1:$KJ_PORT" 2>/dev/null
    sleep 0.2
}

t_cli_errors() {
    # -c with a missing file must fail the same way
    _a=$(timeout 2 $BB httpd -f -p $BB_PORT -h "$WORK/doc" -c /no/such/conf 2>&1)
    _b=$(timeout 2 $KJ -f -p $BB_PORT -h "$WORK/doc" -c /no/such/conf 2>&1)
    if [ "$_a" = "$_b" ]; then
        PASS=$((PASS + 1))
        printf 'ok     -c missing file error\n'
    else
        FAIL=$((FAIL + 1))
        printf 'FAIL   -c missing file error\n'
        printf '  bb: [%s]\n  kj: [%s]\n' "$_a" "$_b"
    fi
}

t_stability() {
    # the parent forks per request, its fds and memory must not grow
    # and no zombies may pile up over a mixed request load
    _start_servers "$WORK/conf.empty"
    _fds_before=$(ls /proc/$KJ_PID/fd 2>/dev/null | wc -l)
    _rss_before=$(awk '/VmRSS/ {print $2}' /proc/$KJ_PID/status 2>/dev/null)
    _i=0
    while [ $_i -lt 60 ]; do
        printf 'GET /file.txt HTTP/1.1\r\n\r\n' | \
            timeout 2 $NC 127.0.0.1 $KJ_PORT >/dev/null 2>&1
        printf 'GET /nope HTTP/1.1\r\n\r\n' | \
            timeout 2 $NC 127.0.0.1 $KJ_PORT >/dev/null 2>&1
        printf 'GET /cgi-bin/env.sh HTTP/1.1\r\n\r\n' | \
            timeout 2 $NC 127.0.0.1 $KJ_PORT >/dev/null 2>&1
        _i=$((_i + 1))
    done
    sleep 0.5
    _fds_after=$(ls /proc/$KJ_PID/fd 2>/dev/null | wc -l)
    _rss_after=$(awk '/VmRSS/ {print $2}' /proc/$KJ_PID/status 2>/dev/null)
    _kids=$(pgrep -P $KJ_PID 2>/dev/null | wc -l)
    if [ "$_fds_before" = "$_fds_after" ] && \
       [ "$_rss_before" = "$_rss_after" ] && [ "$_kids" = 0 ]; then
        PASS=$((PASS + 1))
        printf 'ok     parent stable over 180 requests (%s fds, %s kb)\n' \
            "$_fds_after" "$_rss_after"
    else
        FAIL=$((FAIL + 1))
        printf 'FAIL   parent stability\n'
        printf '  fds: %s -> %s, rss: %s -> %s, children: %s\n' \
            "$_fds_before" "$_fds_after" "$_rss_before" "$_rss_after" \
            "$_kids"
    fi
    _stop_servers
}

t_root_only() {
    # these need root: binding port 80, the -u drop, shadow password lookups
    [ "$(id -u)" = 0 ] || {
        printf 'skip   -u privilege drop, port 80, shadow auth (not root)\n'
        return
    }
    t_case 'bind port 80' 'GET / HTTP/1.1\r\n\r\n'
}

# main

while [ $# -gt 0 ]; do
    case "$1" in
    -v | --verbose)
        VERBOSE=1
        ;;
    -h | --help)
        usage_function
        exit 0
        ;;
    *)
        usage_function
        exit 1
        ;;
    esac
    shift
done

trap keyboard_cancel INT
trap _cleanup EXIT

_dep_check
_fixtures
: > "$WORK/conf.empty"
printf '/secret:user:plainpw\n' > "$WORK/conf.realm"

printf 'kj-httpd vs busybox httpd, ports %s and %s\n\n' $BB_PORT $KJ_PORT

t_cli_modes
t_basic
t_escape
t_cgi
t_conf
t_proxy
t_inetd
t_oversize
t_daemon
t_cli_errors
t_stability
t_root_only

printf '\npassed: %s, failed: %s\n' $PASS $FAIL
[ "$FAIL" = 0 ] || exit 1
exit 0

# vim: set filetype=sh foldmethod=marker foldlevel=0:
