#ifndef PF_DECLCACHE_H
#define PF_DECLCACHE_H

#define PF_DECL_CACHE_SIZE 256
typedef struct {
    void *game, *ours;
    unsigned streams, inst_stream, age;
} pf_decl_entry;
typedef struct {
    pf_decl_entry entries[PF_DECL_CACHE_SIZE];
    unsigned count, hits, misses, evictions, failures;
} pf_decl_cache;

/* Single render thread only. Returned pointers remain valid until the next
 * lookup/clear: callers must finish their group (including verification and
 * device state restoration) before either operation. Zero-initialize once. */
pf_decl_entry *pf_decl_cache_get(pf_decl_cache *cache, void *dev, void *game);
/* Releases owned COM references, preserving cumulative diagnostic counters. */
void pf_decl_cache_clear(pf_decl_cache *cache);
#endif
