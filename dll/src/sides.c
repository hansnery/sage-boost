#include "sides.h"

#include "log.h"

/* BFME2 AsciiString: one pointer to { int ref_count; u16 length; u16 capacity;
 * char data[] } (StringBase<char>::Header). The game copies strings by bumping
 * ref_count and frees the buffer when it drops to zero, so a donor buffer
 * starts with a count no amount of copying can bring back to zero. */
typedef struct {
    volatile LONG ref_count;
    unsigned short length;
    unsigned short capacity;
    char data[FU_SIDE_NAME_MAX + 1];
} ascii_buf;

typedef struct {
    ascii_buf *m_data;
} ascii_string;

typedef struct {
    char new_side[FU_SIDE_NAME_MAX + 1];
    unsigned new_len;
    ascii_buf donor_buf;
    ascii_string donor;
} side_alias;

#define PERMANENT_REF 0x40000000

static side_alias g_aliases[FU_MAX_SIDES];
static unsigned g_count;

/* The game's own Sides (read from its name table) and names that are Sides
 * in every game without being in the table. */
static ascii_buf g_native_buf[FU_MAX_NATIVE];
static ascii_string g_native[FU_MAX_NATIVE];
static unsigned g_native_count;
static const char *const k_reserved[] = { "Neutral", "Civilian", "Observer" };

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

static int eq_nocase(const char *a, unsigned alen, const char *b)
{
    unsigned i;
    for (i = 0; i < alen; i++)
        if (!b[i] || lower(a[i]) != lower(b[i]))
            return 0;
    return b[alen] == 0;
}

static unsigned length_of(const char *s)
{
    unsigned n = 0;
    while (s[n])
        n++;
    return n;
}

int fu_sides_is_reserved(const char *name)
{
    unsigned i, n = length_of(name);
    for (i = 0; i < sizeof k_reserved / sizeof k_reserved[0]; i++)
        if (eq_nocase(name, n, k_reserved[i]))
            return 1;
    return 0;
}

int fu_sides_is_native(const char *name)
{
    unsigned i, n = length_of(name);
    for (i = 0; i < g_native_count; i++)
        if (eq_nocase(name, n, g_native_buf[i].data))
            return 1;
    return fu_sides_is_reserved(name);
}

void fu_sides_set_native(const char *const *names, unsigned n)
{
    unsigned i, k;
    g_native_count = 0;
    for (i = 0; i < n && g_native_count < FU_MAX_NATIVE; i++) {
        ascii_buf *b = &g_native_buf[g_native_count];
        for (k = 0; k < FU_SIDE_NAME_MAX && names[i][k]; k++)
            b->data[k] = names[i][k];
        b->data[k] = 0;
        b->ref_count = PERMANENT_REF;
        b->length = (unsigned short)k;
        b->capacity = (unsigned short)(k + 1);
        g_native[g_native_count++].m_data = b;
    }
}

unsigned fu_sides_native_count(void)
{
    return g_native_count;
}

const char *fu_sides_native_name(unsigned i)
{
    return i < g_native_count ? g_native_buf[i].data : "";
}

const void *fu_sides_native_string(unsigned i)
{
    return i < g_native_count ? &g_native[i] : NULL;
}

const char *fu_ascii_cstr(const void *str)
{
    const ascii_buf *buf;
    if (!str || !(buf = ((const ascii_string *)str)->m_data) || !buf->length)
        return "";
    return buf->data;
}

static unsigned le32(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

/* A printable ASCII, NUL-terminated name inside the image, or NULL. */
static const char *image_name(const fu_image *img, unsigned va)
{
    unsigned i;
    for (i = 0; i <= FU_SIDE_NAME_MAX; i++) {
        const unsigned char *c = fu_image_va(img, va + i);
        if (!c)
            return NULL;
        if (*c == 0)
            return i ? (const char *)fu_image_va(img, va) : NULL;
        if (*c < 0x21 || *c > 0x7E)
            return NULL;
    }
    return NULL;
}

unsigned fu_sides_read_native(const fu_image *img, const fu_game *game)
{
    const char *names[FU_MAX_NATIVE];
    const unsigned char *sig, *slot;
    unsigned sig_rva, table, count, i;

    g_native_count = 0;
    if (fu_engine_find(img, game->side_patch, &sig_rva) != 1)
        return 0;
    sig = img->base + sig_rva;
    table = le32(sig + game->side_table_off);
    count = sig[game->side_count_off];
    if (count == 0 || count >= FU_MAX_NATIVE)
        return 0;
    for (i = 0; i < count; i++) {
        if (!(slot = fu_image_va(img, table + 4 * i)) || !fu_image_va(img, table + 4 * i + 3) ||
            !(names[i] = image_name(img, le32(slot))))
            return 0;
    }
    if (!(names[count] = image_name(img, le32(sig + game->side_special_off))))
        return 0;
    fu_sides_set_native(names, count + 1);
    return g_native_count;
}

static int valid_name(const char *s)
{
    unsigned i;
    for (i = 0; s[i]; i++) {
        char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_'))
            return 0;
    }
    return i > 0 && i <= FU_SIDE_NAME_MAX;
}

void fu_sides_clear(void)
{
    g_count = 0;
}

const char *fu_sides_add(const char *new_side, const char *donor)
{
    side_alias *a;
    unsigned i, n;

    if (!valid_name(new_side) || !valid_name(donor))
        return "names must be 1-31 characters of A-Z, a-z, 0-9, _";
    if (fu_sides_is_native(new_side))
        return "already a Side in this game";
    n = length_of(new_side);
    if (eq_nocase(new_side, n, donor))
        return "a Side cannot be its own donor";
    for (i = 0; i < g_count; i++) {
        if (eq_nocase(g_aliases[i].new_side, g_aliases[i].new_len, new_side))
            return "listed twice";
        if (eq_nocase(g_aliases[i].new_side, g_aliases[i].new_len, donor))
            return "the donor is itself an alias (chains are not followed)";
    }
    if (g_count == FU_MAX_SIDES)
        return "too many entries (max 32)";

    a = &g_aliases[g_count];
    for (i = 0; i <= n; i++)
        a->new_side[i] = new_side[i];
    a->new_len = n;
    n = length_of(donor);
    a->donor_buf.ref_count = PERMANENT_REF;
    a->donor_buf.length = (unsigned short)n;
    a->donor_buf.capacity = (unsigned short)(n + 1);
    for (i = 0; i <= n; i++)
        a->donor_buf.data[i] = donor[i];
    a->donor.m_data = &a->donor_buf;
    g_count++;
    return NULL;
}

unsigned fu_sides_count(void)
{
    return g_count;
}

const char *fu_sides_new_name(unsigned i)
{
    return i < g_count ? g_aliases[i].new_side : "";
}

const char *fu_sides_donor_name(unsigned i)
{
    return i < g_count ? g_aliases[i].donor_buf.data : "";
}

const void *fu_sides_resolve(const void *str)
{
    const ascii_buf *buf;
    unsigned i;

    if (!str || !(buf = ((const ascii_string *)str)->m_data))
        return NULL;
    for (i = 0; i < g_count; i++) {
        const side_alias *a = &g_aliases[i];
        unsigned k;
        if (buf->length != a->new_len)
            continue;
        for (k = 0; k < a->new_len && lower(buf->data[k]) == lower(a->new_side[k]); k++)
            ;
        if (k == a->new_len)
            return &a->donor;
    }
    return NULL;
}

/* "  Key = Value ; comment" -> Key, Value (ASCII only). */
static int split_entry(const WCHAR *line, char *key, char *value)
{
    char *out = key;
    unsigned n = 0;
    int in_value = 0;

    for (;; line++) {
        WCHAR c = *line;
        if (c == L'=' && !in_value) {
            out[n] = 0;
            out = value;
            n = 0;
            in_value = 1;
            continue;
        }
        if (c == 0 || c == L';' || c == L'#') {
            out[n] = 0;
            break;
        }
        if (c > 0x7F)
            return 0;
        if (c == L' ' || c == L'\t') {
            if (n == 0)
                continue; /* leading */
        }
        if (n < FU_SIDE_NAME_MAX + 8)
            out[n++] = (char)c;
    }
    if (!in_value)
        return 0;
    /* trailing whitespace */
    for (n = length_of(key); n && (key[n - 1] == ' ' || key[n - 1] == '\t'); n--)
        key[n - 1] = 0;
    for (n = length_of(value); n && (value[n - 1] == ' ' || value[n - 1] == '\t'); n--)
        value[n - 1] = 0;
    return key[0] != 0;
}

unsigned fu_sides_load(const WCHAR *ini_path)
{
    static WCHAR section[4096];
    const WCHAR *line;
    DWORD got;

    fu_sides_clear();
    got = GetPrivateProfileSectionW(L"Sides", section, sizeof section / sizeof section[0],
                                    ini_path);
    for (line = section; got && *line; line += lstrlenW(line) + 1) {
        char key[FU_SIDE_NAME_MAX + 9], value[FU_SIDE_NAME_MAX + 9];
        const char *why;
        if (*line == L';' || *line == L'#')
            continue;
        if (!split_entry(line, key, value)) {
            fu_logf("sides: ignored \"%ls\" (expected NewSide=DonorSide, ASCII only)", line);
            continue;
        }
        why = fu_sides_add(key, value);
        if (why) {
            fu_logf("sides: rejected %s=%s: %s", key, value, why);
            continue;
        }
        fu_logf("sides: %s -> %s%s", key, value,
                fu_sides_is_native(value) ? "" : " (WARNING: the donor is not one of the game's Sides)");
    }
    return g_count;
}
