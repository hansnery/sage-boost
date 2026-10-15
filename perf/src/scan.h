/* Signature scanner over a loaded PE image's code section. CRT-free. */
#ifndef PF_SCAN_H
#define PF_SCAN_H

#include <windows.h>

#define PF_MAX_PATTERN 256

typedef struct {
    unsigned len;
    unsigned char bytes[PF_MAX_PATTERN];
    unsigned char fixed[PF_MAX_PATTERN]; /* 1 = must match, 0 = wildcard */
} pf_pattern;

typedef struct {
    unsigned char *base;
    unsigned text_rva;
    unsigned text_size;
    unsigned timestamp;
} pf_image;

/* "8B ?? 0F" -> pattern. Returns 0 on malformed input. */
int pf_parse_pattern(const char *s, pf_pattern *p);
int pf_pattern_at(const unsigned char *at, const pf_pattern *p);
/* Matches of p in the code section; *rva gets the first one's start. */
unsigned pf_scan(const pf_image *img, const pf_pattern *p, unsigned *rva);
/* Fills img from an in-memory PE image's executable ".text" section.
 * Returns 0 if the headers are not usable. */
int pf_image_open(pf_image *img, void *module_base);
unsigned pf_crc32(const unsigned char *p, unsigned n);

#endif
