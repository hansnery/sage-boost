#include "scan.h"

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

int pf_parse_pattern(const char *s, pf_pattern *p)
{
    p->len = 0;
    for (;;) {
        while (*s == ' ')
            s++;
        if (!*s)
            return p->len > 0;
        if (p->len == PF_MAX_PATTERN)
            return 0;
        if (s[0] == '?' && s[1] == '?') {
            p->bytes[p->len] = 0;
            p->fixed[p->len] = 0;
        } else {
            int hi = hex_nibble(s[0]), lo = hi < 0 ? -1 : hex_nibble(s[1]);
            if (lo < 0)
                return 0;
            p->bytes[p->len] = (unsigned char)(hi << 4 | lo);
            p->fixed[p->len] = 1;
        }
        p->len++;
        s += 2;
        if (*s && *s != ' ')
            return 0;
    }
}

int pf_pattern_at(const unsigned char *at, const pf_pattern *p)
{
    unsigned i;
    for (i = 0; i < p->len; i++)
        if (p->fixed[i] && at[i] != p->bytes[i])
            return 0;
    return 1;
}

unsigned pf_scan(const pf_image *img, const pf_pattern *p, unsigned *rva)
{
    const unsigned char *start = img->base + img->text_rva;
    unsigned anchor = 0, count = 0, i;

    while (anchor < p->len && !p->fixed[anchor])
        anchor++;
    if (anchor == p->len || img->text_size < p->len)
        return 0;
    for (i = 0; i + p->len <= img->text_size; i++) {
        if (start[i + anchor] != p->bytes[anchor] || !pf_pattern_at(start + i, p))
            continue;
        if (count++ == 0)
            *rva = img->text_rva + i;
    }
    return count;
}

int pf_image_open(pf_image *img, void *module_base)
{
    unsigned char *base = (unsigned char *)module_base;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS32 *nt;
    IMAGE_SECTION_HEADER *sec;
    unsigned i;

    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;
    nt = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return 0;
    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        static const char text[8] = ".text";
        unsigned k;
        for (k = 0; k < 8 && sec->Name[k] == (unsigned char)text[k]; k++)
            ;
        if (k < 8 || !(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE))
            continue;
        img->base = base;
        img->text_rva = sec->VirtualAddress;
        img->text_size = sec->Misc.VirtualSize;
        img->timestamp = nt->FileHeader.TimeDateStamp;
        return img->text_size > 0;
    }
    return 0;
}

unsigned pf_crc32(const unsigned char *p, unsigned n)
{
    static unsigned table[256];
    unsigned c, i, k;

    if (!table[1]) {
        for (i = 0; i < 256; i++) {
            c = i;
            for (k = 0; k < 8; k++)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    c = 0xFFFFFFFFu;
    for (i = 0; i < n; i++)
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
