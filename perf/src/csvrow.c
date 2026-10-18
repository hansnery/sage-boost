#include "csvrow.h"

#include <stddef.h>

const char *const pf_scope_labels[PF_SCOPE_ALL] = {
    "UpdateShadowMap", "UpdateWaterReflection", "RenderViews", "RenderUI", "RenderTerrain",
    "RenderWater", "RenderParticles", "RenderTrees", "RenderShrubs", "RenderDecalShadows",
    "RenderVolumeShadows", "RenderStaticSortLists", "MeshFXShader", "MeshDX8Render",
    "RenderFXShaderBatch",
};

static int starts_with(const char *s, const char *prefix)
{
    while (*prefix && *s == *prefix) {
        s++;
        prefix++;
    }
    return !*prefix;
}

int pf_is_mesh_label(const char *label)
{
    return starts_with(label, "Rendering mesh\t");
}

int pf_scope_column(const char *label)
{
    unsigned i;

    if (starts_with(label, "Rendering mesh\tFXShader"))
        return PF_SCOPE_FX_MESH;
    if (starts_with(label, "Rendering mesh\tDX8Render"))
        return PF_SCOPE_DX8_MESH;
    for (i = 0; i < PF_SCOPE_ALL; i++) {
        const char *a = label, *b = pf_scope_labels[i];
        while (*a && *a == *b) {
            a++;
            b++;
        }
        if (*a == *b)
            return (int)i;
    }
    return -1;
}

void pf_scope_name(const char *label, char *out, unsigned cap)
{
    unsigned i = 0, tabs = 0;
    int mesh = pf_is_mesh_label(label);

    if (!cap)
        return;
    for (; *label && i + 1 < cap; label++) {
        char c = *label;
        if (c == '\t' && mesh && ++tabs == 2)
            break;
        out[i++] = c == '\t' ? ' ' : c == ',' ? ';' : c;
    }
    out[i] = 0;
}

const char pf_csv_header[] =
    "time_s,frames,frame_avg_ms,frame_max_ms,logic_ms,render_ms,skel_render_ms,"
    "skel_other_ms,anim_on_screen,to_workers,main_thread,skel_us_per_obj,"
    "vis_ms,cust_ms,flush_ms,present_ms,shadow_ms,reflect_ms,views_ms,ui_ms,terrain_ms,"
    "water_ms,particles_ms,trees_ms,shrubs_ms,decal_shadows_ms,volume_shadows_ms,"
    "static_sort_ms,fx_mesh_ms,dx8_mesh_ms,draws,rest_ms,fx_batch_ms,reflect_skipped,"
    "shadow_skipped,shadow_meshes,reflect_meshes,view_meshes,shadow_full_rate,fx_groups,fx_merge,"
    "fx_merge_anylight,inst_meshes,inst_draws,inst_groups,inst_leaders,inst_small,inst_multipass,"
    "inst_game_inst,inst_no_shader,inst_disabled,inst_bones,inst_mismatch,inst_verify,"
    "verify_samples,verify_bad,idle_ms,extra_frames,extra_ms,extra_late,extra_units,waits,wait_ms,"
    "outside_ms,pump_ms,sleeps,budget_ms,extra_cost_ms,extra_max_ms,late_ms,extra_held,"
    "extra_shadows,extra_reflections,gap_avg_ms,gap_max_ms,disp_err_ms,rate_changes,unit_steps,"
    "unit_uneven,inst_bonelist,extra_clocks,shadow_inst_meshes,shadow_inst_draws,shadow_inst_groups,"
    "shadow_inst_leaders,shadow_inst_small,shadow_inst_multipass,shadow_inst_game_inst,"
    "shadow_inst_no_shader,shadow_inst_disabled,shadow_inst_bones,shadow_inst_mismatch,"
    "shadow_inst_verify,shadow_verify_samples,shadow_verify_bad,shadow_inst_bonelist,"
    "shadow_inst_off,shadow_inst_split,shadow_inst_ms,clock_requests,clocks_noted,clock_unseen,"
    "fx_groups_shadow,fx_groups_reflect,"
    "fx_groups_view,sb_tests,sb_ends_game,sb_ends_now,stencil_skip_shadow,stencil_skip_other,"
    "particles_skipped,particles_kept,spv_samples,spv_bad,skipped_draws,logic_frames,"
    "game_speed_pct,behind_ms,behind_max_ms,cu_render_ms,cu_after_ms,waits_cut,extra_ref_ms,"
    "logic_ticks,"
    "lt_calls_shadow,lt_ms_shadow,lt_pre_ms_shadow,lt_env_ms_shadow,lt_scene_shadow,"
    "lt_scene_kept_shadow,lt_dyn_shadow,lt_dyn_kept_shadow,"
    "lt_calls_reflect,lt_ms_reflect,lt_pre_ms_reflect,lt_env_ms_reflect,lt_scene_reflect,"
    "lt_scene_kept_reflect,lt_dyn_reflect,lt_dyn_kept_reflect,"
    "lt_calls_views,lt_ms_views,lt_pre_ms_views,lt_env_ms_views,lt_scene_views,"
    "lt_scene_kept_views,lt_dyn_views,lt_dyn_kept_views,"
    "lt_skipped,lt_decided,lt_reuse_hits,lt_reuse_misses,lt_fallback,lt_verify_samples,"
    "lt_verify_bad,lt_stale_draws,sb_light_fixed,sb_light_missed,lt_extra_hits,"
    "lt_extra_misses,fps_block_cost,fps_block_catchup,fps_block_pause,fps_block_stop,"
    "fps_probe_attempts,fps_probe_success,fps_probe_late,fps_learned_ms,"
    "extra_profile_valid,extra_profile_invalid,extra_profile_total_ms,extra_shadow_samples,"
    "extra_shadow_total_ms,extra_reflect_samples,extra_reflect_total_ms,extra_views_samples,"
    "extra_views_total_ms,extra_ui_samples,extra_ui_total_ms,present_calls,present_gaps,"
    "present_p50_upper_ms,present_p95_upper_ms,present_gap_le_8333us,present_gap_le_12500us,"
    "present_gap_le_16667us,present_gap_le_20000us,present_gap_le_25000us,"
    "present_gap_le_33334us,present_gap_le_50000us,present_gap_le_100000us,"
    "present_gap_le_200000us,present_gap_le_500000us,present_gap_le_1000000us,"
    "present_gap_over_1000000us\r\n";

const char pf_scope_header[] = "time_s,scope,calls_per_frame,ms_per_frame\r\n";

typedef struct {
    char *p;
    unsigned left;
    int full;
} out_buf;

static void put_c(out_buf *o, char c)
{
    if (o->left > 1) {
        *o->p++ = c;
        o->left--;
    } else {
        o->full = 1;
    }
}

static void put_u(out_buf *o, unsigned long long v)
{
    char tmp[24];
    unsigned n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        put_c(o, tmp[--n]);
}

/* v / 10^digits, with `digits` decimals. */
static void put_fixed(out_buf *o, unsigned long long v, unsigned digits)
{
    unsigned long long scale = 1;
    unsigned i;
    for (i = 0; i < digits; i++)
        scale *= 10;
    put_u(o, v / scale);
    put_c(o, '.');
    v %= scale;
    for (i = 0; i < digits; i++) {
        scale /= 10;
        put_c(o, (char)('0' + v / scale));
        v %= scale;
    }
}

static void per_frame_ms(out_buf *o, unsigned long long us, unsigned frames)
{
    put_fixed(o, (us + frames / 2) / frames, 3);
}

static void sep(out_buf *o)
{
    put_c(o, ',');
}

static void ms_field(out_buf *o, int have, unsigned long long us, unsigned frames)
{
    sep(o);
    if (have)
        per_frame_ms(o, us, frames);
}

static unsigned finish(out_buf *o, unsigned cap)
{
    put_c(o, '\r');
    put_c(o, '\n');
    if (o->full)
        return 0;
    *o->p = 0;
    return cap - o->left;
}

unsigned pf_format_row(const pf_row *r, char *out, unsigned cap)
{
    out_buf o = { out, cap, 0 };
    unsigned f = r->frames ? r->frames : 1, i;
    int skel = (r->have & PF_HAVE_SKEL) != 0, scene = (r->have & PF_HAVE_SCENE) != 0;

    if (cap == 0)
        return 0;
    put_u(&o, r->time_s);
    sep(&o);
    put_u(&o, r->frames);
    sep(&o);
    per_frame_ms(&o, r->frame_us, f);
    sep(&o);
    put_fixed(&o, r->frame_max_us, 3);
    ms_field(&o, (r->have & PF_HAVE_LOGIC) != 0, r->logic_us, f);
    ms_field(&o, (r->have & PF_HAVE_RENDER) != 0, r->render_us, f);
    ms_field(&o, skel, r->skel_render_us, f);
    ms_field(&o, skel, r->skel_other_us, f);
    sep(&o);
    if (skel)
        put_u(&o, (r->skel_render_calls + f / 2) / f);
    sep(&o);
    put_u(&o, r->to_workers);
    sep(&o);
    put_u(&o, r->main_thread);
    sep(&o);
    if (skel && r->skel_render_calls)
        put_fixed(&o, (r->skel_render_us * 100 + r->skel_render_calls / 2) / r->skel_render_calls,
                  2);
    ms_field(&o, scene, r->vis_us, f);
    ms_field(&o, scene, r->cust_us, f);
    ms_field(&o, scene, r->flush_us, f);
    ms_field(&o, (r->have & PF_HAVE_PRESENT) != 0, r->present_us, f);
    for (i = 0; i < PF_SCOPE_COLUMNS; i++)
        ms_field(&o, (r->have & PF_HAVE_SCOPES) != 0, r->scope_us[i], f);
    sep(&o);
    if (r->have & PF_HAVE_DRAWS)
        put_u(&o, (r->draws + f / 2) / f);
    ms_field(&o, 1, r->rest_us, f);
    ms_field(&o, (r->have & PF_HAVE_SCOPES) != 0, r->scope_us[PF_SCOPE_FX_BATCH], f);
    sep(&o);
    if (r->have & PF_HAVE_REFLECT)
        put_u(&o, r->reflect_skipped);
    sep(&o);
    if (r->have & PF_HAVE_SHADOW)
        put_u(&o, r->shadow_skipped);
    sep(&o);
    if (r->have & PF_HAVE_SCOPES)
        put_u(&o, (r->shadow_meshes + f / 2) / f);
    sep(&o);
    if (r->have & PF_HAVE_SCOPES)
        put_u(&o, (r->reflect_meshes + f / 2) / f);
    sep(&o);
    if (r->have & PF_HAVE_SCOPES)
        put_u(&o, (r->view_meshes + f / 2) / f);
    sep(&o);
    if (r->have & PF_HAVE_SHADOW)
        put_u(&o, r->shadow_full_rate);
    sep(&o);
    if (r->have & PF_HAVE_BATCH)
        put_u(&o, (r->fx_groups + f / 2) / f);
    sep(&o);
    if (r->have & PF_HAVE_BATCH)
        put_u(&o, (r->fx_merge + f / 2) / f);
    sep(&o);
    if (r->have & PF_HAVE_BATCH)
        put_u(&o, (r->fx_merge_anylight + f / 2) / f);
    {
        /* Meshes and draws per frame (pf_inst_counts order), then the
         * verify samples of the whole row. */
        const unsigned *c = (const unsigned *)&r->inst;
        unsigned n = (unsigned)(offsetof(pf_inst_counts, bonelist) / sizeof(unsigned));
        for (i = 0; i < n; i++) {
            sep(&o);
            if (r->have & PF_HAVE_INST)
                put_u(&o, i + 2 < n ? (c[i] + f / 2) / f : c[i]);
        }
    }
    ms_field(&o, (r->have & PF_HAVE_FPS) != 0, r->idle_us, r->waits ? r->waits : 1);
    sep(&o);
    if (r->have & PF_HAVE_FPS)
        put_u(&o, r->extra_frames);
    ms_field(&o, (r->have & PF_HAVE_FPS) != 0, r->extra_us, r->extra_frames ? r->extra_frames : 1);
    sep(&o);
    if (r->have & PF_HAVE_FPS)
        put_u(&o, r->extra_late);
    sep(&o);
    if (r->have & PF_HAVE_FPS)
        put_u(&o, r->extra_frames ? (r->extra_units + r->extra_frames / 2) / r->extra_frames : 0);
    /* Where each game frame's time goes outside update, per game frame. */
    sep(&o);
    if (r->have & PF_HAVE_FPS)
        put_u(&o, r->waits);
    ms_field(&o, (r->have & PF_HAVE_FPS) != 0, r->wait_us, f);
    ms_field(&o, (r->have & PF_HAVE_FPS) != 0, r->outside_us, f);
    ms_field(&o, (r->have & PF_HAVE_PUMP) != 0, r->pump_us, f);
    /* What the scheduler saw at the wait: compare budget_ms with wait_ms. */
    sep(&o);
    if (r->have & PF_HAVE_FPS)
        put_u(&o, r->sleeps);
    ms_field(&o, (r->have & PF_HAVE_FPS) != 0, r->budget_us, f);
    ms_field(&o, (r->have & PF_HAVE_FPS) != 0, r->extra_cost_us, 1);
    /* Per row: the slowest extra frame, the lateness it caused, and the
     * per-draw updates held (two per extra frame when they are in place). */
    ms_field(&o, (r->have & PF_HAVE_FPS) != 0, r->extra_max_us, 1);
    sep(&o);
    if (r->have & PF_HAVE_FPS)
        put_u(&o, r->late_ms);
    sep(&o);
    if (r->have & PF_HAVE_FPS)
        put_u(&o, r->held);
    /* Passes extra frames redrew instead of reusing (per row). */
    sep(&o);
    if (r->have & PF_HAVE_FPS)
        put_u(&o, r->extra_shadows);
    sep(&o);
    if (r->have & PF_HAVE_FPS)
        put_u(&o, r->extra_reflections);
    /* Pacing: the average and longest gap between shown frames, the average
     * error of an extra frame's shown time; per row, count changes and
     * moving units' steps (all, and uneven ones). */
    ms_field(&o, (r->have & PF_HAVE_FPS) != 0, r->gap_us, r->gaps ? r->gaps : 1);
    ms_field(&o, (r->have & PF_HAVE_FPS) != 0, r->gap_max_us, 1);
    ms_field(&o, (r->have & PF_HAVE_FPS) != 0, r->disp_err_us,
             r->extra_frames ? r->extra_frames : 1);
    sep(&o);
    if (r->have & PF_HAVE_FPS)
        put_u(&o, r->rate_changes);
    sep(&o);
    if (r->have & PF_HAVE_FPS)
        put_u(&o, r->unit_steps);
    sep(&o);
    if (r->have & PF_HAVE_FPS)
        put_u(&o, r->unit_uneven);
    /* Instanced meshes whose bone list isn't as long as their joints per
     * vertex, per frame (counted in inst_meshes too). */
    sep(&o);
    if (r->have & PF_HAVE_INST)
        put_u(&o, (r->inst.bonelist + f / 2) / f);
    sep(&o);
    if (r->have & PF_HAVE_FPS)
        put_u(&o, r->extra_clocks);
    /* The shadow-map pass's share of the inst_* columns (same order and
     * units), then its meshes left to the game by ShadowInstancing=0, those
     * in groups the game split from one of the same model and method, and
     * the time in its instanced draws, per frame. */
    for (i = 0; i < PF_INST_PASS_COUNTS; i++) {
        const unsigned *c = r->inst.shadow;
        unsigned whole_row = i == (unsigned)(offsetof(pf_inst_counts, verify_samples) / 4) ||
                             i == (unsigned)(offsetof(pf_inst_counts, verify_bad) / 4);
        sep(&o);
        if (r->have & PF_HAVE_INST)
            put_u(&o, whole_row ? c[i] : (c[i] + f / 2) / f);
    }
    sep(&o);
    if (r->have & PF_HAVE_INST)
        put_u(&o, (r->inst.shadow_off + f / 2) / f);
    sep(&o);
    if (r->have & PF_HAVE_INST)
        put_u(&o, (r->inst.shadow_split + f / 2) / f);
    ms_field(&o, (r->have & PF_HAVE_INST) != 0, r->inst.shadow_inst_us, f);
    /* Command button clocks, whole row: the control bar's requests, the
     * clocks the game's own draws drew, and game frames with an extra frame
     * that drew no clock on a button asked for one. */
    sep(&o);
    if (r->have & PF_HAVE_FPS)
        put_u(&o, r->clock_requests);
    sep(&o);
    if (r->have & PF_HAVE_FPS)
        put_u(&o, r->clocks_noted);
    sep(&o);
    if (r->have & PF_HAVE_FPS)
        put_u(&o, r->clock_unseen);
    /* FX shader groups per frame by pass (shadow map, reflection, views). */
    for (i = 0; i < 3; i++) {
        sep(&o);
        if (r->have & PF_HAVE_BATCH)
            put_u(&o, (r->fx_groups_pass[i] + f / 2) / f);
    }
    /* The shadow pass's batch tests per frame, and the group ends among them
     * as the game decides and as ShadowBatchIgnoreLight does (the difference
     * is the groups merged). */
    sep(&o);
    if (r->have & PF_HAVE_SBATCH)
        put_u(&o, (r->sp.batch_tests + f / 2) / f);
    sep(&o);
    if (r->have & PF_HAVE_SBATCH)
        put_u(&o, (r->sp.batch_split_game + f / 2) / f);
    sep(&o);
    if (r->have & PF_HAVE_SBATCH)
        put_u(&o, (r->sp.batch_split_now + f / 2) / f);
    /* Objects drawn without their two stencil-only flushes, per frame. */
    sep(&o);
    if (r->have & PF_HAVE_STENCIL)
        put_u(&o, (r->sp.stencil_shadow + f / 2) / f);
    sep(&o);
    if (r->have & PF_HAVE_STENCIL)
        put_u(&o, (r->sp.stencil_other + f / 2) / f);
    /* Shadow-pass particle walks skipped and kept, and verify samples (whole row). */
    sep(&o);
    if (r->have & PF_HAVE_SPART)
        put_u(&o, r->sp.particles_skipped);
    sep(&o);
    if (r->have & PF_HAVE_SPART)
        put_u(&o, r->sp.particles_kept);
    sep(&o);
    if (r->have & PF_HAVE_SPVERIFY)
        put_u(&o, r->sp.verify_samples);
    sep(&o);
    if (r->have & PF_HAVE_SPVERIFY)
        put_u(&o, r->sp.verify_bad);
    /* Catch-up, whole row: renders skipped, GameLogic::update calls (6 per 5
     * game frames, menus too), the game's speed (logic runs one step per game
     * frame: game frames in the row, about a second, over 30), how far behind
     * its schedule frames started (average and most, over the frames it was
     * tracked), the render and after-draw estimates the decision used, and
     * limiter waits ended at the schedule. */
    sep(&o);
    if (r->have & PF_HAVE_CATCHUP)
        put_u(&o, r->cu.skipped);
    sep(&o);
    if (r->have & PF_HAVE_LOGIC)
        put_u(&o, r->logic_calls);
    sep(&o);
    if (r->have & PF_HAVE_LOGIC)
        put_fixed(&o, (r->frames * 1000ull + 15) / 30, 1);
    ms_field(&o, (r->have & PF_HAVE_CATCHUP) != 0, r->cu.behind_ms * 1000,
             r->cu.tracked ? r->cu.tracked : 1);
    sep(&o);
    if (r->have & PF_HAVE_CATCHUP)
        put_u(&o, r->cu.behind_max_ms);
    ms_field(&o, (r->have & PF_HAVE_CATCHUP) != 0, r->cu.render_us, 1);
    ms_field(&o, (r->have & PF_HAVE_CATCHUP) != 0, r->cu.after_us, 1);
    sep(&o);
    if (r->have & PF_HAVE_CATCHUP)
        put_u(&o, r->cu.waits_cut);
    /* The game's own draw, what an extra frame not measured yet is expected
     * to cost, when the row closed; and the steps of TheGameLogic's frame
     * counter in the row (5 a second at full speed, none outside a game). */
    ms_field(&o, (r->have & PF_HAVE_FPS) && r->extra_ref_us, r->extra_ref_us, 1);
    sep(&o);
    if ((r->have & PF_HAVE_CATCHUP) && r->cu.ticks_on)
        put_u(&o, r->cu.logic_ticks);
    /* renderOneObject by pass, per frame: calls, its time, the time before
     * the object's Render call and in the light build, then the scene and
     * dynamic lights tested and added. */
    for (i = 0; i < 3; i++) {
        int t = (r->have & PF_HAVE_LTIME) != 0;
        const pf_lt_counts *l = &r->lt;
        sep(&o);
        if (t)
            put_u(&o, (l->calls[i] + f / 2) / f);
        ms_field(&o, t, l->us[i], f);
        ms_field(&o, t, l->pre_us[i], f);
        ms_field(&o, t, l->env_us[i], f);
        sep(&o);
        if (t)
            put_u(&o, (l->scene_tested[i] + f / 2) / f);
        sep(&o);
        if (t)
            put_u(&o, (l->scene_kept[i] + f / 2) / f);
        sep(&o);
        if (t)
            put_u(&o, (l->dyn_tested[i] + f / 2) / f);
        sep(&o);
        if (t)
            put_u(&o, (l->dyn_kept[i] + f / 2) / f);
    }
    /* The cuts: builds skipped, objects decided and reuses per frame; passes
     * left to the game, verify samples and stale lit draws per row. */
    sep(&o);
    if (r->have & PF_HAVE_LIGHTS)
        put_u(&o, (r->lt.skipped + f / 2) / f);
    sep(&o);
    if (r->have & PF_HAVE_LIGHTS)
        put_u(&o, (r->lt.decided + f / 2) / f);
    sep(&o);
    if (r->have & PF_HAVE_LIGHTS)
        put_u(&o, (r->lt.reuse_hits + f / 2) / f);
    sep(&o);
    if (r->have & PF_HAVE_LIGHTS)
        put_u(&o, (r->lt.reuse_misses + f / 2) / f);
    sep(&o);
    if (r->have & PF_HAVE_LIGHTS)
        put_u(&o, r->lt.fallback);
    sep(&o);
    if (r->have & PF_HAVE_LIGHTS)
        put_u(&o, r->lt.verify_samples);
    sep(&o);
    if (r->have & PF_HAVE_LIGHTS)
        put_u(&o, r->lt.verify_bad);
    sep(&o);
    if (r->have & PF_HAVE_LTIME)
        put_u(&o, r->lt.stale_draws);
    /* ShadowBatchIgnoreLight: merged groups given the game's light
     * environment, and those whose setup call didn't come (whole row). */
    sep(&o);
    if (r->have & PF_HAVE_SBATCH)
        put_u(&o, r->sp.batch_light_fixed);
    sep(&o);
    if (r->have & PF_HAVE_SBATCH)
        put_u(&o, r->sp.batch_light_missed);
    /* LightReuse in extra frames, whole row. */
    sep(&o);
    if (r->have & PF_HAVE_LIGHTS)
        put_u(&o, r->lt.extra_hits);
    sep(&o);
    if (r->have & PF_HAVE_LIGHTS)
        put_u(&o, r->lt.extra_misses);
    {
        unsigned i;
        for (i = 0; i < 4; i++) {
            sep(&o);
            if (r->have & PF_HAVE_FPS) put_u(&o, r->fps_blocked[i]);
        }
        for (i = 0; i < 3; i++) {
            sep(&o);
            if (r->have & PF_HAVE_FPS) put_u(&o, r->fps_probes[i]);
        }
        ms_field(&o, (r->have & PF_HAVE_FPS) != 0, r->fps_learned_us, 1);
    }
    {
        unsigned i;
        int fps = (r->have & PF_HAVE_FPS) != 0;
        int present = (r->have & PF_HAVE_PRESENT) != 0;
        sep(&o);
        if (fps) put_u(&o, r->extra_profile.valid);
        sep(&o);
        if (fps) put_u(&o, r->extra_profile.invalid);
        ms_field(&o, fps && r->extra_profile.valid, r->extra_profile.total_us, 1);
        for (i = 0; i < PF_EP_PASSES; i++) {
            sep(&o);
            if (fps) put_u(&o, r->extra_profile.samples[i]);
            ms_field(&o, fps && r->extra_profile.samples[i], r->extra_profile.us[i], 1);
        }
        sep(&o);
        if (present) put_u(&o, r->presentation.calls);
        sep(&o);
        if (present) put_u(&o, r->presentation.gaps);
        ms_field(&o, present && r->presentation.p50_us, r->presentation.p50_us, 1);
        ms_field(&o, present && r->presentation.p95_us, r->presentation.p95_us, 1);
        for (i = 0; i < PF_PRESENT_BINS; i++) {
            sep(&o);
            if (present) put_u(&o, r->presentation.bins[i]);
        }
    }
    return finish(&o, cap);
}

unsigned pf_format_scope_row(unsigned time_s, const char *label, unsigned calls, unsigned frames,
                             unsigned long long us, char *out, unsigned cap)
{
    out_buf o = { out, cap, 0 };
    unsigned f = frames ? frames : 1;

    if (cap == 0)
        return 0;
    put_u(&o, time_s);
    sep(&o);
    while (*label)
        put_c(&o, *label++);
    sep(&o);
    put_fixed(&o, ((unsigned long long)calls * 100 + f / 2) / f, 2);
    ms_field(&o, 1, us, f);
    return finish(&o, cap);
}
