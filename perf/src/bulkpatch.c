#include "bulk.h"
#include "bulkcontract_table.h"
#include "patch.h"
#include "instbulk.h"

static void bulk_next(unsigned index, void *original)
{
    if (!index) {
        extern void *pf_bulk_original;
        pf_bulk_original = original;
    }
}

int pf_bulk_install(const pf_image *image, const pf_bulk_sites *sites, const char **why)
{
    const pf_site hooks[] = {
        {"bulk-group", "FX group lifetime", BK_ROOT_0_SIGNATURE, 0, 5, 8, 1, 0, 0, 0},
        {"bulk-loop", "FX group records", BK_LOOP_SIGNATURE, 0, 6, 0, 1, 0, 0, 0}
    };
    const int enabled[] = {1, 1};
    void *targets[] = {pf_bulk_entry, pf_bulk_bridge};
    const unsigned char *expected[] = {sites->group, sites->hook};
    pf_result results[2];
    /* Startup found the entire native contract before installing mesh/event
     * hooks. Recheck both patch sites and their joined identities now. */
    for (unsigned i = 0; i < 2; i++) {
        pf_pattern pattern;
        unsigned rva;
        if (!pf_parse_pattern(hooks[i].signature, &pattern) ||
            pf_scan(image, &pattern, &rva) != 1 || image->base + rva != expected[i]) {
            if (why) *why = "group patch signature/identity changed";
            return 0;
        }
    }
    if (!pf_bulk_bind(sites->group, sites->resume, sites->fallback)) {
        if (why) *why = "group bridge already active";
        return 0;
    }
    pf_install(image, hooks, 2, enabled, 1, targets, bulk_next, results);
    for (unsigned i = 0; i < 2; i++) {
        if (results[i].status != PF_ST_APPLIED) {
            if (why) *why = pf_status_name(results[i].status);
            return 0;
        }
    }
    pf_inst_bulk_sites inst = {sites->mesh, sites->triangles, sites->draw,
                              sites->begin_callback, sites->end_callback};
    if (!pf_inst_bulk_configure(&inst, 1) || !pf_bulk_enable(1)) {
        if (why) *why = "instancer has pending or failed work";
        return 0;
    }
    if (why) *why = "guarded native group batching enabled";
    return 1;
}
