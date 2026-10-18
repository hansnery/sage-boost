#ifndef PF_BULK_H
#define PF_BULK_H
#include "bulkcontract.h"

/* Bind verified native continuations while disabled and outside a group.
 * The fixture uses the same bridge with privately relocated native bodies. */
int pf_bulk_bind(void *original, void *resume, void *fallback);
int pf_bulk_enable(int on);
unsigned pf_bulk_consumed(void);
int pf_bulk_install(const pf_image *image, const pf_bulk_sites *sites, const char **why);
void __attribute__((thiscall)) pf_bulk_entry(void *renderer, const void *first, const void *end);
void pf_bulk_bridge(void);
int pf_bulk_dispatch(unsigned *registers);
#endif
