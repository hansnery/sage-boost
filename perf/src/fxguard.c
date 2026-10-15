#include "fxguard.h"

#include "shadowfx_table.h"

#define MAX_DEPTH 4
#define MAX_DIRECTORY (8u << 20) /* a .big's file table, at most */
#define PREFIX "shaders\\compiled\\"

typedef struct {
    pf_fxguard_result *r;
    unsigned found[PF_SHADOWFX_COUNT];
    int bad;
} scan_state;

static unsigned be32(const unsigned char *p)
{
    return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3];
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c == '/' ? '\\' : c;
}

static unsigned slen(const char *s)
{
    unsigned n = 0;
    while (s[n])
        n++;
    return n;
}

/* The part of an archive path after "shaders\compiled\" when it ends in
 * ".fxo" (case-insensitive, / or \), else NULL. */
static const char *effect_name(const char *path, unsigned len)
{
    unsigned p = slen(PREFIX), i;
    if (len <= p + 4)
        return 0;
    for (i = 0; i < p; i++)
        if (lower(path[i]) != PREFIX[i])
            return 0;
    if (lower(path[len - 4]) != '.' || lower(path[len - 3]) != 'f' || lower(path[len - 2]) != 'x' ||
        lower(path[len - 1]) != 'o')
        return 0;
    for (i = p; i < len; i++)
        if (path[i] == '\\' || path[i] == '/')
            return 0;
    return path + p;
}

static int same_name(const char *a, unsigned alen, const char *b)
{
    unsigned i;
    for (i = 0; i < alen && b[i]; i++)
        if (lower(a[i]) != lower(b[i]))
            return 0;
    return i == alen && !b[i];
}

static void note(scan_state *st, const char *what, const char *name, unsigned len)
{
    char *w = st->r->why;
    unsigned i = 0, k;
    if (st->bad)
        return;
    st->bad = 1;
    for (k = 0; what[k] && i < sizeof st->r->why - 1; k++)
        w[i++] = what[k];
    for (k = 0; k < len && i < sizeof st->r->why - 1; k++)
        w[i++] = name[k];
    w[i] = 0;
}

static unsigned fnv1a_update(unsigned h, const unsigned char *p, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 0x01000193u;
    return h;
}

/* Hashes size bytes at offset of file h. */
static int hash_range(HANDLE h, unsigned offset, unsigned size, unsigned *fnv)
{
    unsigned char buf[16384];
    unsigned left = size, hash = 0x811C9DC5u;
    if (SetFilePointer(h, (LONG)offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
        return 0;
    while (left) {
        DWORD want = left < sizeof buf ? left : sizeof buf, got = 0;
        if (!ReadFile(h, buf, want, &got, NULL) || got != want)
            return 0;
        hash = fnv1a_update(hash, buf, got);
        left -= got;
    }
    *fnv = hash;
    return 1;
}

static void effect(scan_state *st, const char *name, unsigned len, HANDLE h, unsigned offset,
                   unsigned size)
{
    unsigned fnv = 0, i;
    st->r->effects++;
    if (!hash_range(h, offset, size, &fnv)) {
        note(st, "can't read ", name, len);
        return;
    }
    for (i = 0; i < PF_SHADOWFX_COUNT; i++) {
        if (same_name(name, len, pf_shadowfx_table[i].name) && pf_shadowfx_table[i].size == size &&
            pf_shadowfx_table[i].fnv == fnv) {
            st->found[i]++;
            st->r->proven++;
            return;
        }
    }
    note(st, "an effect not proven for the merge: ", name, len);
}

static void scan_big(scan_state *st, const WCHAR *path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    unsigned char head[16], *dir;
    unsigned count, dir_size, i, pos;
    DWORD got = 0;

    if (h == INVALID_HANDLE_VALUE)
        return;
    if (!ReadFile(h, head, 16, &got, NULL) || got != 16 ||
        !(head[0] == 'B' && head[1] == 'I' && head[2] == 'G' && (head[3] == 'F' || head[3] == '4'))) {
        CloseHandle(h);
        return;
    }
    count = be32(head + 8);
    dir_size = be32(head + 12);
    if (dir_size < 16 || dir_size > MAX_DIRECTORY) {
        CloseHandle(h);
        return;
    }
    dir = HeapAlloc(GetProcessHeap(), 0, dir_size);
    if (!dir) {
        CloseHandle(h);
        return;
    }
    st->r->archives++;
    if (ReadFile(h, dir, dir_size - 16, &got, NULL) && got == dir_size - 16) {
        for (i = 0, pos = 0; i < count && pos + 9 <= got; i++) {
            unsigned off = be32(dir + pos), size = be32(dir + pos + 4), len = 0;
            const char *name = (const char *)dir + pos + 8, *fx;
            while (pos + 8 + len < got && name[len])
                len++;
            if (pos + 8 + len >= got)
                break;
            fx = effect_name(name, len);
            if (fx)
                effect(st, fx, len - (unsigned)(fx - name), h, off, size);
            pos += 8 + len + 1;
        }
    }
    HeapFree(GetProcessHeap(), 0, dir);
    CloseHandle(h);
}

static unsigned wlen(const WCHAR *s)
{
    unsigned n = 0;
    while (s[n])
        n++;
    return n;
}

static int join(WCHAR *out, unsigned cap, const WCHAR *dir, const WCHAR *name)
{
    unsigned a = wlen(dir), b = wlen(name), i;
    if (a + 1 + b + 1 > cap)
        return 0;
    for (i = 0; i < a; i++)
        out[i] = dir[i];
    if (a && out[a - 1] != L'\\' && out[a - 1] != L'/')
        out[a++] = L'\\';
    for (i = 0; i <= b; i++)
        out[a + i] = name[i];
    return 1;
}

static int is_big(const WCHAR *name)
{
    unsigned n = wlen(name);
    return n > 4 && name[n - 4] == L'.' && (name[n - 3] | 0x20) == L'b' &&
           (name[n - 2] | 0x20) == L'i' && (name[n - 1] | 0x20) == L'g';
}

/* Loose root\shaders\compiled\*.fxo files. */
static void scan_loose(scan_state *st, const WCHAR *root)
{
    WCHAR pattern[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE f;
    if (!join(pattern, MAX_PATH, root, L"shaders\\compiled\\*.fxo"))
        return;
    f = FindFirstFileW(pattern, &fd);
    if (f == INVALID_HANDLE_VALUE)
        return;
    do {
        char name[MAX_PATH];
        unsigned i;
        HANDLE h;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        for (i = 0; fd.cFileName[i] && i < MAX_PATH - 1; i++)
            name[i] = fd.cFileName[i] < 128 ? (char)fd.cFileName[i] : '?';
        name[i] = 0;
        if (!join(pattern, MAX_PATH, root, L"shaders\\compiled") ||
            !join(path, MAX_PATH, pattern, fd.cFileName))
            continue;
        h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        effect(st, name, i, h, 0, fd.nFileSizeLow);
        CloseHandle(h);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

static void scan_dir(scan_state *st, const WCHAR *dir, unsigned depth)
{
    WCHAR pattern[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE f;
    if (!join(pattern, MAX_PATH, dir, L"*"))
        return;
    f = FindFirstFileW(pattern, &fd);
    if (f == INVALID_HANDLE_VALUE)
        return;
    do {
        if (fd.cFileName[0] == L'.' &&
            (!fd.cFileName[1] || (fd.cFileName[1] == L'.' && !fd.cFileName[2])))
            continue;
        if (!join(path, MAX_PATH, dir, fd.cFileName))
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth < MAX_DEPTH)
                scan_dir(st, path, depth + 1);
        } else if (is_big(fd.cFileName)) {
            scan_big(st, path);
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

int pf_fxguard_mod_dir(const WCHAR *cmd, WCHAR *out, unsigned cap)
{
    unsigned i, n = 0;
    if (!cmd || !cap)
        return 0;
    for (i = 0; cmd[i]; i++) {
        if ((i == 0 || cmd[i - 1] == L' ' || cmd[i - 1] == L'\t') && cmd[i] == L'-' &&
            (cmd[i + 1] | 0x20) == L'm' && (cmd[i + 2] | 0x20) == L'o' &&
            (cmd[i + 3] | 0x20) == L'd' && (cmd[i + 4] == L' ' || cmd[i + 4] == L'\t')) {
            const WCHAR *p = cmd + i + 4;
            WCHAR end = L' ';
            while (*p == L' ' || *p == L'\t')
                p++;
            if (*p == L'"') {
                end = L'"';
                p++;
            }
            while (*p && *p != end && (end == L'"' || *p != L'\t') && n + 1 < cap)
                out[n++] = *p++;
            out[n] = 0;
            return n != 0;
        }
    }
    return 0;
}

int pf_fxguard_check(const WCHAR *game_dir, const WCHAR *command_line, pf_fxguard_result *r)
{
    WCHAR cwd[MAX_PATH], mod[MAX_PATH];
    scan_state st;
    unsigned i;

    r->archives = r->effects = r->proven = 0;
    r->why[0] = 0;
    for (i = 0; i < PF_SHADOWFX_COUNT; i++)
        st.found[i] = 0;
    st.r = r;
    st.bad = 0;
    if (!game_dir) {
        DWORD n = GetCurrentDirectoryW(MAX_PATH, cwd);
        if (!n || n >= MAX_PATH) {
            note(&st, "no working directory", "", 0);
            return 0;
        }
        game_dir = cwd;
    }
    scan_dir(&st, game_dir, 0);
    scan_loose(&st, game_dir);
    if (pf_fxguard_mod_dir(command_line, mod, MAX_PATH)) {
        scan_dir(&st, mod, 0);
        scan_loose(&st, mod);
    }
    for (i = 0; i < PF_SHADOWFX_COUNT && !st.bad; i++)
        if (pf_shadowfx_table[i].shadow && !st.found[i])
            note(&st, "not found: shaders\\compiled\\", pf_shadowfx_table[i].name,
                 slen(pf_shadowfx_table[i].name));
    return !st.bad;
}
