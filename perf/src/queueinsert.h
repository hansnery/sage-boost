#ifndef PF_QUEUEINSERT_H
#define PF_QUEUEINSERT_H
#include "scan.h"

typedef void (__attribute__((thiscall)) *pf_queue_add_fn)(void *, void *, const void *);
const char *pf_queue_insert_find(const pf_image *image, unsigned *entry);
int pf_queue_insert_install(const pf_image *image, const char **why);
/* The verified native entry/trampoline must be set before invoking the handler. */
void pf_queue_insert_original(pf_queue_add_fn original);
void __attribute__((thiscall)) pf_queue_insert_add(void *queues, void *mesh, const void *methods);
#endif
