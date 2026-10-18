/* Native bulk group interface. Configuration must come from the verified
 * executable contract, before enabling the group hook. No discovery or
 * executable patch is performed by these functions. */
#ifndef PF_INSTBULK_H
#define PF_INSTBULK_H
#include "instsubmit.h"

typedef struct {
    const unsigned char *mesh, *triangles, *draw;
    void *volatile *begin_callback, *volatile *end_callback;
} pf_inst_bulk_sites;

/* Only between groups. A pending/failed transaction refuses reconfiguration. */
int pf_inst_bulk_configure(const pf_inst_bulk_sites *sites, int enable);
/* Group wrapper owns this context. finish never draws after native End. */
void pf_inst_bulk_context(int active);
void pf_inst_bulk_finish(int interrupted);
int pf_inst_bulk_ready(void);
int pf_inst_bulk_matches(const unsigned char *record, const unsigned char *end, void *method);
/* Returns accepted queue records; the deferred leader precedes record.
 * outcome reports the latest submission transaction, not total group units.
 * The caller must check coherent AND complete before normal continuation.
 * Unsupported staging leaves pending units for cancel or the native member
 * path. Only already-accepted records may be advanced in the native cursor. */
unsigned pf_inst_bulk_advance(const unsigned char *record, const unsigned char *end,
                              void *method, pf_submit_result *outcome);
pf_submit_result pf_inst_bulk_cancel(void);
unsigned pf_inst_bulk_leaders(void);

#endif
