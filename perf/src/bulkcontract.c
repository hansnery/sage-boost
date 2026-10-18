#include "bulkcontract.h"
#include "bulkcontract_table.h"
#include <string.h>

const char *pf_bulk_find(const pf_image *image, pf_bulk_sites *sites)
{
    unsigned locations[BK_SYMBOL_COUNT];
    if (sites) memset(sites, 0, sizeof *sites);
    const char *why = pf_contract_find(image, &bk_contract, locations);
    if (why) return why;
    /* AotR strips OriginalFirstThunk: the loader overwrites its only thunk
     * table. Compare each discovered IAT slot with the already-loaded CRT
     * export instead of depending on import-name RVAs surviving startup. */
    HMODULE crt = GetModuleHandleW(L"msvcr71.dll");
    if (!crt) return "native CRT module not loaded";
    for (unsigned i = 0; i < sizeof bk_imports / sizeof bk_imports[0]; i++) {
        FARPROC expected = GetProcAddress(crt, bk_imports[i].name);
        void *actual;
        memcpy(&actual, image->base + locations[bk_imports[i].symbol], sizeof actual);
        if (!expected || actual != (void *)expected) return "native CRT import identity differs";
    }
    if (sites) {
#define ADDRESS(key) (image->base + locations[BK_##key])
        sites->group = ADDRESS(GROUP);
        /* Instruction boundaries and all callers/branches are covered by
         * the complete native group fingerprint, not standalone offsets. */
        sites->hook = sites->group + 0x14f;
        sites->fallback = sites->group + 0x155;
        sites->resume = sites->group + 0x2b7;
        sites->mesh = ADDRESS(MESH); sites->triangles = ADDRESS(TRIANGLES); sites->draw = ADDRESS(DRAW);
        sites->begin_callback = (void *)ADDRESS(BEGIN_CALLBACK);
        sites->end_callback = (void *)ADDRESS(END_CALLBACK);
        sites->fx_get_effect = ADDRESS(FX_GET_EFFECT); sites->fx_begin = ADDRESS(FX_BEGIN);
        sites->fx_begin_pass = ADDRESS(FX_BEGIN_PASS); sites->fx_dynamic = ADDRESS(FX_DYNAMIC);
        sites->fx_end_pass = ADDRESS(FX_END_PASS); sites->fx_end = ADDRESS(FX_END);
        sites->fx_bind = ADDRESS(FX_BIND); sites->fx_apply_sources = ADDRESS(FX_APPLY_SOURCES);
#undef ADDRESS
    }
    return NULL;
}
