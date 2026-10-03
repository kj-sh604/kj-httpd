#include <stdarg.h>
#include <stddef.h>

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef long int64_t;
typedef unsigned long size_t;
typedef long ssize_t;
typedef unsigned socklen_t;

#define VERSION_STR "20261002"
#define SERVER_SOFTWARE "kj-httpd/" VERSION_STR
#define NORETURN __attribute__((noreturn))

/* config
 */

#define IOBUF_SIZE 8192
#define MAX_HTTP_HEADERS_SIZE (32 * 1024)
#define HEADER_READ_TIMEOUT 60
#define CONF_LINE_MAX 256
#define PATH_BUF 4096

/* limits for the static config tables, extra entries are skipped */
#define MAX_MIME 64
#define MAX_INTERP 8
#define MAX_AUTH 32
#define MAX_IP_RULE 32
#define MAX_PROXY 8
#define MAX_CGI_ENV 200
#define CGI_ENV_BUF 32768

typedef struct {
  uint16_t sin_family;
  uint16_t sin_port; /* big endian */
  uint32_t sin_addr; /* big endian */
  char sin_zero[8];
} sockaddr_in_t;

/* x86_64 struct stat as filled by the stat syscall */
typedef struct {
  uint64_t st_dev;
  uint64_t st_ino;
  uint64_t st_nlink;
  uint32_t st_mode;
  uint32_t st_uid;
  uint32_t st_gid;
  uint32_t __pad0;
  uint64_t st_rdev;
  int64_t st_size;
  int st_blksize;
  int __pad1;
  int64_t st_blocks;
  int64_t st_atime, st_atime_nsec;
  int64_t st_mtime, st_mtime_nsec;
  int64_t st_ctime, st_ctime_nsec;
  uint64_t __unused[3];
} stat_t;

struct timeval_t {
  long tv_sec;
  long tv_usec;
};

struct pollfd_t {
  int fd;
  short events;
  short revents;
};

struct kernel_sigaction {
  void (*handler)(int);
  unsigned long flags;
  void (*restorer)(void);
  unsigned long mask;
};

typedef void (*sighandler_t)(int);

#define SA_RESTORER 0x04000000
#define SIG_HUP 1
#define SIG_INT 2
#define SIG_PIPE 13
#define SIG_ALRM 14
#define SIG_CHLD 17
#define SIG_IGN_PTR ((sighandler_t)1)
#define SIG_DFL_PTR ((sighandler_t)0)

#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_KEEPALIVE 9
#define O_RDONLY 0
#define R_OK 4
#define X_OK 1
#define SEEK_SET 0
#define SHUT_WR 1
#define S_IFMT 0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define POLLIN 1
#define POLLOUT 4
#define EINTR 4

enum {
  HTTP_OK = 200,
  HTTP_PARTIAL_CONTENT = 206,
  HTTP_MOVED_TEMPORARILY = 302,
  HTTP_NOT_MODIFIED = 304,
  HTTP_BAD_REQUEST = 400,
  HTTP_UNAUTHORIZED = 401,
  HTTP_FORBIDDEN = 403,
  HTTP_NOT_FOUND = 404,
  HTTP_REQUEST_TIMEOUT = 408,
  HTTP_ENTITY_TOO_LARGE = 413,
  HTTP_INTERNAL_SERVER_ERROR = 500,
  HTTP_NOT_IMPLEMENTED = 501,
};

static const struct {
  int code;
  const char *name;
  const char *info;
} http_responses[] = {
    {HTTP_OK, "OK", 0},
    {HTTP_PARTIAL_CONTENT, "Partial Content", 0},
    {HTTP_MOVED_TEMPORARILY, "Found", 0},
    {HTTP_NOT_MODIFIED, "Not Modified", 0},
    {HTTP_BAD_REQUEST, "Bad Request", "Unsupported method"},
    {HTTP_UNAUTHORIZED, "Unauthorized", ""},
    {HTTP_FORBIDDEN, "Forbidden", ""},
    {HTTP_NOT_FOUND, "Not Found", "The requested URL was not found"},
    {HTTP_REQUEST_TIMEOUT, "Request Timeout",
     "No request appeared within 60 seconds"},
    {HTTP_ENTITY_TOO_LARGE, "Entity Too Large", "Entity Too Large"},
    {HTTP_INTERNAL_SERVER_ERROR, "Internal Server Error",
     "Internal Server Error"},
    {HTTP_NOT_IMPLEMENTED, "Not Implemented",
     "The requested method is not recognized"},
};
#define N_RESPONSES (sizeof(http_responses) / sizeof(http_responses[0]))

static const char HTTP_200[] = "HTTP/1.1 200 OK\r\n";
static const char HTTPD_CONF_NAME[] = "httpd.conf";
static const char DEFAULT_PATH_HTTPD_CONF[] = "/etc";
static const char index_html[] = "index.html";

enum { SEND_HEADERS = 1, SEND_BODY = 2 };

enum {
  FIRST_PARSE = 0,    /* called from main, path is /etc */
  SIGNALED_PARSE = 1, /* called from the SIGHUP handler */
  SUBDIR_PARSE = 2,   /* per directory httpd.conf, merged per request */
  TRY_CURDIR_PARSE = 3,
};

enum { CGI_NONE = 0, CGI_NORMAL, CGI_INDEX, CGI_INTERPRETER };

/* raw syscall wrappers from start.S, return negative errno on error
 */

extern long k_read(int fd, void *buf, size_t n);
extern long k_write(int fd, const void *buf, size_t n);
extern long k_open(const char *path, int flags);
extern long k_close(int fd);
extern long k_stat(const char *path, stat_t *buf);
extern long k_fstat(int fd, stat_t *buf);
extern long k_poll(struct pollfd_t *fds, unsigned nfds, int timeout);
extern long k_lseek(int fd, int64_t offset, int whence);
extern long k_rt_sigaction(int sig, const struct kernel_sigaction *act,
                           struct kernel_sigaction *oldact, size_t sigsetsize);
extern long k_access(const char *path, int mode);
extern long k_pipe(int pipefd[2]);
extern long k_dup2(int oldfd, int newfd);
extern long k_alarm(unsigned seconds);
extern long k_socket(int domain, int type, int protocol);
extern long k_connect(int fd, const sockaddr_in_t *addr, socklen_t len);
extern long k_accept(int fd, sockaddr_in_t *addr, socklen_t *len);
extern long k_sendto(int fd, const void *buf, size_t n, int flags,
                     const sockaddr_in_t *dest, socklen_t len);
extern long k_recvfrom(int fd, void *buf, size_t n, int flags,
                       sockaddr_in_t *src, socklen_t *len);
extern long k_shutdown(int fd, int how);
extern long k_bind(int fd, const sockaddr_in_t *addr, socklen_t len);
extern long k_listen(int fd, int backlog);
extern long k_getpeername(int fd, sockaddr_in_t *addr, socklen_t *len);
extern long k_setsockopt(int fd, int level, int optname, const void *optval,
                         socklen_t optlen);
extern long k_fork(void);
extern long k_execve(const char *path, char **argv, char **envp);
extern long k_exit(int status);
extern long k_wait4(int pid, int *wstatus, int options, void *rusage);
extern long k_getcwd(char *buf, size_t size);
extern long k_chdir(const char *path);
extern long k_gettimeofday(struct timeval_t *tv, void *tz);
extern long k_setuid(unsigned uid);
extern long k_setgid(unsigned gid);
extern long k_setsid(void);
extern long k_setgroups(size_t size, const unsigned *list);
extern long k_getrandom(void *buf, size_t n, unsigned flags);

static char **environ;
static int kerrno;

/* strerror for the errnos that can escape the die paths */
static const char *errno_str(int e) {
  switch (e) {
  case 1:
    return "Operation not permitted";
  case 2:
    return "No such file or directory";
  case 13:
    return "Permission denied";
  case 20:
    return "Not a directory";
  case 98:
    return "Address already in use";
  case 99:
    return "Cannot assign requested address";
  default:
    return "error";
  }
}

static void log_msg(const char *fmt, ...);
static void die(const char *msg) NORETURN;
static void exit_now(int status) NORETURN;
static void log_and_exit(void) NORETURN;
static void send_headers(unsigned responseNum);
static void send_headers_and_exit(unsigned responseNum) NORETURN;
static void send_file_and_exit(const char *url, int what) NORETURN;
static void send_cgi_and_exit(const char *url, const char *orig_uri,
                              const char *request, int post_len) NORETURN;
static void cgi_io_loop_and_exit(int fromCgi_rd, int toCgi_wr,
                                 int post_len) NORETURN;
static void handle_incoming_and_exit(const sockaddr_in_t *from) NORETURN;
static int parse_conf(const char *path, int flag);

/* normalize a raw syscall return, -1 plus kerrno on error */
static long sysret(long r) {
  if (r >= -4095 && r < 0) {
    kerrno = (int)-r;
    return -1;
  }
  kerrno = 0;
  return r;
}

/* memory and string
 */

void *memset(void *dst, int c, size_t n) {
  unsigned char *d = dst;
  while (n--)
    *d++ = (unsigned char)c;
  return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  while (n--)
    *d++ = *s++;
  return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  if (d < s) {
    while (n--)
      *d++ = *s++;
  } else {
    d += n;
    s += n;
    while (n--)
      *--d = *--s;
  }
  return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *x = a, *y = b;
  while (n--) {
    if (*x != *y)
      return *x - *y;
    x++;
    y++;
  }
  return 0;
}

size_t strlen(const char *s) {
  const char *p = s;
  while (*p)
    ++p;
  return (size_t)(p - s);
}

int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
  while (n && *a && *a == *b) {
    a++;
    b++;
    n--;
  }
  if (n == 0)
    return 0;
  return (unsigned char)*a - (unsigned char)*b;
}

int strcasecmp(const char *a, const char *b) {
  unsigned char ca, cb;
  while ((ca = (unsigned char)*a) && (cb = (unsigned char)*b)) {
    if (ca >= 'A' && ca <= 'Z')
      ca += 32;
    if (cb >= 'A' && cb <= 'Z')
      cb += 32;
    if (ca != cb)
      return ca - cb;
    a++;
    b++;
  }
  ca = (unsigned char)*a;
  cb = (unsigned char)*b;
  return ca - cb;
}

int strncasecmp(const char *a, const char *b, size_t n) {
  unsigned char ca, cb;
  while (n--) {
    ca = (unsigned char)*a++;
    cb = (unsigned char)*b++;
    if (ca >= 'A' && ca <= 'Z')
      ca += 32;
    if (cb >= 'A' && cb <= 'Z')
      cb += 32;
    if (ca != cb)
      return ca - cb;
    if (ca == 0)
      return 0;
  }
  return 0;
}

char *strcpy(char *dst, const char *src) {
  char *d = dst;
  while ((*d++ = *src++))
    ;
  return dst;
}

char *strchr(const char *s, int c) {
  char ch = (char)c;
  while (*s) {
    if (*s == ch)
      return (char *)s;
    s++;
  }
  return ch == 0 ? (char *)s : 0;
}

char *strrchr(const char *s, int c) {
  const char *last = 0;
  char ch = (char)c;
  while (*s) {
    if (*s == ch)
      last = s;
    s++;
  }
  if (ch == 0)
    return (char *)s;
  return (char *)last;
}

char *strstr(const char *hay, const char *needle) {
  size_t nl = strlen(needle);
  if (nl == 0)
    return (char *)hay;
  for (; *hay; hay++) {
    if (*hay == *needle && strncmp(hay, needle, nl) == 0)
      return (char *)hay;
  }
  return 0;
}

static int is_digit(int c) { return c >= '0' && c <= '9'; }

static int is_alnum(int c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z');
}

static char *skip_ws(const char *s) {
  while (*s == ' ' || *s == '\t')
    s++;
  return (char *)s;
}

static char *basename_of(const char *path) {
  const char *p = strrchr(path, '/');
  return (char *)(p ? p + 1 : path);
}

/* decimal parse with endptr, no sign, no leading ws, -1 on junk */
static long long parse_ll(const char *s, const char **end) {
  unsigned long long v = 0;
  const char *p = s;
  if (!is_digit((unsigned char)*p)) {
    if (end)
      *end = p;
    return -1;
  }
  while (is_digit((unsigned char)*p)) {
    if (v > 0x7fffffffffffffffULL / 10) {
      if (end)
        *end = p;
      return -1;
    }
    v = v * 10 + (unsigned)(*p - '0');
    p++;
  }
  if (end)
    *end = p;
  return (long long)v;
}

/* printf subset: %s %c %d %i %u %x with .N and .* precision for %s and */
/* l, ll and z length modifiers, always NUL terminates, returns the */
/* number of chars actually written into the buffer */
static size_t kvformat(char *dst, size_t cap, const char *fmt, va_list ap) {
  size_t pos = 0;
  while (*fmt) {
    char c = *fmt++;
    if (c != '%') {
      if (pos + 1 < cap)
        dst[pos] = c;
      pos++;
      continue;
    }
    int prec = -1;
    c = *fmt;
    if (c == '.') {
      fmt++;
      c = *fmt;
      if (c == '*') {
        prec = va_arg(ap, int);
        fmt++;
        c = *fmt;
      } else {
        long long v = 0;
        while (is_digit((unsigned char)c)) {
          v = v * 10 + (c - '0');
          fmt++;
          c = *fmt;
        }
        prec = (int)v;
      }
    }
    int longs = 0;
    if (c == 'z') {
      longs = 1;
      fmt++;
      c = *fmt;
    }
    while (c == 'l') {
      longs++;
      fmt++;
      c = *fmt;
    }
    if (c == '%') {
      if (pos + 1 < cap)
        dst[pos] = '%';
      pos++;
      fmt++;
      continue;
    }
    if (c == 's') {
      const char *s = va_arg(ap, const char *);
      if (!s)
        s = "";
      size_t max = prec >= 0 ? (size_t)prec : (size_t)-1;
      while (*s && max--) {
        if (pos + 1 < cap)
          dst[pos] = *s;
        pos++;
        s++;
      }
      fmt++;
    } else if (c == 'c') {
      int ch = va_arg(ap, int);
      if (pos + 1 < cap)
        dst[pos] = (char)ch;
      pos++;
      fmt++;
    } else if (c == 'd' || c == 'i' || c == 'u' || c == 'x') {
      unsigned long long v;
      int neg = 0;
      if (longs >= 2)
        v = va_arg(ap, unsigned long long);
      else if (longs == 1)
        v = va_arg(ap, unsigned long);
      else
        v = va_arg(ap, unsigned int);
      if ((c == 'd' || c == 'i') &&
          (longs >= 2   ? (long long)v < 0
           : longs == 1 ? (long)v < 0
                        : (int)v < 0)) {
        neg = 1;
        v = longs >= 2   ? (unsigned long long)(-(long long)v)
            : longs == 1 ? (unsigned long)(-(long)v)
                         : (unsigned)(-(int)v);
      }
      if (neg) {
        if (pos + 1 < cap)
          dst[pos] = '-';
        pos++;
      }
      char tmp[24];
      int n = 0;
      int base = c == 'x' ? 16 : 10;
      if (v == 0)
        tmp[n++] = '0';
      while (v) {
        unsigned d = (unsigned)(v % (unsigned long long)base);
        int ch = (d < 10) ? ('0' + (int)d) : ('a' + (int)d - 10);
        tmp[n++] = (char)ch;
        v /= (unsigned long long)base;
      }
      while (n--) {
        if (pos + 1 < cap)
          dst[pos] = tmp[n];
        pos++;
      }
      fmt++;
    } else {
      /* unknown spec, emit verbatim to make bugs visible */
      if (pos + 1 < cap)
        dst[pos] = '%';
      pos++;
      if (c) {
        if (pos + 1 < cap)
          dst[pos] = c;
        pos++;
        fmt++;
      }
    }
  }
  if (cap > 0)
    dst[pos < cap ? pos : cap - 1] = '\0';
  return pos < cap ? pos : cap - 1;
}

static size_t ksnprintf(char *dst, size_t cap, const char *fmt, ...) {
  va_list ap;
  size_t r;
  va_start(ap, fmt);
  r = kvformat(dst, cap, fmt, ap);
  va_end(ap);
  return r;
}

/* io helpers
 */

/* read with EINTR retry, returns bytes or -1 */
static ssize_t safe_read(int fd, void *buf, size_t n) {
  ssize_t r;
  do {
    r = (ssize_t)sysret(k_read(fd, buf, n));
  } while (r < 0 && kerrno == EINTR);
  return r;
}

/* single write with EINTR retry, returns bytes or -1 */
static ssize_t safe_write(int fd, const void *buf, size_t n) {
  ssize_t r;
  do {
    r = (ssize_t)sysret(k_write(fd, buf, n));
  } while (r < 0 && kerrno == EINTR);
  return r;
}

/* write all of buf, EINTR retry, returns n or -1 */
static ssize_t full_write(int fd, const void *buf, size_t n) {
  const char *p = buf;
  size_t left = n;
  while (left) {
    ssize_t r = (ssize_t)sysret(k_write(fd, p, left));
    if (r < 0) {
      if (kerrno == EINTR)
        continue;
      return -1;
    }
    if (r == 0)
      break;
    p += r;
    left -= (size_t)r;
  }
  return (ssize_t)(n - left);
}

/* signals
 */

extern void k_sigreturn(void);

static void set_signal(int sig, sighandler_t handler) {
  struct kernel_sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.handler = handler;
  sa.flags = SA_RESTORER;
  sa.restorer = k_sigreturn;
  sa.mask = 0;
  sysret(k_rt_sigaction(sig, &sa, 0, 8));
}

/* error reporting
 */

static int verbose;
static char rmt_ip[64]; /* "a.b.c.d:port" for logging and cgi env */
static int rmt_ip_set;

/* log to stderr like bb_error_msg, prefix is the peer ip when known */
static void log_msg(const char *fmt, ...) {
  char msg[512];
  char line[600];
  va_list ap;
  va_start(ap, fmt);
  kvformat(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  const char *prefix = (verbose && rmt_ip_set) ? rmt_ip : "httpd";
  ksnprintf(line, sizeof(line), "%s: %s\n", prefix, msg);
  full_write(2, line, strlen(line));
}

static void die(const char *msg) {
  char line[600];
  ksnprintf(line, sizeof(line), "httpd: %s\n", msg);
  full_write(2, line, strlen(line));
  exit_now(1);
}

/* die with the last errno appended, like bb_simple_perror_msg_and_die */
static void die_errno(const char *msg) {
  char line[600];
  ksnprintf(line, sizeof(line), "httpd: %s: %s\n", msg, errno_str(kerrno));
  full_write(2, line, strlen(line));
  exit_now(1);
}

static void exit_now(int status) {
  sysret(k_exit(status));
  for (;;)
    ;
}

/* time and dates
 */

static long now_sec(void) {
  struct timeval_t tv;
  sysret(k_gettimeofday(&tv, 0));
  return tv.tv_sec;
}

/* days since epoch to y/m/d, howard hinnant's civil_from_days */
static void civil_from_days(long z, int *yp, int *mp, int *dp) {
  z += 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned long doe = (unsigned long)(z - era * 146097);
  unsigned long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long y = (long)yoe + era * 400;
  unsigned long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned long mp_ = (5 * doy + 2) / 153;
  unsigned long d = doy - (153 * mp_ + 2) / 5 + 1;
  unsigned long m = mp_ < 10 ? mp_ + 3 : mp_ - 9;
  if (m <= 2)
    y--;
  *yp = (int)y;
  *mp = (int)m;
  *dp = (int)d;
}

/* rfc1123 date, 29 chars like "Sun, 06 Nov 1994 08:49:37 GMT" */
static void fmt_rfc1123(char *out, long t) {
  static const char wday[7][4] = {"Sun", "Mon", "Tue", "Wed",
                                  "Thu", "Fri", "Sat"};
  static const char month[12][4] = {"Jan", "Feb", "Mar", "Apr",
                                    "May", "Jun", "Jul", "Aug",
                                    "Sep", "Oct", "Nov", "Dec"};
  long days = t / 86400;
  long secs = t % 86400;
  int wd = (int)((days + 4) % 7);
  int y, m, d;
  civil_from_days(days, &y, &m, &d);
  int hh = (int)(secs / 3600);
  int mm = (int)((secs / 60) % 60);
  int ss = (int)(secs % 60);
  ksnprintf(out, 40, "%s, %d%d %s %d %d%d:%d%d:%d%d GMT", wday[wd], d / 10,
            d % 10, month[m - 1], y, hh / 10, hh % 10, mm / 10, mm % 10,
            ss / 10, ss % 10);
}

/* random
 */

/* n random bytes, falls back to the clock if getrandom is unavailable */
static void fill_random(void *buf, size_t n) {
  unsigned char *p = buf;
  if (sysret(k_getrandom(buf, n, 0)) == (long)n)
    return;
  unsigned char x = (unsigned char)now_sec();
  while (n--)
    *p++ = x++;
}

/* encoding helpers
 */

static unsigned hex_to_bin(unsigned char c) {
  unsigned v = c - '0';
  if (v <= 9)
    return v;
  v = (unsigned)(c | 0x20) - 'a';
  if (v <= 5)
    return v + 10;
  return ~0U;
}

/* decode %XX in place, semantics match busybox percent_decode_in_place */
/* strict: '+' stays, bad hex returns NULL, decoded '/' or NUL returns str+1 */
/* lax (the -d mode): '+' becomes space, bad hex stays literal */
static char *percent_decode_in_place(char *str, int strict) {
  char *src = str;
  char *dst = str;
  char c;
  while ((c = *src++) != '\0') {
    unsigned v;
    if (!strict && c == '+') {
      *dst++ = ' ';
      continue;
    }
    if (c != '%') {
      *dst++ = c;
      continue;
    }
    v = hex_to_bin((unsigned char)src[0]);
    if (v > 15)
      goto bad_hex;
    v = (v * 16) | hex_to_bin((unsigned char)src[1]);
    if (v > 255)
      goto bad_hex;
    if (strict && (v == '/' || v == '\0'))
      return str + 1;
    *dst++ = (char)v;
    src += 2;
    continue;
  bad_hex:
    if (strict)
      return NULL;
    *dst++ = '%';
  }
  *dst = '\0';
  return str;
}

/* html encode for the -e mode, alnum passes, everything else becomes &#NN; */
static void html_encode_write(const char *s) {
  char out[8];
  while (*s) {
    if (is_alnum((unsigned char)*s)) {
      full_write(1, s, 1);
    } else {
      ksnprintf(out, sizeof(out), "&#%u;", (unsigned char)*s);
      full_write(1, out, strlen(out));
    }
    s++;
  }
}

static const char b64t[] =
    "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

/* decode base64 in place, returns pointer to the terminating NUL */
static char *decode_base64(char *data) {
  static const signed char tab[256] = {
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, 52, 53, 54, 55, 56, 57,
      58, 59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,
      7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
      25, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
      37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1,
      -1, -1};
  char *src = data;
  char *dst = data;
  unsigned acc = 0;
  int bits = 0;
  while (*src) {
    if (*src == '=') {
      src++;
      while (*src == ' ' || *src == '\t')
        src++;
      continue;
    }
    int v = tab[(unsigned char)*src++];
    if (v < 0) {
      /* non base64 chars are to be ignored */
      continue;
    }
    acc = (acc << 6) | (unsigned)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      *dst++ = (char)((acc >> bits) & 0xff);
    }
  }
  *dst = '\0';
  return dst;
}

/* md5, public domain implementation based on rfc1321 and libtomcrypt
 */

struct md5_ctx {
  uint64_t len;
  uint32_t h[4];
  uint8_t buf[64];
};

static uint32_t rol32(uint32_t n, int k) { return (n << k) | (n >> (32 - k)); }
#define MD5_F(x, y, z) (z ^ (x & (y ^ z)))
#define MD5_G(x, y, z) (y ^ (z & (y ^ x)))
#define MD5_H(x, y, z) (x ^ y ^ z)
#define MD5_I(x, y, z) (y ^ (x | ~z))
#define MD5_FF(a, b, c, d, w, s, t) \
  a += MD5_F(b, c, d) + w + t;      \
  a = rol32(a, s) + b
#define MD5_GG(a, b, c, d, w, s, t) \
  a += MD5_G(b, c, d) + w + t;      \
  a = rol32(a, s) + b
#define MD5_HH(a, b, c, d, w, s, t) \
  a += MD5_H(b, c, d) + w + t;      \
  a = rol32(a, s) + b
#define MD5_II(a, b, c, d, w, s, t) \
  a += MD5_I(b, c, d) + w + t;      \
  a = rol32(a, s) + b

static const uint32_t md5_tab[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

static void md5_processblock(struct md5_ctx *s, const uint8_t *buf) {
  uint32_t i, W[16], a, b, c, d;
  for (i = 0; i < 16; i++) {
    W[i] = buf[4 * i];
    W[i] |= (uint32_t)buf[4 * i + 1] << 8;
    W[i] |= (uint32_t)buf[4 * i + 2] << 16;
    W[i] |= (uint32_t)buf[4 * i + 3] << 24;
  }
  a = s->h[0];
  b = s->h[1];
  c = s->h[2];
  d = s->h[3];
  i = 0;
  while (i < 16) {
    MD5_FF(a, b, c, d, W[i], 7, md5_tab[i]);
    i++;
    MD5_FF(d, a, b, c, W[i], 12, md5_tab[i]);
    i++;
    MD5_FF(c, d, a, b, W[i], 17, md5_tab[i]);
    i++;
    MD5_FF(b, c, d, a, W[i], 22, md5_tab[i]);
    i++;
  }
  while (i < 32) {
    MD5_GG(a, b, c, d, W[(5 * i + 1) % 16], 5, md5_tab[i]);
    i++;
    MD5_GG(d, a, b, c, W[(5 * i + 1) % 16], 9, md5_tab[i]);
    i++;
    MD5_GG(c, d, a, b, W[(5 * i + 1) % 16], 14, md5_tab[i]);
    i++;
    MD5_GG(b, c, d, a, W[(5 * i + 1) % 16], 20, md5_tab[i]);
    i++;
  }
  while (i < 48) {
    MD5_HH(a, b, c, d, W[(3 * i + 5) % 16], 4, md5_tab[i]);
    i++;
    MD5_HH(d, a, b, c, W[(3 * i + 5) % 16], 11, md5_tab[i]);
    i++;
    MD5_HH(c, d, a, b, W[(3 * i + 5) % 16], 16, md5_tab[i]);
    i++;
    MD5_HH(b, c, d, a, W[(3 * i + 5) % 16], 23, md5_tab[i]);
    i++;
  }
  while (i < 64) {
    MD5_II(a, b, c, d, W[7 * i % 16], 6, md5_tab[i]);
    i++;
    MD5_II(d, a, b, c, W[7 * i % 16], 10, md5_tab[i]);
    i++;
    MD5_II(c, d, a, b, W[7 * i % 16], 15, md5_tab[i]);
    i++;
    MD5_II(b, c, d, a, W[7 * i % 16], 21, md5_tab[i]);
    i++;
  }
  s->h[0] += a;
  s->h[1] += b;
  s->h[2] += c;
  s->h[3] += d;
}

static void md5_pad(struct md5_ctx *s) {
  unsigned r = (unsigned)(s->len % 64);
  s->buf[r++] = 0x80;
  if (r > 56) {
    memset(s->buf + r, 0, 64 - r);
    r = 0;
    md5_processblock(s, s->buf);
  }
  memset(s->buf + r, 0, 56 - r);
  s->len *= 8;
  s->buf[56] = (uint8_t)s->len;
  s->buf[57] = (uint8_t)(s->len >> 8);
  s->buf[58] = (uint8_t)(s->len >> 16);
  s->buf[59] = (uint8_t)(s->len >> 24);
  s->buf[60] = (uint8_t)(s->len >> 32);
  s->buf[61] = (uint8_t)(s->len >> 40);
  s->buf[62] = (uint8_t)(s->len >> 48);
  s->buf[63] = (uint8_t)(s->len >> 56);
  md5_processblock(s, s->buf);
}

static void md5_init(struct md5_ctx *s) {
  s->len = 0;
  s->h[0] = 0x67452301;
  s->h[1] = 0xefcdab89;
  s->h[2] = 0x98badcfe;
  s->h[3] = 0x10325476;
}

static void md5_sum(struct md5_ctx *s, uint8_t *md) {
  md5_pad(s);
  for (int i = 0; i < 4; i++) {
    md[4 * i] = (uint8_t)s->h[i];
    md[4 * i + 1] = (uint8_t)(s->h[i] >> 8);
    md[4 * i + 2] = (uint8_t)(s->h[i] >> 16);
    md[4 * i + 3] = (uint8_t)(s->h[i] >> 24);
  }
}

static void md5_update(struct md5_ctx *s, const void *m, size_t len) {
  const uint8_t *p = m;
  unsigned r = (unsigned)(s->len % 64);
  s->len += len;
  if (r) {
    if (len < 64 - r) {
      memcpy(s->buf + r, p, len);
      return;
    }
    memcpy(s->buf + r, p, 64 - r);
    len -= 64 - r;
    p += 64 - r;
    md5_processblock(s, s->buf);
  }
  for (; len >= 64; len -= 64, p += 64)
    md5_processblock(s, p);
  memcpy(s->buf, p, len);
}

/* sha256, public domain implementation based on fips180-3
 */

struct sha256_ctx {
  uint64_t len;
  uint32_t h[8];
  uint8_t buf[64];
};

static uint32_t ror32(uint32_t n, int k) { return (n >> k) | (n << (32 - k)); }
#define SHA_Ch(x, y, z) (z ^ (x & (y ^ z)))
#define SHA_Maj(x, y, z) ((x & y) | (z & (x | y)))
#define SHA256_S0(x) (ror32(x, 2) ^ ror32(x, 13) ^ ror32(x, 22))
#define SHA256_S1(x) (ror32(x, 6) ^ ror32(x, 11) ^ ror32(x, 25))
#define SHA256_R0(x) (ror32(x, 7) ^ ror32(x, 18) ^ (x >> 3))
#define SHA256_R1(x) (ror32(x, 17) ^ ror32(x, 19) ^ (x >> 10))

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static void sha256_processblock(struct sha256_ctx *s, const uint8_t *buf) {
  uint32_t W[64], t1, t2, a, b, c, d, e, f, g, h;
  int i;
  for (i = 0; i < 16; i++) {
    W[i] = (uint32_t)buf[4 * i] << 24;
    W[i] |= (uint32_t)buf[4 * i + 1] << 16;
    W[i] |= (uint32_t)buf[4 * i + 2] << 8;
    W[i] |= buf[4 * i + 3];
  }
  for (; i < 64; i++)
    W[i] = SHA256_R1(W[i - 2]) + W[i - 7] + SHA256_R0(W[i - 15]) + W[i - 16];
  a = s->h[0];
  b = s->h[1];
  c = s->h[2];
  d = s->h[3];
  e = s->h[4];
  f = s->h[5];
  g = s->h[6];
  h = s->h[7];
  for (i = 0; i < 64; i++) {
    t1 = h + SHA256_S1(e) + SHA_Ch(e, f, g) + sha256_k[i] + W[i];
    t2 = SHA256_S0(a) + SHA_Maj(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  s->h[0] += a;
  s->h[1] += b;
  s->h[2] += c;
  s->h[3] += d;
  s->h[4] += e;
  s->h[5] += f;
  s->h[6] += g;
  s->h[7] += h;
}

static void sha256_pad(struct sha256_ctx *s) {
  unsigned r = (unsigned)(s->len % 64);
  s->buf[r++] = 0x80;
  if (r > 56) {
    memset(s->buf + r, 0, 64 - r);
    r = 0;
    sha256_processblock(s, s->buf);
  }
  memset(s->buf + r, 0, 56 - r);
  s->len *= 8;
  s->buf[56] = (uint8_t)(s->len >> 56);
  s->buf[57] = (uint8_t)(s->len >> 48);
  s->buf[58] = (uint8_t)(s->len >> 40);
  s->buf[59] = (uint8_t)(s->len >> 32);
  s->buf[60] = (uint8_t)(s->len >> 24);
  s->buf[61] = (uint8_t)(s->len >> 16);
  s->buf[62] = (uint8_t)(s->len >> 8);
  s->buf[63] = (uint8_t)s->len;
  sha256_processblock(s, s->buf);
}

static void sha256_init(struct sha256_ctx *s) {
  s->len = 0;
  s->h[0] = 0x6a09e667;
  s->h[1] = 0xbb67ae85;
  s->h[2] = 0x3c6ef372;
  s->h[3] = 0xa54ff53a;
  s->h[4] = 0x510e527f;
  s->h[5] = 0x9b05688c;
  s->h[6] = 0x1f83d9ab;
  s->h[7] = 0x5be0cd19;
}

static void sha256_sum(struct sha256_ctx *s, uint8_t *md) {
  sha256_pad(s);
  for (int i = 0; i < 8; i++) {
    md[4 * i] = (uint8_t)(s->h[i] >> 24);
    md[4 * i + 1] = (uint8_t)(s->h[i] >> 16);
    md[4 * i + 2] = (uint8_t)(s->h[i] >> 8);
    md[4 * i + 3] = (uint8_t)s->h[i];
  }
}

static void sha256_update(struct sha256_ctx *s, const void *m, size_t len) {
  const uint8_t *p = m;
  unsigned r = (unsigned)(s->len % 64);
  s->len += len;
  if (r) {
    if (len < 64 - r) {
      memcpy(s->buf + r, p, len);
      return;
    }
    memcpy(s->buf + r, p, 64 - r);
    len -= 64 - r;
    p += 64 - r;
    sha256_processblock(s, s->buf);
  }
  for (; len >= 64; len -= 64, p += 64)
    sha256_processblock(s, p);
  memcpy(s->buf, p, len);
}

/* sha512, public domain implementation based on fips180-3
 */

struct sha512_ctx {
  uint64_t len;
  uint64_t h[8];
  uint8_t buf[128];
};

static uint64_t ror64(uint64_t n, int k) { return (n >> k) | (n << (64 - k)); }
#define SHA512_S0(x) (ror64(x, 28) ^ ror64(x, 34) ^ ror64(x, 39))
#define SHA512_S1(x) (ror64(x, 14) ^ ror64(x, 18) ^ ror64(x, 41))
#define SHA512_R0(x) (ror64(x, 1) ^ ror64(x, 8) ^ (x >> 7))
#define SHA512_R1(x) (ror64(x, 19) ^ ror64(x, 61) ^ (x >> 6))

static const uint64_t sha512_k[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL,
    0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL,
    0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL,
    0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL,
    0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL,
    0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL,
    0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL,
    0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL,
    0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL,
    0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL,
    0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
    0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};

static void sha512_processblock(struct sha512_ctx *s, const uint8_t *buf) {
  uint64_t W[80], t1, t2, a, b, c, d, e, f, g, h;
  int i;
  for (i = 0; i < 16; i++) {
    W[i] = (uint64_t)buf[8 * i] << 56;
    W[i] |= (uint64_t)buf[8 * i + 1] << 48;
    W[i] |= (uint64_t)buf[8 * i + 2] << 40;
    W[i] |= (uint64_t)buf[8 * i + 3] << 32;
    W[i] |= (uint64_t)buf[8 * i + 4] << 24;
    W[i] |= (uint64_t)buf[8 * i + 5] << 16;
    W[i] |= (uint64_t)buf[8 * i + 6] << 8;
    W[i] |= buf[8 * i + 7];
  }
  for (; i < 80; i++)
    W[i] = SHA512_R1(W[i - 2]) + W[i - 7] + SHA512_R0(W[i - 15]) + W[i - 16];
  a = s->h[0];
  b = s->h[1];
  c = s->h[2];
  d = s->h[3];
  e = s->h[4];
  f = s->h[5];
  g = s->h[6];
  h = s->h[7];
  for (i = 0; i < 80; i++) {
    t1 = h + SHA512_S1(e) + SHA_Ch(e, f, g) + sha512_k[i] + W[i];
    t2 = SHA512_S0(a) + SHA_Maj(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  s->h[0] += a;
  s->h[1] += b;
  s->h[2] += c;
  s->h[3] += d;
  s->h[4] += e;
  s->h[5] += f;
  s->h[6] += g;
  s->h[7] += h;
}

static void sha512_pad(struct sha512_ctx *s) {
  unsigned r = (unsigned)(s->len % 128);
  s->buf[r++] = 0x80;
  if (r > 112) {
    memset(s->buf + r, 0, 128 - r);
    r = 0;
    sha512_processblock(s, s->buf);
  }
  memset(s->buf + r, 0, 120 - r);
  s->len *= 8;
  s->buf[120] = (uint8_t)(s->len >> 56);
  s->buf[121] = (uint8_t)(s->len >> 48);
  s->buf[122] = (uint8_t)(s->len >> 40);
  s->buf[123] = (uint8_t)(s->len >> 32);
  s->buf[124] = (uint8_t)(s->len >> 24);
  s->buf[125] = (uint8_t)(s->len >> 16);
  s->buf[126] = (uint8_t)(s->len >> 8);
  s->buf[127] = (uint8_t)s->len;
  sha512_processblock(s, s->buf);
}

static void sha512_init(struct sha512_ctx *s) {
  s->len = 0;
  s->h[0] = 0x6a09e667f3bcc908ULL;
  s->h[1] = 0xbb67ae8584caa73bULL;
  s->h[2] = 0x3c6ef372fe94f82bULL;
  s->h[3] = 0xa54ff53a5f1d36f1ULL;
  s->h[4] = 0x510e527fade682d1ULL;
  s->h[5] = 0x9b05688c2b3e6c1fULL;
  s->h[6] = 0x1f83d9abfb41bd6bULL;
  s->h[7] = 0x5be0cd19137e2179ULL;
}

static void sha512_sum(struct sha512_ctx *s, uint8_t *md) {
  sha512_pad(s);
  for (int i = 0; i < 8; i++) {
    md[8 * i] = (uint8_t)(s->h[i] >> 56);
    md[8 * i + 1] = (uint8_t)(s->h[i] >> 48);
    md[8 * i + 2] = (uint8_t)(s->h[i] >> 40);
    md[8 * i + 3] = (uint8_t)(s->h[i] >> 32);
    md[8 * i + 4] = (uint8_t)(s->h[i] >> 24);
    md[8 * i + 5] = (uint8_t)(s->h[i] >> 16);
    md[8 * i + 6] = (uint8_t)(s->h[i] >> 8);
    md[8 * i + 7] = (uint8_t)s->h[i];
  }
}

static void sha512_update(struct sha512_ctx *s, const void *m, size_t len) {
  const uint8_t *p = m;
  unsigned r = (unsigned)(s->len % 128);
  s->len += len;
  if (r) {
    if (len < 128 - r) {
      memcpy(s->buf + r, p, len);
      return;
    }
    memcpy(s->buf + r, p, 128 - r);
    len -= 128 - r;
    p += 128 - r;
    sha512_processblock(s, s->buf);
  }
  for (; len >= 128; len -= 128, p += 128)
    sha512_processblock(s, p);
  memcpy(s->buf, p, len);
}

/* crypt, the md5 crypt design is from poul-henning kamp
 * implementations follow the public domain musl libc code and the
 * sha-crypt spec from http://people.redhat.com/drepper/SHA-crypt.txt
 */

static char *to64_crypt(char *s, unsigned u, int n) {
  while (--n >= 0) {
    *s++ = b64t[u % 64];
    u /= 64;
  }
  return s;
}

#define CRYPT_KEY_MAX 256
#define CRYPT_SALT_MAX_MD5 8
#define CRYPT_SALT_MAX_SHA 16
#define CRYPT_ROUNDS_DEFAULT 5000
#define CRYPT_ROUNDS_MIN 1000
#define CRYPT_ROUNDS_MAX 9999999

/* returns NULL on bad setting or oversized key, output into out */
static char *md5_crypt(const char *key, const char *setting, char *out) {
  struct md5_ctx ctx;
  uint8_t md[16];
  unsigned i, klen, slen;
  const char *salt;

  for (i = 0; i <= CRYPT_KEY_MAX && key[i]; i++)
    ;
  if (i > CRYPT_KEY_MAX)
    return NULL;
  klen = i;

  if (strncmp(setting, "$1$", 3) != 0)
    return NULL;
  salt = setting + 3;
  for (i = 0; i < CRYPT_SALT_MAX_MD5 && salt[i] && salt[i] != '$'; i++)
    ;
  slen = i;

  md5_init(&ctx);
  md5_update(&ctx, key, klen);
  md5_update(&ctx, salt, slen);
  md5_update(&ctx, key, klen);
  md5_sum(&ctx, md);

  md5_init(&ctx);
  md5_update(&ctx, key, klen);
  md5_update(&ctx, setting, 3 + slen);
  for (i = klen; i > sizeof(md); i -= sizeof(md))
    md5_update(&ctx, md, sizeof(md));
  md5_update(&ctx, md, i);
  md[0] = 0;
  for (i = klen; i; i >>= 1)
    if (i & 1)
      md5_update(&ctx, md, 1);
    else
      md5_update(&ctx, key, 1);
  md5_sum(&ctx, md);

  for (i = 0; i < 1000; i++) {
    md5_init(&ctx);
    if (i % 2)
      md5_update(&ctx, key, klen);
    else
      md5_update(&ctx, md, sizeof(md));
    if (i % 3)
      md5_update(&ctx, salt, slen);
    if (i % 7)
      md5_update(&ctx, key, klen);
    if (i % 2)
      md5_update(&ctx, md, sizeof(md));
    else
      md5_update(&ctx, key, klen);
    md5_sum(&ctx, md);
  }

  memcpy(out, setting, 3 + slen);
  char *p = out + 3 + slen;
  *p++ = '$';
  static const unsigned char perm[5][3] = {{0, 6, 12},  {1, 7, 13},
                                           {2, 8, 14},  {3, 9, 15},
                                           {4, 10, 5}};
  for (i = 0; i < 5; i++)
    p = to64_crypt(p, (md[perm[i][0]] << 16) | (md[perm[i][1]] << 8) |
                          md[perm[i][2]], 4);
  p = to64_crypt(p, md[11], 2);
  *p = 0;
  return out;
}

static unsigned crypt_sha_rounds(const char **saltp) {
  const char *salt = *saltp;
  unsigned r = CRYPT_ROUNDS_DEFAULT;
  if (strncmp(salt, "rounds=", 7) == 0) {
    const char *end;
    long long u;
    salt += 7;
    if (!is_digit((unsigned char)*salt))
      return 0;
    u = parse_ll(salt, &end);
    if (*end != '$' || u < 0)
      return 0;
    *saltp = end + 1;
    if (u < CRYPT_ROUNDS_MIN)
      r = CRYPT_ROUNDS_MIN;
    else if (u > CRYPT_ROUNDS_MAX)
      return 0;
    else
      r = (unsigned)u;
  }
  return r;
}

/* hash n bytes worth of the repeated md digest */
static void sha256_hashmd(struct sha256_ctx *s, unsigned n, const void *md) {
  unsigned i;
  for (i = n; i > 32; i -= 32)
    sha256_update(s, md, 32);
  sha256_update(s, md, i);
}

static char *sha256_crypt(const char *key, const char *setting, char *out) {
  struct sha256_ctx ctx;
  uint8_t md[32], kmd[32], smd[32];
  unsigned i, r, klen, slen;
  char rounds[20] = "";
  const char *salt;
  char *p;

  for (i = 0; i <= CRYPT_KEY_MAX && key[i]; i++)
    ;
  if (i > CRYPT_KEY_MAX)
    return NULL;
  klen = i;

  if (strncmp(setting, "$5$", 3) != 0)
    return NULL;
  salt = setting + 3;
  r = crypt_sha_rounds(&salt);
  if (r == 0)
    return NULL;
  if (r != CRYPT_ROUNDS_DEFAULT)
    ksnprintf(rounds, sizeof(rounds), "rounds=%u$", r);

  for (i = 0; i < CRYPT_SALT_MAX_SHA && salt[i] && salt[i] != '$'; i++)
    if (salt[i] == '\n' || salt[i] == ':')
      return NULL;
  slen = i;

  sha256_init(&ctx);
  sha256_update(&ctx, key, klen);
  sha256_update(&ctx, salt, slen);
  sha256_update(&ctx, key, klen);
  sha256_sum(&ctx, md);

  sha256_init(&ctx);
  sha256_update(&ctx, key, klen);
  sha256_update(&ctx, salt, slen);
  sha256_hashmd(&ctx, klen, md);
  for (i = klen; i > 0; i >>= 1)
    if (i & 1)
      sha256_update(&ctx, md, sizeof(md));
    else
      sha256_update(&ctx, key, klen);
  sha256_sum(&ctx, md);

  sha256_init(&ctx);
  for (i = 0; i < klen; i++)
    sha256_update(&ctx, key, klen);
  sha256_sum(&ctx, kmd);

  sha256_init(&ctx);
  for (i = 0; i < 16u + md[0]; i++)
    sha256_update(&ctx, salt, slen);
  sha256_sum(&ctx, smd);

  for (i = 0; i < r; i++) {
    sha256_init(&ctx);
    if (i % 2)
      sha256_hashmd(&ctx, klen, kmd);
    else
      sha256_update(&ctx, md, sizeof(md));
    if (i % 3)
      sha256_update(&ctx, smd, slen);
    if (i % 7)
      sha256_hashmd(&ctx, klen, kmd);
    if (i % 2)
      sha256_update(&ctx, md, sizeof(md));
    else
      sha256_hashmd(&ctx, klen, kmd);
    sha256_sum(&ctx, md);
  }

  p = out;
  p += ksnprintf(p, 64, "$5$%s%.*s$", rounds, (int)slen, salt);
  static const unsigned char perm[10][3] = {
      {0, 10, 20}, {21, 1, 11}, {12, 22, 2}, {3, 13, 23}, {24, 4, 14},
      {15, 25, 5}, {6, 16, 26}, {27, 7, 17}, {18, 28, 8}, {9, 19, 29}};
  for (i = 0; i < 10; i++)
    p = to64_crypt(p, (md[perm[i][0]] << 16) | (md[perm[i][1]] << 8) |
                          md[perm[i][2]], 4);
  p = to64_crypt(p, (md[31] << 8) | md[30], 3);
  *p = 0;
  return out;
}

/* hash n bytes worth of the repeated md digest */
static void sha512_hashmd(struct sha512_ctx *s, unsigned n, const void *md) {
  unsigned i;
  for (i = n; i > 64; i -= 64)
    sha512_update(s, md, 64);
  sha512_update(s, md, i);
}

static char *sha512_crypt(const char *key, const char *setting, char *out) {
  struct sha512_ctx ctx;
  uint8_t md[64], kmd[64], smd[64];
  unsigned i, r, klen, slen;
  char rounds[20] = "";
  const char *salt;
  char *p;

  for (i = 0; i <= CRYPT_KEY_MAX && key[i]; i++)
    ;
  if (i > CRYPT_KEY_MAX)
    return NULL;
  klen = i;

  if (strncmp(setting, "$6$", 3) != 0)
    return NULL;
  salt = setting + 3;
  r = crypt_sha_rounds(&salt);
  if (r == 0)
    return NULL;
  if (r != CRYPT_ROUNDS_DEFAULT)
    ksnprintf(rounds, sizeof(rounds), "rounds=%u$", r);

  for (i = 0; i < CRYPT_SALT_MAX_SHA && salt[i] && salt[i] != '$'; i++)
    if (salt[i] == '\n' || salt[i] == ':')
      return NULL;
  slen = i;

  sha512_init(&ctx);
  sha512_update(&ctx, key, klen);
  sha512_update(&ctx, salt, slen);
  sha512_update(&ctx, key, klen);
  sha512_sum(&ctx, md);

  sha512_init(&ctx);
  sha512_update(&ctx, key, klen);
  sha512_update(&ctx, salt, slen);
  sha512_hashmd(&ctx, klen, md);
  for (i = klen; i > 0; i >>= 1)
    if (i & 1)
      sha512_update(&ctx, md, sizeof(md));
    else
      sha512_update(&ctx, key, klen);
  sha512_sum(&ctx, md);

  sha512_init(&ctx);
  for (i = 0; i < klen; i++)
    sha512_update(&ctx, key, klen);
  sha512_sum(&ctx, kmd);

  sha512_init(&ctx);
  for (i = 0; i < 16u + md[0]; i++)
    sha512_update(&ctx, salt, slen);
  sha512_sum(&ctx, smd);

  for (i = 0; i < r; i++) {
    sha512_init(&ctx);
    if (i % 2)
      sha512_hashmd(&ctx, klen, kmd);
    else
      sha512_update(&ctx, md, sizeof(md));
    if (i % 3)
      sha512_update(&ctx, smd, slen);
    if (i % 7)
      sha512_hashmd(&ctx, klen, kmd);
    if (i % 2)
      sha512_update(&ctx, md, sizeof(md));
    else
      sha512_hashmd(&ctx, klen, kmd);
    sha512_sum(&ctx, md);
  }

  p = out;
  p += ksnprintf(p, 64, "$6$%s%.*s$", rounds, (int)slen, salt);
  static const unsigned char perm[21][3] = {
      {0, 21, 42},  {22, 43, 1},  {44, 2, 23},  {3, 24, 45},  {25, 46, 4},
      {47, 5, 26},  {6, 27, 48},  {28, 49, 7},  {50, 8, 29},  {9, 30, 51},
      {31, 52, 10}, {53, 11, 32}, {12, 33, 54}, {34, 55, 13}, {56, 14, 35},
      {15, 36, 57}, {37, 58, 16}, {59, 17, 38}, {18, 39, 60}, {40, 61, 19},
      {62, 20, 41}};
  for (i = 0; i < 21; i++)
    p = to64_crypt(p, (md[perm[i][0]] << 16) | (md[perm[i][1]] << 8) |
                          md[perm[i][2]], 4);
  p = to64_crypt(p, md[63], 2);
  *p = 0;
  return out;
}

/* crypt entry point, returns "*" on unsupported hash */
static const char *pw_encrypt(const char *key, const char *setting) {
  static char out[192];
  char *r = NULL;
  if (strncmp(setting, "$1$", 3) == 0)
    r = md5_crypt(key, setting, out);
  else if (strncmp(setting, "$5$", 3) == 0)
    r = sha256_crypt(key, setting, out);
  else if (strncmp(setting, "$6$", 3) == 0)
    r = sha512_crypt(key, setting, out);
  if (r == NULL)
    return "*";
  return out;
}

/* account files
 */

struct pw_entry {
  char name[64];
  char passwd[192];
  unsigned uid;
  unsigned gid;
};

static char acct_buf[16384];

static int read_small_file(const char *path, char *buf, size_t cap) {
  int fd = (int)sysret(k_open(path, O_RDONLY));
  if (fd < 0)
    return -1;
  size_t total = 0;
  while (total + 1 < cap) {
    ssize_t n = safe_read(fd, buf + total, cap - 1 - total);
    if (n <= 0)
      break;
    total += (size_t)n;
  }
  sysret(k_close(fd));
  buf[total] = '\0';
  return (int)total;
}

/* find a line in an account style file, fields split by ':' */
/* name != NULL matches fields[0], name == NULL matches the uid in fields[2] */
static int lookup_account(const char *path, const char *name, unsigned uid,
                          int field, char *out, size_t outcap, unsigned *num1,
                          unsigned *num2) {
  int len = read_small_file(path, acct_buf, sizeof(acct_buf));
  if (len < 0)
    return -1;
  char *line = acct_buf;
  while (line && *line) {
    char *next = strchr(line, '\n');
    if (next)
      *next++ = '\0';
    char *fields[8];
    int nf = 0;
    char *p = line;
    fields[nf++] = p;
    while (*p && nf < 8) {
      if (*p == ':') {
        *p++ = '\0';
        fields[nf++] = p;
      } else {
        p++;
      }
    }
    int match = 0;
    if (name != NULL) {
      if (nf > field && strcmp(fields[0], name) == 0)
        match = 1;
    } else {
      if (nf > 3 && (unsigned)parse_ll(fields[2], NULL) == uid)
        match = 1;
    }
    if (match) {
      if (out) {
        size_t l = strlen(fields[field]);
        if (l >= outcap)
          l = outcap - 1;
        memcpy(out, fields[field], l);
        out[l] = '\0';
      }
      if (num1 && nf > 2)
        *num1 = (unsigned)parse_ll(fields[2], NULL);
      if (num2 && nf > 3)
        *num2 = (unsigned)parse_ll(fields[3], NULL);
      return 0;
    }
    line = next;
  }
  return -1;
}

static int getpwnam(const char *name, struct pw_entry *pw) {
  if (lookup_account("/etc/passwd", name, 0, 1, pw->passwd,
                     sizeof(pw->passwd), &pw->uid, &pw->gid) < 0)
    return -1;
  size_t l = strlen(name);
  if (l >= sizeof(pw->name))
    l = sizeof(pw->name) - 1;
  memcpy(pw->name, name, l);
  pw->name[l] = '\0';
  return 0;
}

static int getpwuid(unsigned uid, struct pw_entry *pw) {
  if (lookup_account("/etc/passwd", NULL, uid, 1, pw->passwd,
                     sizeof(pw->passwd), &pw->uid, &pw->gid) < 0)
    return -1;
  pw->name[0] = '\0';
  return 0;
}

static int getgrnam(const char *name, unsigned *gid) {
  return lookup_account("/etc/group", name, 0, 2, NULL, 0, gid, NULL);
}

static int getspnam_hash(const char *name, char *out, size_t outcap) {
  return lookup_account("/etc/shadow", name, 0, 1, out, outcap, NULL, NULL);
}

/* dns
 */

static unsigned char dns_servers[3][4]; /* a.b.c.d bytes */
static int n_dns_servers;

static int parse_ipv4(const char *s, unsigned char out[4]) {
  int parts = 0;
  while (*s) {
    if (parts == 4)
      return -1;
    unsigned v = 0;
    int digits = 0;
    while (is_digit((unsigned char)*s)) {
      v = v * 10 + (unsigned)(*s - '0');
      if (v > 255)
        return -1;
      s++;
      digits++;
    }
    if (!digits)
      return -1;
    out[parts++] = (unsigned char)v;
    if (*s == '.') {
      s++;
      if (!*s)
        return -1;
    } else if (*s) {
      return -1;
    }
  }
  if (parts != 4)
    return -1;
  return 0;
}

static void load_resolv_conf(void) {
  n_dns_servers = 0;
  char buf[2048];
  if (read_small_file("/etc/resolv.conf", buf, sizeof(buf)) < 0)
    return;
  char *line = buf;
  while (line && *line && n_dns_servers < 3) {
    char *next = strchr(line, '\n');
    if (next)
      *next++ = '\0';
    char *p = skip_ws(line);
    if (strncmp(p, "nameserver", 10) == 0 && (p[10] == ' ' || p[10] == '\t')) {
      p = skip_ws(p + 10);
      if (parse_ipv4(p, dns_servers[n_dns_servers]) == 0)
        n_dns_servers++;
    }
    line = next;
  }
}

static int hosts_lookup(const char *host, unsigned char out[4]) {
  char buf[16384];
  if (read_small_file("/etc/hosts", buf, sizeof(buf)) < 0)
    return -1;
  char *line = buf;
  while (line && *line) {
    char *next = strchr(line, '\n');
    if (next)
      *next++ = '\0';
    char *p = line;
    while (*p == ' ' || *p == '\t')
      p++;
    if (*p == '#' || *p == '\0') {
      line = next;
      continue;
    }
    unsigned char ip[4];
    char *tok = p;
    while (*p && *p != ' ' && *p != '\t')
      p++;
    if (*p)
      *p++ = '\0';
    if (parse_ipv4(tok, ip) == 0) {
      /* any following token matching host wins */
      while (*p) {
        while (*p == ' ' || *p == '\t')
          p++;
        if (!*p)
          break;
        char *name = p;
        while (*p && *p != ' ' && *p != '\t')
          p++;
        int end = (*p == '\0');
        if (!end)
          *p++ = '\0';
        if (strcmp(name, host) == 0) {
          memcpy(out, ip, 4);
          return 0;
        }
        if (end)
          break;
      }
    }
    line = next;
  }
  return -1;
}

/* skip a dns name at pos, returns new pos or -1 */
static long dns_skip_name(const unsigned char *b, size_t len, size_t pos) {
  while (1) {
    if (pos >= len)
      return -1;
    unsigned char l = b[pos];
    if (l == 0)
      return (long)pos + 1;
    if ((l & 0xC0) == 0xC0)
      return (long)pos + 2;
    pos += (size_t)(1 + l);
  }
}

static int dns_query(const unsigned char server[4], const char *host,
                     unsigned char out[4]) {
  unsigned char q[512];
  unsigned char r[512];
  unsigned short id;
  fill_random(&id, sizeof(id));
  size_t qlen = 0;
  q[qlen++] = (unsigned char)(id >> 8);
  q[qlen++] = (unsigned char)id;
  q[qlen++] = 0x01; /* rd */
  q[qlen++] = 0x00;
  q[qlen++] = 0x00;
  q[qlen++] = 0x01; /* qdcount */
  q[qlen++] = 0x00;
  q[qlen++] = 0x00;
  q[qlen++] = 0x00;
  q[qlen++] = 0x00;
  q[qlen++] = 0x00;
  q[qlen++] = 0x00;
  const char *p = host;
  while (*p) {
    const char *dot = strchr(p, '.');
    size_t lab = dot ? (size_t)(dot - p) : strlen(p);
    if (lab == 0 || lab > 63 || qlen + lab + 1 > sizeof(q) - 8)
      return -1;
    q[qlen++] = (unsigned char)lab;
    memcpy(q + qlen, p, lab);
    qlen += lab;
    p += lab;
    if (dot)
      p++;
  }
  if (p == host)
    return -1;
  q[qlen++] = 0;
  q[qlen++] = 0;
  q[qlen++] = 1; /* type a */
  q[qlen++] = 0;
  q[qlen++] = 1; /* class in */
  q[qlen++] = 0;

  int s = (int)sysret(k_socket(AF_INET, SOCK_DGRAM, 0));
  if (s < 0)
    return -1;
  sockaddr_in_t dest;
  memset(&dest, 0, sizeof(dest));
  dest.sin_family = AF_INET;
  unsigned char *sp = (unsigned char *)&dest.sin_addr;
  sp[0] = server[0];
  sp[1] = server[1];
  sp[2] = server[2];
  sp[3] = server[3];
  unsigned char *pp = (unsigned char *)&dest.sin_port;
  pp[0] = 0;
  pp[1] = 53;
  int got = -1;
  for (int attempt = 0; attempt < 2 && got < 0; attempt++) {
    if (sysret(k_sendto(s, q, qlen, 0, &dest, sizeof(dest))) < 0)
      break;
    struct pollfd_t pfd;
    pfd.fd = s;
    pfd.events = POLLIN;
    int pr = (int)sysret(k_poll(&pfd, 1, 2000));
    if (pr <= 0)
      continue;
    ssize_t n = safe_read(s, r, sizeof(r));
    if (n < 12)
      continue;
    if (r[0] != q[0] || r[1] != q[1])
      continue;
    unsigned flags = ((unsigned)r[2] << 8) | r[3];
    if (!(flags & 0x8000) || (flags & 0x0f))
      continue;
    unsigned an = ((unsigned)r[6] << 8) | r[7];
    if (an == 0)
      continue;
    long pos = dns_skip_name(r, (size_t)n, 12);
    if (pos < 0)
      continue;
    pos += 4;
    for (unsigned i = 0; i < an; i++) {
      long npos = dns_skip_name(r, (size_t)n, (size_t)pos);
      if (npos < 0 || npos + 10 > n) {
        npos = -1;
        break;
      }
      unsigned type = ((unsigned)r[npos] << 8) | r[npos + 1];
      unsigned rdlen = ((unsigned)r[npos + 8] << 8) | r[npos + 9];
      pos = npos + 10;
      if (type == 1 && rdlen == 4 && pos + 4 <= n) {
        memcpy(out, r + pos, 4);
        got = 0;
        break;
      }
      pos += rdlen;
    }
  }
  sysret(k_close(s));
  return got;
}

/* resolve a hostname to 4 ipv4 bytes, numeric first then files then dns */
static int resolve_host(const char *host, unsigned char out[4]) {
  if (!host || !*host)
    return -1;
  if (parse_ipv4(host, out) == 0)
    return 0;
  if (hosts_lookup(host, out) == 0)
    return 0;
  size_t hl = strlen(host);
  if (hl == 0 || hl > 253)
    return -1;
  for (const char *p = host; *p; p++)
    if (!(is_alnum((unsigned char)*p) || *p == '-' || *p == '.'))
      return -1;
  if (n_dns_servers == 0)
    load_resolv_conf();
  for (int i = 0; i < n_dns_servers; i++)
    if (dns_query(dns_servers[i], host, out) == 0)
      return 0;
  return -1;
}

/* httpd.conf storage
 */

struct mime_entry {
  char ext[24];
  char type[64];
};
static struct mime_entry mime_tab[MAX_MIME];
static int n_mime;

struct interp_entry {
  char ext[24];
  char cmd[152];
};
static struct interp_entry interp_tab[MAX_INTERP];
static int n_interp;

struct auth_entry {
  char path[256];
  char userpass[160];
};
static struct auth_entry auth_tab[MAX_AUTH];
static int n_auth;

struct ip_rule {
  unsigned ip;
  unsigned mask;
  char ad; /* 'A' or 'D' */
};
static struct ip_rule ip_tab[MAX_IP_RULE];
static int n_ip;

struct proxy_entry_t {
  char url_from[128];
  char host_port[128];
  char url_to[128];
};
static struct proxy_entry_t proxy_tab[MAX_PROXY];
static int n_proxy;

static const char *err_page_tab[N_RESPONSES];
static char err_page_buf[N_RESPONSES][152];

static char index_page_buf[64];
static const char *index_page = index_html;

static char home_httpd[PATH_BUF];

/* request state
 */

static const char *opt_c_config;
static const char *g_realm = "Web Server Authentication";
static char *g_query;
static int flg_deny_all;
static int content_gzip;
static long file_size = -1;
static long last_mod;
static long range_start = -1;
static long range_end;
static long range_len;
static const char *found_mime;
static const char *found_moved;
static char *remoteuser;
static char if_none_match_buf[IOBUF_SIZE];
static char *if_none_match;
static char etag_buf[64];

static char iobuf[IOBUF_SIZE];
static char hdr_buf[IOBUF_SIZE];
static char *hdr_ptr;
static int hdr_cnt;
static char urlcopy[IOBUF_SIZE + 64];
static char query_buf[IOBUF_SIZE];
static char remoteuser_buf[256];
static char conf_buf[16384];

/* httpd.conf parsing
 */

static void config_error(const char *line, const char *fname) {
  log_msg("config error '%s' in '%s'", line, fname);
}

/* ip mask parsing, matches busybox scan_ip and scan_ip_mask */
static int scan_ip(const char **strp, unsigned *ipp, char endc) {
  const char *p = *strp;
  int auto_mask = 8;
  unsigned ip = 0;

  if (*p == '/')
    return -auto_mask;
  for (int j = 0; j < 4; j++) {
    unsigned octet = 0;
    if ((*p < '0' || *p > '9') && *p != '/' && *p)
      return -auto_mask;
    while (*p >= '0' && *p <= '9') {
      octet *= 10;
      octet += (unsigned)(*p - '0');
      if (octet > 255)
        return -auto_mask;
      p++;
    }
    if (*p == '.')
      p++;
    if (*p != '/' && *p)
      auto_mask += 8;
    ip = (ip << 8) | octet;
  }
  if (*p) {
    if (*p != endc)
      return -auto_mask;
    p++;
    if (*p == '\0')
      return -auto_mask;
  }
  *ipp = ip;
  *strp = p;
  return auto_mask;
}

static int scan_ip_mask(const char *str, unsigned *ipp, unsigned *maskp) {
  int i = scan_ip(&str, ipp, '/');
  unsigned mask;
  const char *end;

  if (i < 0)
    return i;
  if (*str) {
    long long v = parse_ll(str, &end);
    i = (int)(v < 0 ? -1 : v);
    if (*end == '.')
      return scan_ip(&str, maskp, '\0') - 32;
    if (*end)
      return -1;
  }
  if (i > 32)
    return -1;
  if (i == 32) {
    mask = 0;
  } else {
    mask = 0xffffffff;
    mask >>= (unsigned)i;
  }
  *maskp = ~mask;
  return 0;
}

/* collapse //, /./ and /../ in an absolute path */
static void simplify_path(char *path) {
  char *out = path;
  char *in = path;
  *out++ = '/';
  in++;
  while (*in) {
    if (*in == '/') {
      in++;
      continue;
    }
    if (in[0] == '.' && (in[1] == '/' || in[1] == '\0')) {
      in++;
      continue;
    }
    if (in[0] == '.' && in[1] == '.' && (in[2] == '/' || in[2] == '\0')) {
      in += 2;
      while (out > path + 1) {
        out--;
        if (out[-1] == '/')
          break;
      }
      continue;
    }
    while (*in && *in != '/')
      *out++ = *in++;
    if (*in == '/') {
      *out++ = '/';
      in++;
    }
  }
  if (out > path + 1 && out[-1] == '/')
    out--;
  *out = '\0';
}

static int parse_conf(const char *path, int flag) {
  char filename[PATH_BUF + 32];
  const char *fname;
  char line[CONF_LINE_MAX];
  int fd;

  n_ip = 0;
  flg_deny_all = 0;
  if (flag != SUBDIR_PARSE) {
    n_mime = 0;
    n_interp = 0;
    n_auth = 0;
    index_page = index_html;
  }
  /* error pages and proxy entries survive reloads, like busybox */

  fname = opt_c_config;
  if (fname == NULL || flag == SUBDIR_PARSE) {
    ksnprintf(filename, sizeof(filename), "%s/%s", path, HTTPD_CONF_NAME);
    fname = filename;
  }

  fd = (int)sysret(k_open(fname, O_RDONLY));
  if (fd < 0) {
    if (flag >= SUBDIR_PARSE)
      return -1;
    if (flag == FIRST_PARSE && opt_c_config)
      die_errno(opt_c_config);
    flag = TRY_CURDIR_PARSE;
    fname = HTTPD_CONF_NAME;
    fd = (int)sysret(k_open(fname, O_RDONLY));
    if (fd < 0)
      return -1;
  }

  size_t total = 0;
  while (total + 1 < sizeof(conf_buf)) {
    ssize_t n = safe_read(fd, conf_buf + total, sizeof(conf_buf) - 1 - total);
    if (n <= 0)
      break;
    total += (size_t)n;
  }
  sysret(k_close(fd));
  conf_buf[total] = '\0';

  if (flag != SUBDIR_PARSE)
    path = "";

  char *cur = conf_buf;
  while (cur && *cur) {
    char *nl = strchr(cur, '\n');
    if (nl)
      *nl++ = '\0';
    /* strip all whitespace, cut at # */
    int w = 0;
    for (char *p = cur; *p && *p != '#'; p++) {
      if (*p != ' ' && *p != '\t') {
        if (w >= CONF_LINE_MAX - 1)
          break;
        line[w++] = *p;
      }
    }
    line[w] = '\0';
    if (w == 0) {
      cur = nl;
      continue;
    }

    char *after_colon = strchr(line, ':');
    if (after_colon == NULL || *++after_colon == '\0') {
      config_error(line, fname);
      cur = nl;
      continue;
    }

    char ch = (char)(line[0] & ~0x20);

    if (ch == 'I') {
      size_t l = strlen(after_colon);
      if (l >= sizeof(index_page_buf))
        l = sizeof(index_page_buf) - 1;
      memcpy(index_page_buf, after_colon, l);
      index_page_buf[l] = '\0';
      index_page = index_page_buf;
      cur = nl;
      continue;
    }

    if (flag == FIRST_PARSE && ch == 'H') {
      size_t l = strlen(after_colon);
      if (l >= sizeof(home_httpd))
        l = sizeof(home_httpd) - 1;
      memcpy(home_httpd, after_colon, l);
      home_httpd[l] = '\0';
      if (sysret(k_chdir(home_httpd)) < 0)
        die_errno(home_httpd);
      cur = nl;
      continue;
    }

    if (ch == 'A' || ch == 'D') {
      if (*after_colon == '*') {
        if (ch == 'D')
          flg_deny_all = 1;
        /* A:* is the default anyway */
      } else if (n_ip < MAX_IP_RULE) {
        unsigned ip = 0, mask = 0;
        if (scan_ip_mask(after_colon, &ip, &mask) != 0) {
          /* syntax error, protect all */
          ch = 'D';
          ip = 0;
          mask = 0;
        }
        if (ch == 'D') {
          /* deny rules first */
          for (int i = n_ip; i > 0; i--)
            ip_tab[i] = ip_tab[i - 1];
          ip_tab[0].ip = ip;
          ip_tab[0].mask = mask;
          ip_tab[0].ad = 'D';
          n_ip++;
        } else {
          ip_tab[n_ip].ip = ip;
          ip_tab[n_ip].mask = mask;
          ip_tab[n_ip].ad = 'A';
          n_ip++;
        }
      }
      cur = nl;
      continue;
    }

    if (flag == FIRST_PARSE && ch == 'E') {
      long long status = parse_ll(line + 1, NULL);
      if (status >= 100) {
        for (unsigned i = 0; i < N_RESPONSES; i++) {
          if (http_responses[i].code == (int)status) {
            size_t l = strlen(after_colon);
            if (l >= sizeof(err_page_buf[0]))
              l = sizeof(err_page_buf[0]) - 1;
            memcpy(err_page_buf[i], after_colon, l);
            err_page_buf[i][l] = '\0';
            err_page_tab[i] = err_page_buf[i];
            break;
          }
        }
      } else {
        config_error(line, fname);
      }
      cur = nl;
      continue;
    }

    if (flag == FIRST_PARSE && ch == 'P') {
      /* P:/url:[http://]hostname[:port]/new/path */
      char *host_port = strchr(after_colon, ':');
      if (host_port != NULL) {
        *host_port++ = '\0';
        if (strncmp(host_port, "http://", 7) == 0)
          host_port += 7;
        if (*host_port != '\0') {
          char *url_to = strchr(host_port, '/');
          if (url_to != NULL && n_proxy < MAX_PROXY) {
            size_t l;
            l = strlen(after_colon);
            if (l >= sizeof(proxy_tab[0].url_from))
              l = sizeof(proxy_tab[0].url_from) - 1;
            memcpy(proxy_tab[n_proxy].url_from, after_colon, l);
            proxy_tab[n_proxy].url_from[l] = '\0';
            l = (size_t)(url_to - host_port);
            if (l >= sizeof(proxy_tab[0].host_port))
              l = sizeof(proxy_tab[0].host_port) - 1;
            memcpy(proxy_tab[n_proxy].host_port, host_port, l);
            proxy_tab[n_proxy].host_port[l] = '\0';
            l = strlen(url_to);
            if (l >= sizeof(proxy_tab[0].url_to))
              l = sizeof(proxy_tab[0].url_to) - 1;
            memcpy(proxy_tab[n_proxy].url_to, url_to, l);
            proxy_tab[n_proxy].url_to[l] = '\0';
            n_proxy++;
            cur = nl;
            continue;
          }
        }
      }
      config_error(line, fname);
      cur = nl;
      continue;
    }

    ch = line[0];
    if (ch == '.' || (ch == '*' && line[1] == '.')) {
      if (ch == '.' && n_mime < MAX_MIME) {
        /* user mime lines win over builtin, prepend */
        for (int i = n_mime; i > 0; i--)
          mime_tab[i] = mime_tab[i - 1];
        size_t el = (size_t)(after_colon - line - 1);
        if (el >= sizeof(mime_tab[0].ext))
          el = sizeof(mime_tab[0].ext) - 1;
        memcpy(mime_tab[0].ext, line, el);
        mime_tab[0].ext[el] = '\0';
        size_t tl = strlen(after_colon);
        if (tl >= sizeof(mime_tab[0].type))
          tl = sizeof(mime_tab[0].type) - 1;
        memcpy(mime_tab[0].type, after_colon, tl);
        mime_tab[0].type[tl] = '\0';
        n_mime++;
      } else if (ch == '*' && n_interp < MAX_INTERP) {
        size_t el = (size_t)(after_colon - line - 1);
        if (el >= sizeof(interp_tab[0].ext))
          el = sizeof(interp_tab[0].ext) - 1;
        memcpy(interp_tab[0].ext, line, el);
        interp_tab[0].ext[el] = '\0';
        size_t cl = strlen(after_colon);
        if (cl >= sizeof(interp_tab[0].cmd))
          cl = sizeof(interp_tab[0].cmd) - 1;
        memcpy(interp_tab[0].cmd, after_colon, cl);
        interp_tab[0].cmd[cl] = '\0';
        n_interp++;
      }
      cur = nl;
      continue;
    }

    if (ch == '/') {
      /* /path:user:pass, subdir paths are relative to the subdir */
      if (n_auth < MAX_AUTH) {
        char full[384];
        ksnprintf(full, sizeof(full), "/%s%.*s", path,
                  (int)(after_colon - line - 1), line);
        simplify_path(full);
        size_t pl = strlen(full);
        int insert = n_auth;
        for (int i = 0; i < n_auth; i++) {
          if (pl >= strlen(auth_tab[i].path)) {
            insert = i;
            break;
          }
        }
        for (int i = n_auth; i > insert; i--)
          auth_tab[i] = auth_tab[i - 1];
        if (pl >= sizeof(auth_tab[0].path))
          pl = sizeof(auth_tab[0].path) - 1;
        memcpy(auth_tab[insert].path, full, pl);
        auth_tab[insert].path[pl] = '\0';
        size_t ul = strlen(after_colon);
        if (ul >= sizeof(auth_tab[0].userpass))
          ul = sizeof(auth_tab[0].userpass) - 1;
        memcpy(auth_tab[insert].userpass, after_colon, ul);
        auth_tab[insert].userpass[ul] = '\0';
        n_auth++;
      }
      cur = nl;
      continue;
    }

    config_error(line, fname);
    cur = nl;
  }
  return 0;
}

static void sighup_handler(int sig) {
  (void)sig;
  parse_conf(DEFAULT_PATH_HTTPD_CONF, SIGNALED_PARSE);
}

/* acl and auth
 */

static void if_ip_denied_send_forbidden_and_exit(unsigned remote_ip) {
  for (int i = 0; i < n_ip; i++) {
    if ((remote_ip & ip_tab[i].mask) == ip_tab[i].ip) {
      if (ip_tab[i].ad == 'A')
        return;
      send_headers_and_exit(HTTP_FORBIDDEN);
    }
  }
  if (flg_deny_all)
    send_headers_and_exit(HTTP_FORBIDDEN);
}

/* check user:passwd against the auth rules for path */
/* returns 1 if allowed, 0 if the password failed, no rules means allowed */
static int check_user_passwd(const char *path, char *user_and_passwd) {
  const char *prev = NULL;

  for (int i = 0; i < n_auth; i++) {
    const char *dir_prefix = auth_tab[i].path;
    int r = 0;

    if (prev && strcmp(prev, dir_prefix) != 0)
      continue;

    size_t len = strlen(dir_prefix);
    if (len != 1 &&
        (strncmp(dir_prefix, path, len) != 0 ||
         (path[len] != '/' && path[len] != '\0')))
      continue;

    prev = dir_prefix;

    char *colon = strchr(user_and_passwd, ':');
    const char *up = auth_tab[i].userpass;
    const char *passwd = NULL;
    int have_colon = colon != NULL;

    if (!have_colon) {
      /* no colon in peer input, plain compare */
      r = strcmp(up, user_and_passwd);
    } else {
      if (up[0] != '*' && strncmp(up, user_and_passwd,
                                  (size_t)(colon - user_and_passwd + 1)) != 0)
        continue;
      const char *pwcolon = strchr(up, ':');
      if (pwcolon == NULL) {
        /* bad input line, plain compare */
        r = strcmp(up, user_and_passwd);
      } else {
        passwd = pwcolon + 1;
        if (passwd[0] == '*') {
          /* use the system password for this user */
          struct pw_entry pw;
          *colon = '\0';
          int ok = getpwnam(user_and_passwd, &pw) == 0;
          *colon = ':';
          if (!ok || pw.passwd[0] == '\0')
            continue;
          passwd = pw.passwd;
          if ((passwd[0] == 'x' || passwd[0] == '*') && passwd[1] == '\0') {
            static char shadow[192];
            if (getspnam_hash(user_and_passwd, shadow, sizeof(shadow)) == 0)
              passwd = shadow;
          }
          r = strcmp(pw_encrypt(colon + 1, passwd), passwd);
        } else if (passwd[0] == '$' && is_digit((unsigned char)passwd[1])) {
          r = strcmp(pw_encrypt(colon + 1, passwd), passwd);
        } else {
          r = strcmp(colon + 1, passwd);
        }
      }
    }

    if (r == 0) {
      size_t ul = have_colon ? (size_t)(colon - user_and_passwd)
                             : strlen(user_and_passwd);
      if (ul >= sizeof(remoteuser_buf))
        ul = sizeof(remoteuser_buf) - 1;
      memcpy(remoteuser_buf, user_and_passwd, ul);
      remoteuser_buf[ul] = '\0';
      remoteuser = remoteuser_buf;
      return 1;
    }
  }
  return prev == NULL;
}

/* mime
 */

static const char *const mime_groups[][2] = {
    {".txt.h.c.cc.cpp", "text/plain"},
    {".htm.html", "text/html"},
    {".jpg.jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".png", "image/png"},
    {".svg", "image/svg+xml"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".wav", "audio/wav"},
    {".avi", "video/x-msvideo"},
    {".qt.mov", "video/quicktime"},
    {".mpe.mpeg", "video/mpeg"},
    {".mid.midi", "audio/midi"},
    {".mp3", "audio/mpeg"},
};

static void lookup_mime(const char *suffix) {
  found_mime = NULL;
  for (unsigned g = 0; g < sizeof(mime_groups) / sizeof(mime_groups[0]); g++) {
    const char *group = mime_groups[g][0];
    const char *p = strstr(group, suffix);
    if (!p)
      continue;
    p += strlen(suffix);
    if (*p == '\0' || *p == '.') {
      found_mime = mime_groups[g][1];
      return;
    }
    /* partial match, can not match later groups either */
    return;
  }
  for (int i = 0; i < n_mime; i++) {
    if (strcmp(mime_tab[i].ext, suffix) == 0) {
      found_mime = mime_tab[i].type;
      return;
    }
  }
}

/* responses
 */

static void send_request_timeout_and_exit(int sig) {
  (void)sig;
  send_headers_and_exit(HTTP_REQUEST_TIMEOUT);
}

static void log_and_exit(void) {
  /* paranoia, ie is said to be confused by a plain close */
  sysret(k_shutdown(1, SHUT_WR));
  if (verbose > 2)
    log_msg("closed");
  exit_now(0);
}

static void send_headers_and_exit(unsigned responseNum) {
  content_gzip = 0;
  file_size = -1;
  send_headers(responseNum);
  log_and_exit();
}

static void send_headers(unsigned responseNum) {
  const char *responseString = "";
  const char *infoString = NULL;
  const char *error_page = NULL;
  size_t len;
  char date_str[40];

  for (unsigned i = 0; i < N_RESPONSES; i++) {
    if (http_responses[i].code == (int)responseNum) {
      responseString = http_responses[i].name;
      infoString = http_responses[i].info;
      error_page = err_page_tab[i];
      break;
    }
  }

  if (verbose)
    log_msg("response:%u", responseNum);

  fmt_rfc1123(date_str, now_sec());
  len = ksnprintf(iobuf, IOBUF_SIZE, "HTTP/1.1 %u %s\r\nDate: %s\r\n"
                                     "Connection: close\r\n",
                  responseNum, responseString, date_str);

  if (responseNum != HTTP_OK || found_mime) {
    len += ksnprintf(iobuf + len, IOBUF_SIZE - len, "Content-type: %s\r\n",
                     responseNum != HTTP_OK ? "text/html" : found_mime);
  }

  if (responseNum == HTTP_UNAUTHORIZED) {
    len += ksnprintf(iobuf + len, IOBUF_SIZE - len,
                     "WWW-Authenticate: Basic realm=\"%s\"\r\n", g_realm);
  }

  if (responseNum == HTTP_MOVED_TEMPORARILY) {
    len += ksnprintf(iobuf + len, IOBUF_SIZE - len, "Location: %s/%s%s\r\n",
                     found_moved, g_query ? "?" : "",
                     g_query ? g_query : "");
  }

  if (error_page && sysret(k_access(error_page, R_OK)) == 0) {
    if (len > IOBUF_SIZE - 3)
      len = IOBUF_SIZE - 3;
    iobuf[len++] = '\r';
    iobuf[len++] = '\n';
    full_write(1, iobuf, len);
    send_file_and_exit(error_page, SEND_BODY);
  }

  if (file_size != -1) {
    if (responseNum == HTTP_PARTIAL_CONTENT) {
      len += ksnprintf(iobuf + len, IOBUF_SIZE - len,
                       "Content-Range: bytes %ld-%ld/%ld\r\n", range_start,
                       range_end, file_size);
      file_size = range_end - range_start + 1;
    }
    fmt_rfc1123(date_str, last_mod);
    len += ksnprintf(iobuf + len, IOBUF_SIZE - len,
                     "Accept-Ranges: bytes\r\nLast-Modified: %s\r\n"
                     "ETag: %s\r\nContent-Length: %ld\r\n",
                     date_str, etag_buf, file_size);
  }

  if (content_gzip)
    len += ksnprintf(iobuf + len, IOBUF_SIZE - len,
                     "Content-Encoding: gzip\r\n");

  if (len > IOBUF_SIZE - 3)
    len = IOBUF_SIZE - 3;
  iobuf[len++] = '\r';
  iobuf[len++] = '\n';

  if (infoString) {
    len += ksnprintf(iobuf + len, IOBUF_SIZE - len,
                     "<HTML><HEAD><TITLE>%u %s</TITLE></HEAD>\n"
                     "<BODY><H1>%u %s</H1>\n%s\n</BODY></HTML>\n",
                     responseNum, responseString, responseNum, responseString,
                     infoString);
  }

  if (full_write(1, iobuf, len) != (ssize_t)len) {
    if (verbose > 1)
      log_msg("error writing to socket");
    log_and_exit();
  }
}

/* read a header line into iobuf, \r removed, \n becomes NUL */
static unsigned get_line(void) {
  unsigned count = 0;

  while (1) {
    if (hdr_cnt <= 0) {
      sysret(k_alarm(HEADER_READ_TIMEOUT));
      hdr_cnt = (int)safe_read(0, hdr_buf, sizeof(hdr_buf));
      if (hdr_cnt <= 0)
        goto ret;
      hdr_ptr = hdr_buf;
    }
    hdr_cnt--;
    char c = *hdr_ptr++;
    if (c == '\r')
      continue;
    if (c == '\n')
      break;
    iobuf[count] = c;
    if (count < IOBUF_SIZE - 1)
      count++;
  }
ret:
  iobuf[count] = '\0';
  return count;
}

/* file serving
 */

static void send_file_and_exit(const char *url, int what) {
  char gzbuf[IOBUF_SIZE + 16];
  int fd;
  ssize_t count;

  if (content_gzip) {
    /* does <url>.gz exist, then use it instead */
    ksnprintf(gzbuf, sizeof(gzbuf), "%s.gz", url);
    fd = (int)sysret(k_open(gzbuf, O_RDONLY));
    if (fd >= 0) {
      stat_t sb;
      if (sysret(k_fstat(fd, &sb)) == 0) {
        file_size = (long)sb.st_size;
        last_mod = (long)sb.st_mtime;
      }
    } else {
      content_gzip = 0;
      fd = (int)sysret(k_open(url, O_RDONLY));
    }
  } else {
    fd = (int)sysret(k_open(url, O_RDONLY));
  }

  if (fd < 0) {
    /* error pages must not recurse into 404 handling */
    if (what != SEND_BODY)
      send_headers_and_exit(HTTP_NOT_FOUND);
    log_and_exit();
  }

  ksnprintf(etag_buf, sizeof(etag_buf), "\"%llx-%llx\"",
            (unsigned long long)last_mod, (unsigned long long)file_size);

  if (if_none_match && strstr(if_none_match, etag_buf))
    send_headers_and_exit(HTTP_NOT_MODIFIED);

  set_signal(SIG_PIPE, SIG_IGN_PTR);

  const char *suffix = strrchr(url, '.');
  if (suffix)
    lookup_mime(suffix);

  range_len = 0x7fffffffffffffffL;
  if (range_start >= 0) {
    if (range_end == 0 || range_end > file_size - 1)
      range_end = file_size - 1;
    if (range_end < range_start ||
        sysret(k_lseek(fd, range_start, SEEK_SET)) != range_start) {
      sysret(k_lseek(fd, 0, SEEK_SET));
      range_start = -1;
    } else {
      range_len = range_end - range_start + 1;
      send_headers(HTTP_PARTIAL_CONTENT);
      what = SEND_BODY;
    }
  }

  if (what & SEND_HEADERS)
    send_headers(HTTP_OK);

  /* note, the body is sent even for HEAD, same as busybox httpd */
  while ((count = safe_read(fd, iobuf, IOBUF_SIZE)) > 0) {
    if (count > range_len)
      count = (ssize_t)range_len;
    if (full_write(1, iobuf, (size_t)count) != count)
      break;
    range_len -= count;
    if (range_len == 0)
      break;
  }
  log_and_exit();
}

/* cgi
 */

static char *cenv[MAX_CGI_ENV];
static int n_cenv;
static char cgi_env_buf[CGI_ENV_BUF];
static size_t cgi_env_len;

static void env_reset(void) {
  n_cenv = 0;
  cgi_env_len = 0;
}

static int env_name_matches(const char *entry, const char *name) {
  size_t nl = strlen(name);
  if (strncmp(entry, name, nl) != 0)
    return 0;
  return entry[nl] == '=';
}

/* add name=value, ignores the entry on overflow */
static void env_add(const char *name, const char *value) {
  if (n_cenv >= MAX_CGI_ENV - 1)
    return;
  size_t need = strlen(name) + (value ? strlen(value) : 0) + 2;
  if (cgi_env_len + need > sizeof(cgi_env_buf))
    return;
  char *slot = cgi_env_buf + cgi_env_len;
  size_t n = ksnprintf(slot, sizeof(cgi_env_buf) - cgi_env_len, "%s=%s", name,
                       value ? value : "");
  cgi_env_len += n + 1;
  cenv[n_cenv++] = slot;
}

/* copy the inherited environment, skipping names we set ourselves */
static void env_copy_inherited(void) {
  static const char *const ours[] = {
      "PATH_INFO",       "REQUEST_METHOD", "REQUEST_URI",
      "SCRIPT_FILENAME", "SCRIPT_NAME",    "QUERY_STRING",
      "SERVER_SOFTWARE", "SERVER_PROTOCOL", "GATEWAY_INTERFACE",
      "REMOTE_ADDR",     "REMOTE_PORT",    "CONTENT_LENGTH",
      "REMOTE_USER",     "AUTH_TYPE",      "CONTENT_TYPE"};
  for (char **e = environ; e && *e; e++) {
    int skip = 0;
    for (unsigned i = 0; i < sizeof(ours) / sizeof(ours[0]); i++)
      if (env_name_matches(*e, ours[i])) {
        skip = 1;
        break;
      }
    if (!skip && strncmp(*e, "HTTP_", 5) == 0)
      skip = 1;
    if (!skip && n_cenv < MAX_CGI_ENV - 1)
      cenv[n_cenv++] = *e;
  }
}

/* add an already formatted NAME=VALUE string from iobuf */
static void env_add_raw(int http_prefix, const char *name, size_t namelen,
                        const char *value) {
  if (n_cenv >= MAX_CGI_ENV - 1)
    return;
  size_t need = namelen + strlen(value) + 8;
  if (cgi_env_len + need > sizeof(cgi_env_buf))
    return;
  char *slot = cgi_env_buf + cgi_env_len;
  size_t n = ksnprintf(slot, sizeof(cgi_env_buf) - cgi_env_len, "%s%.*s=%s",
                       http_prefix ? "HTTP_" : "", (int)namelen, name, value);
  cgi_env_len += n + 1;
  cenv[n_cenv++] = slot;
}

/* pump data between the network (fd 0/1) and a cgi script or proxy socket */
static void cgi_io_loop_and_exit(int fromCgi_rd, int toCgi_wr, int post_len) {
  enum { NET = 0, FROM_CGI = 1, TO_CGI = 2 };
  struct pollfd_t pfd[3];
  int out_cnt;
  int count;

  /* if cgi dies we still want to finish reading its output */
  set_signal(SIG_PIPE, SIG_IGN_PTR);

  post_len -= hdr_cnt;
  out_cnt = 0;
  pfd[FROM_CGI].fd = fromCgi_rd;
  pfd[FROM_CGI].events = POLLIN;
  pfd[TO_CGI].fd = toCgi_wr;

  while (1) {
    pfd[NET].fd = -1;
    pfd[NET].events = POLLIN;
    pfd[NET].revents = 0;
    pfd[TO_CGI].events = POLLOUT;
    pfd[TO_CGI].revents = 0;

    if (toCgi_wr && hdr_cnt <= 0) {
      if (post_len > 0) {
        pfd[NET].fd = 0;
      } else {
        /* no more post data, let cgi see EOF on its stdin */
        if (toCgi_wr != fromCgi_rd)
          sysret(k_close(toCgi_wr));
        toCgi_wr = 0;
      }
    }

    int pr;
    do {
      pr = (int)sysret(
          k_poll(pfd, hdr_cnt > 0 ? TO_CGI + 1 : FROM_CGI + 1, -1));
    } while (pr < 0 && kerrno == EINTR);
    if (pr <= 0)
      break;

    if (pfd[TO_CGI].revents) {
      count = (int)safe_write(toCgi_wr, hdr_ptr, (size_t)hdr_cnt);
      if (count > 0) {
        hdr_ptr += count;
        hdr_cnt -= count;
      } else {
        /* EOF or broken pipe to cgi, stop piping post data */
        hdr_cnt = 0;
        post_len = 0;
      }
    }

    if (pfd[NET].revents) {
      count = (int)safe_read(0, hdr_buf, sizeof(hdr_buf));
      if (count > 0) {
        hdr_cnt = count;
        hdr_ptr = hdr_buf;
        post_len -= count;
      } else {
        post_len = 0;
      }
    }

    if (pfd[FROM_CGI].revents) {
      char *rbuf = iobuf;

      if (out_cnt >= 0) {
        /* initial buffering, to decide whether to add our own header */
        count = (int)safe_read(fromCgi_rd, rbuf + out_cnt, IOBUF_SIZE - 8);
        if (count <= 0) {
          if (out_cnt) {
            full_write(1, HTTP_200, sizeof(HTTP_200) - 1);
            full_write(1, rbuf, (size_t)out_cnt);
          }
          break;
        }
        out_cnt += count;
        count = 0;
        if (out_cnt >= 8 && memcmp(rbuf, "Status: ", 8) == 0) {
          /* send "HTTP/1.1 " and skip "Status: " */
          if (full_write(1, HTTP_200, 9) != 9)
            break;
          rbuf += 8;
          count = out_cnt - 8;
          out_cnt = -1;
        } else if (out_cnt >= 4) {
          if (memcmp(rbuf, HTTP_200, 4) != 0) {
            if (full_write(1, HTTP_200, sizeof(HTTP_200) - 1) !=
                (ssize_t)(sizeof(HTTP_200) - 1))
              break;
          }
          count = out_cnt;
          out_cnt = -1;
        }
      } else {
        count = (int)safe_read(fromCgi_rd, rbuf, IOBUF_SIZE);
        if (count <= 0)
          break;
      }
      if (full_write(1, rbuf, (size_t)count) != count)
        break;
    }
  }
  log_and_exit();
}

static void xpipe(int fds[2]) {
  if (sysret(k_pipe(fds)) < 0) {
    log_msg("pipe");
    exit_now(1);
  }
}

static void send_cgi_and_exit(const char *url, const char *orig_uri,
                              const char *request, int post_len) {
  char cgi_url[IOBUF_SIZE + 64];
  int fromCgi[2];
  int toCgi[2];
  char *script, *last_slash;
  int pid;

  strcpy(cgi_url, url);
  url = cgi_url;

  /* check for [dirs/]script.cgi/PATH_INFO */
  last_slash = script = (char *)url;
  while ((script = strchr(script + 1, '/')) != NULL) {
    stat_t sb;
    *script = '\0';
    int dir = sysret(k_stat(url + 1, &sb)) == 0 &&
              (sb.st_mode & S_IFMT) == S_IFDIR;
    *script = '/';
    if (!dir)
      break;
    last_slash = script;
  }
  env_add("PATH_INFO", script ? script : "");
  env_add("REQUEST_METHOD", request);
  if (g_query) {
    char ru[IOBUF_SIZE + 64];
    ksnprintf(ru, sizeof(ru), "%s?%s", orig_uri, g_query);
    env_add("REQUEST_URI", ru);
  } else {
    env_add("REQUEST_URI", orig_uri);
  }
  if (script)
    *script = '\0';

  /* SCRIPT_FILENAME is required by php in cgi mode */
  if (home_httpd[0] == '/') {
    char fullpath[PATH_BUF + IOBUF_SIZE];
    ksnprintf(fullpath, sizeof(fullpath), "%s%s", home_httpd, url);
    env_add("SCRIPT_FILENAME", fullpath);
  }
  env_add("SCRIPT_NAME", url);
  env_add("QUERY_STRING", g_query ? g_query : "");
  env_add("SERVER_SOFTWARE", SERVER_SOFTWARE);
  env_add("SERVER_PROTOCOL", "HTTP/1.1");
  env_add("GATEWAY_INTERFACE", "CGI/1.1");
  {
    const char *p = rmt_ip_set ? rmt_ip : NULL;
    const char *cp = p ? strrchr(p, ':') : NULL;
    char addr[64];
    addr[0] = '\0';
    if (p) {
      size_t l = cp ? (size_t)(cp - p) : strlen(p);
      if (l >= sizeof(addr))
        l = sizeof(addr) - 1;
      memcpy(addr, p, l);
      addr[l] = '\0';
    }
    env_add("REMOTE_ADDR", addr);
    if (cp)
      env_add("REMOTE_PORT", cp + 1);
  }
  if (post_len) {
    char cl[32];
    ksnprintf(cl, sizeof(cl), "%d", post_len);
    env_add("CONTENT_LENGTH", cl);
  }
  if (remoteuser) {
    env_add("REMOTE_USER", remoteuser);
    env_add("AUTH_TYPE", "Basic");
  }
  cenv[n_cenv] = NULL;

  xpipe(fromCgi);
  xpipe(toCgi);

  pid = (int)sysret(k_fork());
  if (pid < 0)
    log_and_exit();

  if (pid == 0) {
    char *argv[3];

    sysret(k_close(toCgi[1]));
    sysret(k_close(fromCgi[0]));
    if (sysret(k_dup2(toCgi[0], 0)) < 0)
      exit_now(242);
    if (toCgi[0] != 0)
      sysret(k_close(toCgi[0]));
    if (sysret(k_dup2(fromCgi[1], 1)) < 0)
      exit_now(242);
    if (fromCgi[1] != 1)
      sysret(k_close(fromCgi[1]));

    /* chdir to the script dir */
    script = last_slash;
    if (script != url) {
      *script = '\0';
      if (sysret(k_chdir(url + 1)) < 0)
        send_headers_and_exit(HTTP_NOT_FOUND);
    }
    script++;

    argv[0] = script;
    argv[1] = NULL;

    const char *suffix = strrchr(script, '.');
    if (suffix) {
      for (int i = 0; i < n_interp; i++) {
        if (strcmp(interp_tab[i].ext + 1, suffix) == 0) {
          argv[0] = interp_tab[i].cmd;
          argv[1] = script;
          argv[2] = NULL;
          break;
        }
      }
    }

    /* restore default signal dispositions for the cgi process */
    set_signal(SIG_CHLD, SIG_DFL_PTR);
    set_signal(SIG_PIPE, SIG_DFL_PTR);
    set_signal(SIG_HUP, SIG_DFL_PTR);

    /* no PATH search, argv[0] is relative to the script dir */
    sysret(k_execve(argv[0], argv, cenv));
    if (verbose)
      log_msg("can't execute '%s'", argv[0]);
    send_headers_and_exit(HTTP_NOT_FOUND);
  }

  /* parent, pump the data */
  sysret(k_close(fromCgi[1]));
  sysret(k_close(toCgi[0]));
  cgi_io_loop_and_exit(fromCgi[0], toCgi[1], post_len);
}

/* request handling
 */

static int find_proxy_and_relay(const char *url, const char *method,
                                const char *http_ver) {
  for (int i = 0; i < n_proxy; i++) {
    if (strncmp(url, proxy_tab[i].url_from,
                strlen(proxy_tab[i].url_from)) == 0) {
      if (verbose > 1)
        log_msg("proxy:%s", url);
      unsigned char ip[4];
      const char *host = proxy_tab[i].host_port;
      const char *portsep = strrchr(host, ':');
      long port = 80;
      char hostbuf[152];
      if (portsep) {
        size_t hl = (size_t)(portsep - host);
        if (hl >= sizeof(hostbuf))
          hl = sizeof(hostbuf) - 1;
        memcpy(hostbuf, host, hl);
        hostbuf[hl] = '\0';
        port = parse_ll(portsep + 1, NULL);
        if (port < 1 || port > 0xffff)
          return -1;
      } else {
        strcpy(hostbuf, host);
      }
      if (resolve_host(hostbuf, ip) < 0)
        return -1;
      int fd = (int)sysret(k_socket(AF_INET, SOCK_STREAM, 0));
      if (fd < 0)
        return -1;
      sockaddr_in_t addr;
      memset(&addr, 0, sizeof(addr));
      addr.sin_family = AF_INET;
      unsigned char *ap = (unsigned char *)&addr.sin_addr;
      ap[0] = ip[0];
      ap[1] = ip[1];
      ap[2] = ip[2];
      ap[3] = ip[3];
      unsigned char *pp = (unsigned char *)&addr.sin_port;
      pp[0] = (unsigned char)(port >> 8);
      pp[1] = (unsigned char)(port & 0xff);
      if (sysret(k_connect(fd, &addr, sizeof(addr))) < 0) {
        sysret(k_close(fd));
        return -1;
      }
      sysret(k_alarm(0));
      char hdr[IOBUF_SIZE + 128];
      size_t n = ksnprintf(hdr, sizeof(hdr), "%s %s%s %s\r\n", method,
                           proxy_tab[i].url_to,
                           url + strlen(proxy_tab[i].url_from), http_ver);
      if (full_write(fd, hdr, n) != (ssize_t)n) {
        sysret(k_close(fd));
        return -1;
      }
      cgi_io_loop_and_exit(fd, fd, 0x7fffffff);
    }
  }
  return 0;
}

static void handle_incoming_and_exit(const sockaddr_in_t *from) {
  stat_t sb;
  char *urlp, *tptr;
  unsigned remote_ip;
  unsigned total_headers_len;
  unsigned long POST_length;
  char method_buf[16];
  const char *prequest;
  int method_id;
  int cgi_type = CGI_NONE;
  int authorized = -1;
  char *HTTP_slash;
  const char *http_ver;

  env_reset();
  env_copy_inherited();

  if (from->sin_family == AF_INET) {
    unsigned char *b = (unsigned char *)&from->sin_addr;
    ksnprintf(rmt_ip, sizeof(rmt_ip), "%u.%u.%u.%u:%u", b[0], b[1], b[2], b[3],
              (((unsigned char *)&from->sin_port)[0] << 8) |
                  ((unsigned char *)&from->sin_port)[1]);
    rmt_ip_set = 1;
  }
  if (verbose > 2)
    log_msg("connected");

  remote_ip = 0;
  if (from->sin_family == AF_INET) {
    unsigned char *b = (unsigned char *)&from->sin_addr;
    remote_ip = ((unsigned)b[0] << 24) | ((unsigned)b[1] << 16) |
                ((unsigned)b[2] << 8) | (unsigned)b[3];
  }
  if_ip_denied_send_forbidden_and_exit(remote_ip);

  set_signal(SIG_ALRM, send_request_timeout_and_exit);

  if (get_line() == 0) {
    /* firefox speculatively opens connections and sends nothing */
    if (verbose > 2)
      log_msg("eof on read, closing");
    exit_now(0);
  }

  /* request line: METHOD /url HTTP/x.y */
  urlp = strchr(iobuf, ' ');
  if (urlp == NULL)
    send_headers_and_exit(HTTP_BAD_REQUEST);
  *urlp++ = '\0';
  if (urlp[0] != '/')
    send_headers_and_exit(HTTP_BAD_REQUEST);
  HTTP_slash = strchr(urlp, ' ');
  if (HTTP_slash == NULL || strncmp(HTTP_slash + 1, "HTTP/", 5) != 0)
    send_headers_and_exit(HTTP_BAD_REQUEST);
  *HTTP_slash++ = '\0';
  http_ver = HTTP_slash;

  if (find_proxy_and_relay(urlp, iobuf, http_ver) != 0)
    send_headers_and_exit(HTTP_INTERNAL_SERVER_ERROR);

  prequest = "GET";
  method_id = 0;
  if (strcasecmp(iobuf, "GET") == 0)
    goto found;
  prequest = "HEAD";
  method_id = 1;
  if (strcasecmp(iobuf, "HEAD") == 0)
    goto found;
  prequest = "POST";
  method_id = 2;
  if (strcasecmp(iobuf, "POST") == 0)
    goto found;
  /* other methods are allowed for cgi targets */
  {
    size_t l = strlen(iobuf);
    if (l >= sizeof(method_buf))
      l = sizeof(method_buf) - 1;
    memcpy(method_buf, iobuf, l);
    method_buf[l] = '\0';
    prequest = method_buf;
    method_id = 3;
  }
found:

  strcpy(urlcopy, urlp);

  g_query = strchr(urlcopy, '?');
  if (g_query)
    *g_query++ = '\0';

  tptr = percent_decode_in_place(urlcopy, 1);
  if (tptr == NULL)
    send_headers_and_exit(HTTP_BAD_REQUEST);
  if (tptr == urlcopy + 1) {
    /* '/' or NUL was encoded */
    send_headers_and_exit(HTTP_NOT_FOUND);
  }

  /* canonicalize the path, stolen from busybox bb_simplify_path */
  urlp = tptr = urlcopy;
  while (1) {
    if (*urlp == '/') {
      if (*tptr == '/')
        goto next_char;
      if (*tptr == '.') {
        if (tptr[1] == '.' && (tptr[2] == '/' || tptr[2] == '\0')) {
          /* "..", protect the root */
          if (urlp == urlcopy)
            send_headers_and_exit(HTTP_BAD_REQUEST);
          while (*--urlp != '/')
            continue;
          tptr++;
        }
        if (tptr[1] == '/' || tptr[1] == '\0')
          goto next_char;
      }
    }
    *++urlp = *tptr;
    if (*tptr == '\0')
      break;
  next_char:
    tptr++;
  }

  if (verbose > 1)
    log_msg("url:%s", urlcopy);

  /* per directory httpd.conf, merged for this request only */
  tptr = urlcopy;
  while ((tptr = strchr(tptr + 1, '/')) != NULL) {
    *tptr = '\0';
    if (parse_conf(urlcopy + 1, SUBDIR_PARSE) == 0)
      if_ip_denied_send_forbidden_and_exit(remote_ip);
    *tptr = '/';
  }

  tptr = urlcopy + 1;

  if (strncmp(tptr, "cgi-bin/", 8) == 0) {
    if (tptr[8] == '\0')
      send_headers_and_exit(HTTP_FORBIDDEN);
    cgi_type = CGI_NORMAL;
  }

  if (urlp[-1] == '/') {
    /* the index page overwrite would clobber the query, deep copy it */
    if (g_query) {
      strcpy(query_buf, g_query);
      g_query = query_buf;
    }
    strcpy(urlp, index_page);
  }
  if (sysret(k_stat(tptr, &sb)) == 0) {
    if (urlp[-1] != '/' && (sb.st_mode & S_IFMT) == S_IFDIR) {
      found_moved = urlcopy;
    } else {
      const char *suffix = strrchr(tptr, '.');
      if (suffix) {
        for (int i = 0; i < n_interp; i++) {
          if (strcmp(interp_tab[i].ext + 1, suffix) == 0) {
            cgi_type = CGI_INTERPRETER;
            break;
          }
        }
      }
      file_size = (long)sb.st_size;
      last_mod = (long)sb.st_mtime;
    }
  } else if (urlp[-1] == '/') {
    /* a dir url with no index page, try cgi-bin/index.cgi */
    if (sysret(k_access("cgi-bin/index.cgi", X_OK)) != 0)
      send_headers_and_exit(HTTP_NOT_FOUND);
    cgi_type = CGI_INDEX;
  }

  /* auth checks would be confused by the appended index page, truncate */
  urlp[0] = '\0';

  total_headers_len = 0;
  POST_length = 0;

  while (1) {
    unsigned iobuf_len = get_line();
    if (iobuf_len == 0)
      break;
    total_headers_len += iobuf_len;
    if (total_headers_len >= MAX_HTTP_HEADERS_SIZE)
      send_headers_and_exit(HTTP_ENTITY_TOO_LARGE);

    if (method_id == 2 && strncasecmp(iobuf, "Content-Length:", 15) == 0) {
      tptr = skip_ws(iobuf + 15);
      if (!tptr[0])
        send_headers_and_exit(HTTP_BAD_REQUEST);
      const char *end;
      long long v = parse_ll(tptr, &end);
      if (v < 0 || *end || v > 0x7fffffff)
        send_headers_and_exit(HTTP_BAD_REQUEST);
      POST_length = (unsigned long)v;
      continue;
    }

    if (strncasecmp(iobuf, "Authorization:", 14) == 0) {
      tptr = skip_ws(iobuf + 14);
      if (strncasecmp(tptr, "Basic", 5) == 0) {
        tptr += 5;
        decode_base64(tptr);
        authorized = check_user_passwd(urlcopy, tptr);
        continue;
      }
    }

    if (strncasecmp(iobuf, "Range:", 6) == 0) {
      /* we know only bytes=NNN-[MMM] */
      char *s = skip_ws(iobuf + 6);
      if (strncmp(s, "bytes=", 6) == 0) {
        const char *end;
        s += 6;
        range_start = parse_ll(s, &end);
        if (*end != '-' || range_start < 0) {
          range_start = -1;
        } else if (end[1]) {
          range_end = parse_ll(end + 1, NULL);
          if (range_end < range_start)
            range_start = -1;
        }
      }
      continue;
    }

    if (strncasecmp(iobuf, "Accept-Encoding:", 16) == 0) {
      if (strstr(iobuf, "gzip"))
        content_gzip = 1;
      continue;
    }

    if (strncasecmp(iobuf, "If-None-Match:", 14) == 0) {
      tptr = skip_ws(iobuf + 14);
      size_t l = strlen(tptr);
      if (l >= sizeof(if_none_match_buf))
        l = sizeof(if_none_match_buf) - 1;
      memcpy(if_none_match_buf, tptr, l);
      if_none_match_buf[l] = '\0';
      if_none_match = if_none_match_buf;
      continue;
    }

    if (cgi_type != CGI_NONE) {
      /* header name to upper case, non alnum to _, HTTP_ prefix */
      int ct = strncasecmp(iobuf, "Content-Type:", 13) == 0;
      char *colon = strchr(iobuf, ':');
      if (!colon)
        continue;
      char *cp = iobuf;
      while (cp < colon) {
        char c = (char)(*cp & ~0x20);
        if ((unsigned)(c - 'A') <= ('Z' - 'A')) {
          *cp++ = c;
          continue;
        }
        if (!is_digit((unsigned char)*cp))
          *cp = '_';
        cp++;
      }
      env_add_raw(!ct, iobuf, (size_t)(colon - iobuf), skip_ws(colon + 1));
      *colon = ':';
    }
  }

  sysret(k_alarm(0));

  if (strcmp(basename_of(urlcopy), HTTPD_CONF_NAME) == 0)
    send_headers_and_exit(HTTP_FORBIDDEN);

  if (authorized < 0)
    authorized = check_user_passwd(urlcopy, (char *)"");
  if (!authorized)
    send_headers_and_exit(HTTP_UNAUTHORIZED);

  if (found_moved)
    send_headers_and_exit(HTTP_MOVED_TEMPORARILY);

  if (cgi_type != CGI_NONE) {
    send_cgi_and_exit(
        (cgi_type == CGI_INDEX) ? "/cgi-bin/index.cgi" : urlcopy, urlcopy,
        prequest, (int)POST_length);
  }

  if (method_id != 0 && method_id != 1) {
    /* POST and friends for plain files make no sense */
    send_headers_and_exit(HTTP_NOT_IMPLEMENTED);
  }

  /* restore the truncated .../index.html */
  if (urlp[-1] == '/')
    urlp[0] = index_page[0];
  send_file_and_exit(urlcopy + 1,
                     (method_id != 1) ? (SEND_HEADERS | SEND_BODY)
                                      : SEND_HEADERS);
}

/* server
 */

static int open_server(const char *bind_addr_or_port) {
  sockaddr_in_t addr;
  unsigned char *ip;
  unsigned char *port;
  long p;

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  ip = (unsigned char *)&addr.sin_addr;
  port = (unsigned char *)&addr.sin_port;

  const char *end;
  long v = parse_ll(bind_addr_or_port, &end);
  if (*end == '\0' && v > 0 && v <= 0xffff) {
    /* a plain port number, bind the wildcard */
    port[0] = (unsigned char)(v >> 8);
    port[1] = (unsigned char)(v & 0xff);
  } else {
    /* [IP:]PORT, a bare address means port 80 */
    char hostbuf[PATH_BUF];
    const char *host = bind_addr_or_port;
    const char *sep = strrchr(bind_addr_or_port, ':');
    p = 80;
    if (sep) {
      size_t hl = (size_t)(sep - bind_addr_or_port);
      if (hl >= sizeof(hostbuf))
        hl = sizeof(hostbuf) - 1;
      memcpy(hostbuf, bind_addr_or_port, hl);
      hostbuf[hl] = '\0';
      host = hostbuf;
      p = parse_ll(sep + 1, &end);
      if (p < 1 || p > 0xffff || *end) {
        char m[512];
        ksnprintf(m, sizeof(m), "bad port '%s'", bind_addr_or_port);
        die(m);
      }
    }
    unsigned char ipb[4];
    if (resolve_host(host, ipb) < 0) {
      char m[512];
      ksnprintf(m, sizeof(m), "bad address '%s'", bind_addr_or_port);
      die(m);
    }
    ip[0] = ipb[0];
    ip[1] = ipb[1];
    ip[2] = ipb[2];
    ip[3] = ipb[3];
    port[0] = (unsigned char)(p >> 8);
    port[1] = (unsigned char)(p & 0xff);
  }

  int sock = (int)sysret(k_socket(AF_INET, SOCK_STREAM, 0));
  if (sock < 0)
    die_errno("socket");
  int yes = 1;
  if (sysret(k_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes))) <
      0)
    die_errno("setsockopt");
  if (sysret(k_bind(sock, &addr, sizeof(addr))) < 0)
    die_errno("bind");
  if (sysret(k_listen(sock, 9)) < 0)
    die_errno("listen");
  return sock;
}

static void serve(int sock) {
  while (1) {
    sockaddr_in_t from;
    socklen_t len = sizeof(from);
    memset(&from, 0, sizeof(from));
    int n = (int)sysret(k_accept(sock, &from, &len));
    if (n < 0)
      continue;
    int yes = 1;
    sysret(k_setsockopt(n, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes)));
    if (sysret(k_fork()) == 0) {
      /* child, do not reload config on HUP */
      set_signal(SIG_HUP, SIG_IGN_PTR);
      sysret(k_close(sock));
      if (n != 0) {
        if (sysret(k_dup2(n, 0)) < 0)
          exit_now(0);
        sysret(k_close(n));
      }
      sysret(k_dup2(0, 1));
      handle_incoming_and_exit(&from);
    }
    sysret(k_close(n));
  }
}

static void serve_inetd(void) {
  sockaddr_in_t from;
  socklen_t len = sizeof(from);
  memset(&from, 0, sizeof(from));
  /* may fail if httpd -i is run by hand from a terminal */
  sysret(k_getpeername(0, &from, &len));
  handle_incoming_and_exit(&from);
}

/* main
 */

static void usage(const char *self) {
  char msg[1024];
  ksnprintf(msg, sizeof(msg),
            "Usage: %s [-ibv[v]] [-c CONFFILE] [-p [IP:]PORT] "
            "[-u USER[:GRP]] [-r REALM] [-h HOME]\n"
            "or %s -d/-e/-m STRING\n"
            "\n"
            "Listen for incoming HTTP requests\n"
            "\n"
            "\t-i\t\tInetd mode\n"
            "\t-b\t\tRun in background\n"
            "\t-v[v]\t\tVerbose\n"
            "\t-p [IP:]PORT\tBind to IP:PORT (default *:80)\n"
            "\t-u USER[:GRP]\tSet uid/gid after binding to port\n"
            "\t-r REALM\tAuthentication Realm for Basic Authentication\n"
            "\t-h HOME\t\tHome directory (default .)\n"
            "\t-c FILE\t\tConfiguration file (default {/etc,HOME}/httpd.conf)\n"
            "\t-m STRING\tMD5 crypt STRING\n"
            "\t-e STRING\tHTML encode STRING\n"
            "\t-d STRING\tURL decode STRING\n",
            self, self);
  full_write(2, msg, strlen(msg));
  exit_now(1);
}

static void make_salt(char *salt) {
  unsigned char rnd[6];
  fill_random(rnd, sizeof(rnd));
  salt[0] = '$';
  salt[1] = '1';
  salt[2] = '$';
  for (int i = 0; i < 8; i++) {
    unsigned v = (unsigned)rnd[i % 6] + (unsigned)i;
    salt[3 + i] = b64t[v % 64];
  }
  salt[11] = '\0';
}

/* resolve USER or USER:GRP like busybox get_uidgid, always sets uid and gid */
static int parse_ugid(const char *s, unsigned *uid, unsigned *gid) {
  char buf[256];
  size_t l = strlen(s);
  if (l >= sizeof(buf))
    return -1;
  memcpy(buf, s, l + 1);
  char *colon = strchr(buf, ':');
  char *user = buf;
  char *group = NULL;
  if (colon) {
    *colon = '\0';
    group = colon + 1;
  }
  const char *end;
  long v = parse_ll(user, &end);
  if (*user && *end == '\0' && v >= 0) {
    /* numeric uid, gid from the passwd entry or equal to the uid */
    struct pw_entry pw;
    *uid = (unsigned)v;
    if (getpwuid((unsigned)v, &pw) == 0)
      *gid = pw.gid;
    else
      *gid = (unsigned)v;
  } else {
    struct pw_entry pw;
    if (getpwnam(user, &pw) < 0)
      return -1;
    *uid = pw.uid;
    *gid = pw.gid;
  }
  if (group) {
    long g = parse_ll(group, &end);
    if (*end == '\0' && g >= 0) {
      *gid = (unsigned)g;
    } else {
      unsigned gr;
      if (getgrnam(group, &gr) < 0)
        return -1;
      *gid = gr;
    }
  }
  return 0;
}

int main(int argc, char **argv, char **envp) {
  int server_socket = 0;
  const char *url_for_decode = NULL;
  const char *url_for_encode = NULL;
  const char *pass = NULL;
  const char *s_ugid = NULL;
  unsigned opt_uid = 0, opt_gid = 0;
  int opt_inetd = 0, opt_background = 0;
  const char *bind_addr_or_port = "80";

  environ = envp;
  (void)argc;

  if (sysret(k_getcwd(home_httpd, sizeof(home_httpd))) < 0)
    strcpy(home_httpd, ".");

  /* getopt32 style parsing, -v counts */
  int i = 1;
  while (i < argc) {
    const char *a = argv[i];
    if (a[0] != '-' || a[1] == '\0')
      break;
    if (a[1] == '-' && a[2] == '\0')
      break;
    i++;
    int j = 1;
    while (a[j]) {
      char c = a[j++];
      const char *val = NULL;
      if (c == 'c' || c == 'd' || c == 'h' || c == 'e' || c == 'r' ||
          c == 'm' || c == 'u' || c == 'p') {
        if (a[j]) {
          val = a + j;
          j += (int)strlen(a + j);
        } else {
          if (i >= argc) {
            char m[64];
            ksnprintf(m, sizeof(m), "option requires an argument: -%c", c);
            die(m);
          }
          val = argv[i++];
        }
      }
      switch (c) {
      case 'c':
        opt_c_config = val;
        break;
      case 'd':
        url_for_decode = val;
        break;
      case 'h':
        if (strlen(val) >= sizeof(home_httpd))
          die("home path too long");
        strcpy(home_httpd, val);
        break;
      case 'e':
        url_for_encode = val;
        break;
      case 'r':
        g_realm = val;
        break;
      case 'm':
        pass = val;
        break;
      case 'u':
        s_ugid = val;
        break;
      case 'p':
        bind_addr_or_port = val;
        break;
      case 'i':
        opt_inetd = 1;
        break;
      case 'b':
        opt_background = 1;
        break;
      case 'v':
        verbose++;
        break;
      default: {
        char m[64];
        ksnprintf(m, sizeof(m), "invalid option: -%c", c);
        log_msg("%s", m);
        usage(argv[0]);
      }
      }
    }
  }

  if (url_for_decode) {
    char out[IOBUF_SIZE];
    size_t l = strlen(url_for_decode);
    if (l >= sizeof(out))
      l = sizeof(out) - 1;
    memcpy(out, url_for_decode, l);
    out[l] = '\0';
    percent_decode_in_place(out, 0);
    full_write(1, out, strlen(out));
    exit_now(0);
  }
  if (url_for_encode) {
    html_encode_write(url_for_encode);
    exit_now(0);
  }
  if (pass) {
    char salt[16];
    make_salt(salt);
    const char *r = pw_encrypt(pass, salt);
    full_write(1, r, strlen(r));
    full_write(1, "\n", 1);
    exit_now(0);
  }
  if (s_ugid) {
    if (parse_ugid(s_ugid, &opt_uid, &opt_gid) < 0) {
      char m[128];
      ksnprintf(m, sizeof(m), "unknown user/group %s", s_ugid);
      die(m);
    }
  }

  if (sysret(k_chdir(home_httpd)) < 0)
    die_errno(home_httpd);

  if (!opt_inetd) {
    set_signal(SIG_CHLD, SIG_IGN_PTR);
    server_socket = open_server(bind_addr_or_port);
    if (s_ugid) {
      /* gid is always set by parse_ugid */
      if (sysret(k_setgroups(1, &opt_gid)) < 0)
        die_errno("setgroups");
      if (sysret(k_setgid(opt_gid)) < 0)
        die_errno("setgid");
      if (sysret(k_setuid(opt_uid)) < 0)
        die_errno("setuid");
    }
  }

  parse_conf(DEFAULT_PATH_HTTPD_CONF, FIRST_PARSE);
  if (!opt_inetd)
    set_signal(SIG_HUP, sighup_handler);

  if (opt_inetd) {
    serve_inetd();
  } else {
    if (opt_background) {
      /* fork and detach, keep stdio and cwd as they are */
      long pid = sysret(k_fork());
      if (pid < 0)
        die("fork");
      if (pid > 0)
        exit_now(0);
      sysret(k_setsid());
    }
    serve(server_socket);
  }
  return 0;
}
