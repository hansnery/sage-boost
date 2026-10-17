#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "declcache.h"
#include "d3d9vt.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
typedef struct {
    void **vt;
    unsigned refs, reads, invalid;
} fake;
static void *obj_vt[5], *dev_vt[PF_DEV_SLOTS];
static fake originals[700], generated[1000], device;
static unsigned created, fail_create, live, max_live;
static ULONG __stdcall addref(void *obj) { return ++((fake *)obj)->refs; }
static ULONG __stdcall release(void *obj)
{
    fake *f = obj;
    CHECK(f->refs);
    if (--f->refs == 0) live--;
    return f->refs;
}
static HRESULT __stdcall get(void *obj, void *data, UINT *n)
{
    fake *f = obj;
    pf_vertex_element *el = data;
    f->reads++;
    if (!data) { *n = 2; return S_OK; }
    CHECK(*n >= 2);
    el[0] = (pf_vertex_element){0, 0, 0, 0, 0, 0};
    el[1] = (pf_vertex_element){0xFF, 0, PF_D3DDECLTYPE_UNUSED, 0, 0, 0};
    if (f->invalid == 1) el[0].stream = 15;
    if (f->invalid == 2) el[0].usage = PF_D3DDECLUSAGE_TEXCOORD, el[0].usage_index = 7;
    if (f->invalid == 3) el[1].stream = 0;
    return S_OK;
}
static HRESULT __stdcall create(void *dev, const void *data, void **out)
{
    const pf_vertex_element *el = data;
    (void)dev;
    CHECK(el[1].stream == 1 && el[1].usage_index == 7 && el[2].stream == 0xFF);
    if (fail_create == 1) return E_FAIL;
    if (fail_create == 2) return S_OK; /* success with null object */
    CHECK(created < 1000);
    generated[created] = (fake){obj_vt, 1, 0, 0};
    *out = &generated[created++];
    live++;
    if (live > max_live) max_live = live;
    return fail_create == 3 ? E_FAIL : S_OK;
}

int main(void)
{
    pf_decl_cache c = {0};
    pf_decl_entry *hot;
    void *hot_ours;
    unsigned i, before;
    obj_vt[PF_UNK_ADDREF] = addref;
    obj_vt[PF_UNK_RELEASE] = release;
    obj_vt[PF_DECL_GET_DECLARATION] = get;
    dev_vt[PF_DEV_CREATE_DECL] = create;
    device.vt = dev_vt;
    for (i = 0; i < 700; i++) originals[i] = (fake){obj_vt, 1, 0, 0};
    CHECK(!pf_decl_cache_get(&c, &device, 0) && c.misses == 0);
    hot = pf_decl_cache_get(&c, &device, &originals[0]);
    CHECK(hot && hot->streams == 1 && hot->inst_stream == 1);
    hot_ours = hot->ours;
    for (i = 1; i < 600; i++) {
        CHECK(pf_decl_cache_get(&c, &device, &originals[i]));
        CHECK(pf_decl_cache_get(&c, &device, &originals[0])->ours == hot_ours);
    }
    CHECK(c.count == 256 && c.misses == 600 && c.hits == 599 && c.evictions == 344);
    CHECK(live == 256 && max_live == 256 && originals[0].reads == 2);
    CHECK(originals[1].refs == 1 && originals[599].refs == 2);
    /* All age ranks remain unique and bounded after repeated replacement. */
    for (i = 0; i < 256; i++) {
        unsigned j;
        CHECK(c.entries[i].age < 256);
        for (j = i + 1; j < 256; j++) CHECK(c.entries[i].age != c.entries[j].age);
    }
    /* Unsupported declarations stay cached and do not get reparsed. */
    for (i = 600; i < 603; i++) {
        originals[i].invalid = i - 599;
        before = created;
        CHECK(!pf_decl_cache_get(&c, &device, &originals[i]));
        CHECK(!pf_decl_cache_get(&c, &device, &originals[i]));
        CHECK(originals[i].reads == 2 && created == before && originals[i].refs == 2);
    }
    for (i = 603; i < 606; i++) {
        fail_create = i - 602;
        CHECK(!pf_decl_cache_get(&c, &device, &originals[i]));
        CHECK(!pf_decl_cache_get(&c, &device, &originals[i]));
        CHECK(originals[i].reads == 2 && originals[i].refs == 2);
    }
    CHECK(c.failures == 3);
    before = c.misses;
    pf_decl_cache_clear(&c); /* Device transition releases all owned refs. */
    CHECK(c.count == 0 && c.misses == before && live == 0);
    pf_decl_cache_clear(&c); /* Idempotent cleanup. */
    for (i = 0; i < 700; i++) CHECK(originals[i].refs == 1);
    for (i = 0; i < created; i++) CHECK(generated[i].refs == 0);
    fail_create = 0;
    CHECK(pf_decl_cache_get(&c, &device, &originals[603]));
    pf_decl_cache_clear(&c);
    CHECK(live == 0 && originals[603].refs == 1);
    puts("OK: declaration cache >256, LRU hot entry, unsupported/failure cache, COM balance");
    return 0;
}
