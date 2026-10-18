/* Timing hooks: wrap each outermost call of the timed functions on the main
 * thread (the first thread to run the frame hook), watch the probed ones,
 * attribute the time, and write one CSV row per whole second and the scope
 * table once per minute, from the frame hook's exit. */
#ifndef PF_TIMING_H
#define PF_TIMING_H

#include <windows.h>

#include "inst.h"
#include "sites.h"
#include "uistate.h"

typedef void *(*pf_inst_enter_fn)(unsigned k, void **slot, void *next);
typedef void (*pf_inst_take_fn)(pf_inst_counts *c);

typedef struct {
    unsigned frames, logic_calls, render_calls, skel_render_calls, skel_other_calls;
    unsigned passed_through, stale, rows;
    unsigned scopes, stale_scopes, draws, presents;
    unsigned reflect_runs, reflect_skipped, reflect_forced;
    unsigned shadow_runs, shadow_skipped, shadow_forced, shadow_restored, shadow_held;
    unsigned shadow_extra; /* shadow maps drawn by extra frames (ExtraFrameShadows) */
    unsigned shadow_fresh; /* game frames that reused one an extra frame had just drawn */
    unsigned fx_groups, fx_merge, fx_merge_anylight;
} pf_timing_stats;

/* Before installing: creates both CSVs (overwriting them) with their headers.
 * NULL paths: no files (only the reflection interval is wanted). Returns 0
 * if the timing CSV can't be created. */
int pf_timing_open(const pf_site *sites, const WCHAR *csv_path, const WCHAR *scopes_path);
/* 0: per-mesh markers are ignored (the draw counter isn't installed then). */
void pf_timing_set_detail(int detail);
/* Before installing: the water reflection runs once every `interval` calls. */
void pf_timing_set_reflect(unsigned interval);
/* Before installing: the shadow map is redrawn once every `interval` calls;
 * in between, the shaders get the matrix of the stored map. */
void pf_timing_set_shadow(unsigned interval);
/* The shadow map is redrawn every frame while the player places a building,
 * and while the camera scrolls if while_scrolling. ui is copied. */
void pf_timing_set_ui(const pf_ui_state *ui, int while_scrolling);
void pf_timing_set_next(unsigned k, void *trampoline);
/* The instancing sites go to enter (with where the hooked call would go);
 * take fills each CSV row's instancing columns. NULL: pass through. */
void pf_timing_set_inst(pf_inst_enter_fn enter, pf_inst_take_fn take);
/* Bit k set: site k is hooked. Decides which columns are filled. */
void pf_timing_set_installed(pf_site_mask mask);
/* ShadowPassVerify is on: its CSV columns are filled. */
void pf_timing_set_sp_verify(int on);
/* The pass the render markers say is open: 0 shadow map, 1 reflection,
 * 2 views, -1 none. */
int pf_timing_pass(void);
/* No hooks after all: closes and deletes both CSVs. */
void pf_timing_discard(const WCHAR *csv_path, const WCHAR *scopes_path);
/* Writes the open row and the scope table now. */
void pf_timing_flush(void);
void pf_timing_get_stats(pf_timing_stats *s);

/* Aggregate one qualified native mesh interval; logical counts remain per
 * accepted record. Only the accepted render thread can begin an interval. */
int pf_timing_render_thread(void);
int pf_timing_bulk_begin(void);
int pf_timing_bulk_end(unsigned accepted);
int pf_timing_bulk_cancel(void);
void pf_timing_bulk_abandon(void);

void *pf_enter(unsigned k, void **slot);
void *pf_exit(unsigned k, unsigned char *esp);

#endif
