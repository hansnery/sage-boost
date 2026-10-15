#include "gameversion.h"

/* One node of the VS_VERSIONINFO tree: WORD wLength, wValueLength, wType,
 * WCHAR szKey[], DWORD-aligned Value, DWORD-aligned children. Alignment is
 * relative to the start of the resource. */
typedef struct {
    const WCHAR *key;
    const BYTE *value;
    unsigned value_bytes;
    const BYTE *children;
    const BYTE *end;
} vs_node;

static const BYTE *align4(const BYTE *res, const BYTE *p)
{
    return res + (((unsigned)(p - res) + 3u) & ~3u);
}

static int vs_read(const BYTE *res, const BYTE *p, const BYTE *limit, vs_node *n)
{
    WORD len, vlen, type;
    const WCHAR *k;

    if (p + 6 > limit)
        return 0;
    len = ((const WORD *)p)[0];
    vlen = ((const WORD *)p)[1];
    type = ((const WORD *)p)[2];
    if (len < 6 || p + len > limit)
        return 0;
    n->end = p + len;
    n->key = (const WCHAR *)(p + 6);
    for (k = n->key; (const BYTE *)(k + 1) <= n->end && *k; k++)
        ;
    if ((const BYTE *)(k + 1) > n->end)
        return 0;
    n->value = align4(res, (const BYTE *)(k + 1));
    n->value_bytes = type == 1 ? vlen * 2u : vlen;
    if (n->value > n->end)
        n->value = n->end;
    if (n->value + n->value_bytes > n->end)
        n->value_bytes = (unsigned)(n->end - n->value);
    n->children = align4(res, n->value + n->value_bytes);
    if (n->children > n->end)
        n->children = n->end;
    return 1;
}

static int key_is(const WCHAR *key, const char *ascii)
{
    while (*ascii && *key == (WCHAR)(unsigned char)*ascii) {
        key++;
        ascii++;
    }
    return *ascii == 0 && *key == 0;
}

static void read_internal_name(const BYTE *res, const vs_node *sfi, fu_game_version *out)
{
    const BYTE *t = sfi->children;
    vs_node table, str;

    while (t < sfi->end && vs_read(res, t, sfi->end, &table)) {
        const BYTE *s = table.children;
        while (s < table.end && vs_read(res, s, table.end, &str)) {
            if (key_is(str.key, "InternalName")) {
                const WCHAR *src = (const WCHAR *)str.value;
                unsigned i, max = str.value_bytes / 2;
                for (i = 0; i + 1 < 32 && i < max && src[i]; i++)
                    out->internal_name[i] = src[i];
                out->internal_name[i] = 0;
                /* Trailing spaces are common in hand-written version blocks. */
                while (i > 0 && out->internal_name[i - 1] == L' ')
                    out->internal_name[--i] = 0;
                return;
            }
            s = align4(res, str.end);
        }
        t = align4(res, table.end);
    }
}

void fu_game_version_read(HMODULE module, fu_game_version *out)
{
    HRSRC rsrc;
    HGLOBAL h;
    const BYTE *res, *limit, *c;
    vs_node root, child;
    DWORD size;

    out->has_fixed = 0;
    out->v[0] = out->v[1] = out->v[2] = out->v[3] = 0;
    out->internal_name[0] = 0;

    rsrc = FindResourceW(module, MAKEINTRESOURCEW(1), (LPCWSTR)RT_VERSION);
    if (!rsrc || !(h = LoadResource(module, rsrc)) || !(res = (const BYTE *)LockResource(h)))
        return;
    size = SizeofResource(module, rsrc);
    limit = res + size;
    if (!vs_read(res, res, limit, &root) || !key_is(root.key, "VS_VERSION_INFO"))
        return;

    if (root.value_bytes >= sizeof(VS_FIXEDFILEINFO)) {
        const VS_FIXEDFILEINFO *ffi = (const VS_FIXEDFILEINFO *)root.value;
        if (ffi->dwSignature == 0xFEEF04BDu) {
            out->has_fixed = 1;
            out->v[0] = HIWORD(ffi->dwFileVersionMS);
            out->v[1] = LOWORD(ffi->dwFileVersionMS);
            out->v[2] = HIWORD(ffi->dwFileVersionLS);
            out->v[3] = LOWORD(ffi->dwFileVersionLS);
        }
    }

    for (c = root.children; c < root.end && vs_read(res, c, root.end, &child);
         c = align4(res, child.end)) {
        if (key_is(child.key, "StringFileInfo")) {
            read_internal_name(res, &child, out);
            break;
        }
    }
}

const fu_game *fu_game_identify(const fu_game_version *v)
{
    unsigned i;
    if (!v->has_fixed || !key_is(v->internal_name, "BFME2"))
        return NULL;
    for (i = 0; i < fu_game_count; i++)
        if (v->v[0] == fu_games[i].ver_major && v->v[1] == fu_games[i].ver_minor)
            return &fu_games[i];
    return NULL;
}
