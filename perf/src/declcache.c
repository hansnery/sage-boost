#include "declcache.h"
#include "d3d9vt.h"

#define MAX_ELEMENTS 64

static void release(void *obj)
{
    if (obj)
        ((pf_release_fn)PF_VT(obj)[PF_UNK_RELEASE])(obj);
}

void pf_decl_cache_clear(pf_decl_cache *c)
{
    unsigned i;
    for (i = 0; i < c->count; i++) {
        release(c->entries[i].ours);
        release(c->entries[i].game);
        c->entries[i] = (pf_decl_entry){0};
    }
    c->count = 0;
}

pf_decl_entry *pf_decl_cache_get(pf_decl_cache *c, void *dev, void *game)
{
    pf_vertex_element el[MAX_ELEMENTS + 2];
    UINT n = 0;
    unsigned i, slot = 0, age, streams = 0, top = 0;
    pf_decl_entry *d;
    pf_get_data_fn get;
    void *ours = 0;
    if (!game || !dev)
        return 0;
    for (i = 0; i < c->count; i++) {
        if (c->entries[i].game == game) {
            d = &c->entries[i];
            age = d->age;
            /* Dense ranks cannot wrap, even after billions of lookups. */
            for (slot = 0; slot < c->count; slot++)
                if (c->entries[slot].age < age)
                    c->entries[slot].age++;
            d->age = 0;
            c->hits++;
            return d->ours ? d : 0;
        }
        if (c->entries[i].age > c->entries[slot].age)
            slot = i;
    }
    c->misses++;
    if (c->count < PF_DECL_CACHE_SIZE)
        slot = c->count++;
    else {
        release(c->entries[slot].ours);
        release(c->entries[slot].game);
        c->evictions++;
    }
    for (i = 0; i < c->count; i++)
        if (i != slot)
            c->entries[i].age++;
    d = &c->entries[slot];
    *d = (pf_decl_entry){0};
    ((pf_release_fn)PF_VT(game)[PF_UNK_ADDREF])(game);
    d->game = game; /* Also cache unsupported/failed declarations. */
    get = (pf_get_data_fn)PF_VT(game)[PF_DECL_GET_DECLARATION];
    if (FAILED(get(game, 0, &n)) || n < 2 || n > MAX_ELEMENTS || FAILED(get(game, el, &n)) ||
        n < 2 || n > MAX_ELEMENTS)
        return 0;
    for (i = 0; i < n && el[i].stream != 0xFF; i++) {
        if ((el[i].usage == PF_D3DDECLUSAGE_TEXCOORD && el[i].usage_index == 7) ||
            el[i].stream >= 15)
            return 0;
        streams |= 1u << el[i].stream;
        if (el[i].stream > top)
            top = el[i].stream;
    }
    if (!i || i == n)
        return 0;
    el[i] = (pf_vertex_element){(WORD)(top + 1), 0, PF_D3DDECLTYPE_FLOAT1, 0,
                                PF_D3DDECLUSAGE_TEXCOORD, 7};
    el[i + 1] = (pf_vertex_element){0xFF, 0, PF_D3DDECLTYPE_UNUSED, 0, 0, 0};
    if (FAILED(((pf_create_obj_fn)PF_VT(dev)[PF_DEV_CREATE_DECL])(dev, el, &ours)) || !ours) {
        release(ours);
        c->failures++;
        return 0;
    }
    d->ours = ours;
    d->streams = streams;
    d->inst_stream = top + 1;
    return d;
}
