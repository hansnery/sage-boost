/* One second of timing as a CSV row, and the per-minute scope rows. Integer only. */
#ifndef PF_CSVROW_H
#define PF_CSVROW_H

#define PF_HAVE_LOGIC 1u
#define PF_HAVE_RENDER 2u
#define PF_HAVE_SKEL 4u
#define PF_HAVE_SCENE 8u
#define PF_HAVE_PRESENT 16u
#define PF_HAVE_SCOPES 32u
#define PF_HAVE_DRAWS 64u
#define PF_HAVE_REFLECT 128u
#define PF_HAVE_SHADOW 256u
#define PF_HAVE_BATCH 512u
#define PF_HAVE_INST 1024u
#define PF_HAVE_FPS 2048u
#define PF_HAVE_PUMP 4096u
#define PF_HAVE_SBATCH 8192u     /* ShadowBatchIgnoreLight's site */
#define PF_HAVE_STENCIL 16384u   /* NoStencilFlushes' sites */
#define PF_HAVE_SPART 32768u     /* the shadow pass's particle site */
#define PF_HAVE_SPVERIFY 65536u  /* ShadowPassVerify */
#define PF_HAVE_LIGHTS 131072u   /* the light environment cuts' sites */
#define PF_HAVE_LTIME 262144u    /* renderOneObject's timers */
#define PF_HAVE_CATCHUP 524288u  /* catch-up's sites */

#include "catchup.h"
#include "extraprofile.h"
#include "presentgap.h"
#include "inst.h"
#include "lights.h"
#include "shadowpass.h"

/* Render-event labels with a column of their own, in column order. The
 * first PF_SCOPE_COLUMNS sit together in the row; FX_BATCH came later and
 * is appended at the end. */
enum {
    PF_SCOPE_SHADOW, PF_SCOPE_REFLECT, PF_SCOPE_VIEWS, PF_SCOPE_UI, PF_SCOPE_TERRAIN,
    PF_SCOPE_WATER, PF_SCOPE_PARTICLES, PF_SCOPE_TREES, PF_SCOPE_SHRUBS,
    PF_SCOPE_DECAL_SHADOWS, PF_SCOPE_VOLUME_SHADOWS, PF_SCOPE_STATIC_SORT, PF_SCOPE_FX_MESH,
    PF_SCOPE_DX8_MESH, PF_SCOPE_COLUMNS,
    PF_SCOPE_FX_BATCH = PF_SCOPE_COLUMNS, PF_SCOPE_ALL
};
/* The frame-level markers, which count toward the frame's covered time. */
#define PF_SCOPE_IS_FRAME_LEVEL(c) ((unsigned)(c) <= PF_SCOPE_UI)

extern const char *const pf_scope_labels[PF_SCOPE_ALL];

/* Per-mesh markers: "Rendering mesh<TAB><kind><TAB><model>", built at run
 * time in reused buffers. */
int pf_is_mesh_label(const char *label);
/* The column a label counts toward (per-mesh labels by their kind), or -1. */
int pf_scope_column(const char *label);
/* The name a label is listed under in the scope table: per-mesh labels by
 * kind, tabs as spaces, commas as semicolons, truncated to cap - 1. */
void pf_scope_name(const char *label, char *out, unsigned cap);

typedef struct {
    unsigned time_s;
    unsigned frames;
    unsigned long long frame_us, frame_max_us;
    unsigned long long logic_us, render_us;
    unsigned long long skel_render_us, skel_other_us;
    unsigned skel_render_calls; /* outermost skeleton updates during the render */
    unsigned to_workers, main_thread;
    unsigned long long vis_us, cust_us, flush_us, present_us;
    unsigned long long scope_us[PF_SCOPE_ALL];
    unsigned draws;             /* in the whole row */
    unsigned long long rest_us;
    unsigned reflect_skipped;   /* in the whole row */
    unsigned shadow_skipped;    /* in the whole row */
    unsigned shadow_meshes, reflect_meshes, view_meshes; /* per-mesh markers per pass, whole row */
    unsigned shadow_full_rate;  /* redraws forced by placement or scrolling, whole row */
    unsigned fx_groups;         /* FX shader batch groups, whole row */
    unsigned fx_merge;          /* groups that could share the previous group's effect pass */
    unsigned fx_merge_anylight; /* the same, ignoring the light environment */
    pf_inst_counts inst;        /* whole row; meshes and draws per frame in the CSV */
    unsigned long long idle_us; /* the limiter's wait, whole row */
    unsigned waits;             /* game frames that waited */
    unsigned extra_frames;      /* extra frames drawn, whole row */
    unsigned long long extra_us;
    unsigned extra_late;        /* game frames made late by an extra frame */
    unsigned extra_units;       /* units moved ahead, summed over the extra frames */
    unsigned long long wait_us;    /* the limiter's first Sleep to the next update, whole row */
    unsigned long long outside_us; /* update's return to the limiter (or the next update) */
    unsigned long long pump_us;    /* the Windows message pump, whole row */
    unsigned sleeps;               /* the limiter's Sleep calls, whole row */
    unsigned long long budget_us;  /* time left before the deadline at each first wait, whole row */
    unsigned extra_cost_us;        /* the extra frames' expected cost when the row closed */
    unsigned extra_ref_us;         /* the game's own draw when the row closed (0: not timed) */
    unsigned extra_max_us;         /* the slowest extra frame in the row */
    unsigned late_ms;              /* how late the late game frames were, summed over the row */
    unsigned held;                 /* view and particle updates held in extra frames, whole row */
    unsigned extra_shadows;        /* shadow maps drawn in extra frames, whole row */
    unsigned extra_reflections;    /* reflections drawn in extra frames, whole row */
    unsigned long long gap_us;     /* gaps between shown frames, summed */
    unsigned gaps, gap_max_us;     /* how many, the longest */
    unsigned long long disp_err_us; /* extra frames: |shown - planned shown time|, summed */
    unsigned rate_changes;         /* game frames whose extra-frame count changed */
    unsigned unit_steps, unit_uneven; /* moving units' steps, and those uneven by over 1/4 */
    unsigned extra_clocks;         /* command button clocks drawn in extra frames, whole row */
    /* The control bar's clock requests, the clocks the game's own draws
     * drew, and game frames with an extra frame that drew no clock on a
     * button asked for one, whole row. */
    unsigned clock_requests, clocks_noted, clock_unseen;
    unsigned fx_groups_pass[3];       /* fx_groups by pass: shadow map, reflection, views */
    pf_sp_counts sp;                  /* the shadow-pass fixes, whole row */
    pf_lt_counts lt;                  /* renderOneObject and the light cuts, whole row */
    unsigned logic_calls;             /* GameLogic::update calls, whole row */
    pf_cu_row cu;                     /* catch-up, whole row */
    unsigned fps_blocked[4], fps_probes[3], fps_learned_us;
    pf_ep_row extra_profile;
    pf_present_row presentation;
    unsigned have;              /* PF_HAVE_*: columns that were measured */
} pf_row;

/* Room for all columns even when counters approach their integer limits. */
#define PF_CSV_ROW_CAP 8192
extern const char pf_csv_header[];
extern const char pf_scope_header[];

/* NUL- and "\r\n"-terminated row; columns not measured are empty. Returns
 * its length, or 0 if it doesn't fit in cap. */
unsigned pf_format_row(const pf_row *r, char *out, unsigned cap);

/* "time_s,label,calls per frame,ms per frame\r\n"; same return. */
unsigned pf_format_scope_row(unsigned time_s, const char *label, unsigned calls, unsigned frames,
                             unsigned long long us, char *out, unsigned cap);

#endif
