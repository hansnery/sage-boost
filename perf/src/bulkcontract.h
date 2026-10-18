#ifndef PF_BULKCONTRACT_H
#define PF_BULKCONTRACT_H
#include "scan.h"

typedef struct {
    unsigned char *group, *hook, *resume, *fallback;
    unsigned char *mesh, *triangles, *draw;
    void *volatile *begin_callback, *volatile *end_callback;
    void *fx_get_effect, *fx_begin, *fx_begin_pass, *fx_dynamic;
    void *fx_end_pass, *fx_end, *fx_bind, *fx_apply_sources;
} pf_bulk_sites;

/* Run before installing any mesh, DIP, effect or event hooks. Discovery
 * only: this never writes the image or enables the unfinished bulk engine. */
const char *pf_bulk_find(const pf_image *image, pf_bulk_sites *sites);
#endif
