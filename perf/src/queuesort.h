#ifndef PF_QUEUESORT_H
#define PF_QUEUESORT_H
#include "scan.h"

typedef unsigned (__cdecl *pf_queue_compare_fn)(const void *, const void *);
typedef void (__cdecl *pf_queue_sort_fn)(void *, void *, pf_queue_compare_fn);
typedef struct { unsigned entry; pf_queue_compare_fn mesh, mesh_state; } pf_queue_sort_sites;
const char *pf_queue_sort_find(const pf_image *image, pf_queue_sort_sites *sites);
int pf_queue_sort_install(const pf_image *image, const char **why);
void pf_queue_sort_configure(pf_queue_sort_fn original, pf_queue_compare_fn mesh,
                             pf_queue_compare_fn mesh_state);
void __cdecl pf_queue_sort_run(void *first, void *last, pf_queue_compare_fn compare);
/* Shared algorithm entry for already qualified records, including forced
 * depth-zero heap coverage. kind 0 = mesh, 1 = mesh then state pointer. */
void pf_queue_sort_raw(void *first, void *last, unsigned kind, unsigned depth);
#endif
