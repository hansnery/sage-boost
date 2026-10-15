#include "log.h"

#include <stdarg.h>

#define LINE_MAX 1024

static HANDLE g_log = INVALID_HANDLE_VALUE;

typedef struct {
    char buf[LINE_MAX];
    unsigned len;
} line;

static void put_c(line *l, char c)
{
    if (l->len < LINE_MAX - 2) /* room for "\r\n" */
        l->buf[l->len++] = c;
}

static void put_s(line *l, const char *s)
{
    if (!s)
        s = "(null)";
    while (*s)
        put_c(l, *s++);
}

static void put_ws(line *l, const WCHAR *ws)
{
    char tmp[LINE_MAX];
    int n;
    if (!ws) {
        put_s(l, "(null)");
        return;
    }
    n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, tmp, sizeof tmp, NULL, NULL);
    if (n > 0)
        put_s(l, tmp);
}

static void put_num(line *l, unsigned v, unsigned radix, int upper, int negative,
                    unsigned width, char pad)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[16];
    unsigned n = 0;
    do {
        tmp[n++] = digits[v % radix];
        v /= radix;
    } while (v);
    if (negative)
        width = width ? width - 1 : 0;
    if (negative && pad == '0')
        put_c(l, '-');
    while (width > n) {
        put_c(l, pad);
        width--;
    }
    if (negative && pad != '0')
        put_c(l, '-');
    while (n)
        put_c(l, tmp[--n]);
}

void fu_log_open(const WCHAR *path)
{
    fu_log_close();
    g_log = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
}

void fu_log_append(const WCHAR *path)
{
    fu_log_close();
    g_log = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
}

void fu_log_close(void)
{
    if (g_log != INVALID_HANDLE_VALUE)
        CloseHandle(g_log);
    g_log = INVALID_HANDLE_VALUE;
}

static void vformat(line *l, const char *fmt, va_list ap);

static WCHAR g_event_path[MAX_PATH];

void fu_log_event_path(const WCHAR *path)
{
    unsigned i = 0;
    if (path)
        for (; path[i] && i < MAX_PATH - 1; i++)
            g_event_path[i] = path[i];
    g_event_path[i] = 0;
}

void fu_log_event(const char *fmt, ...)
{
    line l;
    va_list ap;
    DWORD written;
    HANDLE h;

    if (!g_event_path[0])
        return;
    h = CreateFileW(g_event_path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    va_start(ap, fmt);
    vformat(&l, fmt, ap);
    va_end(ap);
    WriteFile(h, l.buf, l.len, &written, NULL);
    CloseHandle(h);
}

void fu_logf(const char *fmt, ...)
{
    line l;
    va_list ap;
    DWORD written;

    if (g_log == INVALID_HANDLE_VALUE)
        return;
    va_start(ap, fmt);
    vformat(&l, fmt, ap);
    va_end(ap);
    WriteFile(g_log, l.buf, l.len, &written, NULL);
}

/* One formatted line, "\r\n"-terminated, into l. */
static void vformat(line *l, const char *fmt, va_list ap)
{
    l->len = 0;
    while (*fmt) {
        char pad = ' ';
        unsigned width = 0;
        if (*fmt != '%') {
            put_c(l, *fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (unsigned)(*fmt++ - '0');
        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            unsigned n = 0;
            while (s && s[n])
                n++;
            put_s(l, s);
            while (width > n) { /* left-justified: pad on the right */
                put_c(l, ' ');
                width--;
            }
            break;
        }
        case 'l':
            if (fmt[1] == 's') {
                fmt++;
                put_ws(l, va_arg(ap, const WCHAR *));
            }
            break;
        case 'c':
            put_c(l, (char)va_arg(ap, int));
            break;
        case 'd': {
            int v = va_arg(ap, int);
            put_num(l, v < 0 ? 0u - (unsigned)v : (unsigned)v, 10, 0, v < 0, width, pad);
            break;
        }
        case 'u':
            put_num(l, va_arg(ap, unsigned), 10, 0, 0, width, pad);
            break;
        case 'x':
        case 'X':
            put_num(l, va_arg(ap, unsigned), 16, *fmt == 'X', 0, width, pad);
            break;
        case '%':
            put_c(l, '%');
            break;
        default:
            if (!*fmt)
                fmt--;
            break;
        }
        fmt++;
    }
    l->buf[l->len++] = '\r';
    l->buf[l->len++] = '\n';
}
