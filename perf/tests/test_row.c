/* The CSV formatters and the whole-second row clock. */
#include <stdio.h>
#include <string.h>

#include "clock.h"
#include "csvrow.h"

static int g_failures;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            g_failures++;                                 \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

static void commas(char *s, unsigned n)
{
    while (n--)
        strcat(s, ",");
}

int main(void)
{
    char buf[2048], expect[2048], name[64];
    unsigned n, closed = 99;
    pf_clock c;
    pf_row r;

    printf("header\n");
    CHECK(!strcmp(pf_csv_header,
                  "time_s,frames,frame_avg_ms,frame_max_ms,logic_ms,render_ms,skel_render_ms,"
                  "skel_other_ms,anim_on_screen,to_workers,main_thread,skel_us_per_obj,"
                  "vis_ms,cust_ms,flush_ms,present_ms,shadow_ms,reflect_ms,views_ms,ui_ms,"
                  "terrain_ms,water_ms,particles_ms,trees_ms,shrubs_ms,decal_shadows_ms,"
                  "volume_shadows_ms,static_sort_ms,fx_mesh_ms,dx8_mesh_ms,draws,rest_ms,"
                  "fx_batch_ms,reflect_skipped,shadow_skipped,shadow_meshes,reflect_meshes,"
                  "view_meshes,shadow_full_rate,fx_groups,fx_merge,fx_merge_anylight,"
                  "inst_meshes,inst_draws,inst_groups,inst_leaders,inst_small,inst_multipass,"
                  "inst_game_inst,inst_no_shader,inst_disabled,inst_bones,inst_mismatch,"
                  "inst_verify,verify_samples,verify_bad,idle_ms,extra_frames,extra_ms,extra_late,"
                  "extra_units,waits,wait_ms,outside_ms,pump_ms,sleeps,budget_ms,extra_cost_ms,"
                  "extra_max_ms,late_ms,extra_held,extra_shadows,extra_reflections,gap_avg_ms,"
                  "gap_max_ms,disp_err_ms,rate_changes,unit_steps,unit_uneven,inst_bonelist,"
                  "extra_clocks,shadow_inst_meshes,shadow_inst_draws,shadow_inst_groups,shadow_inst_leaders,"
                  "shadow_inst_small,shadow_inst_multipass,shadow_inst_game_inst,"
                  "shadow_inst_no_shader,shadow_inst_disabled,shadow_inst_bones,"
                  "shadow_inst_mismatch,shadow_inst_verify,shadow_verify_samples,"
                  "shadow_verify_bad,shadow_inst_bonelist,shadow_inst_off,shadow_inst_split,"
                  "shadow_inst_ms,clock_requests,clocks_noted,clock_unseen,fx_groups_shadow,"
                  "fx_groups_reflect,fx_groups_view,sb_tests,"
                  "sb_ends_game,sb_ends_now,stencil_skip_shadow,stencil_skip_other,"
                  "particles_skipped,particles_kept,spv_samples,spv_bad,skipped_draws,logic_frames,"
                  "game_speed_pct,behind_ms,behind_max_ms,cu_render_ms,cu_after_ms,waits_cut,"
                  "extra_ref_ms,logic_ticks,"
                  "lt_calls_shadow,lt_ms_shadow,lt_pre_ms_shadow,lt_env_ms_shadow,lt_scene_shadow,"
                  "lt_scene_kept_shadow,lt_dyn_shadow,lt_dyn_kept_shadow,"
                  "lt_calls_reflect,lt_ms_reflect,lt_pre_ms_reflect,lt_env_ms_reflect,"
                  "lt_scene_reflect,lt_scene_kept_reflect,lt_dyn_reflect,lt_dyn_kept_reflect,"
                  "lt_calls_views,lt_ms_views,lt_pre_ms_views,lt_env_ms_views,lt_scene_views,"
                  "lt_scene_kept_views,lt_dyn_views,lt_dyn_kept_views,"
                  "lt_skipped,lt_decided,lt_reuse_hits,lt_reuse_misses,lt_fallback,"
                  "lt_verify_samples,lt_verify_bad,lt_stale_draws,sb_light_fixed,"
                  "sb_light_missed,lt_extra_hits,lt_extra_misses,"
                  "fps_block_cost,fps_block_catchup,fps_block_pause,fps_block_stop,"
                  "fps_probe_attempts,fps_probe_success,fps_probe_late,fps_learned_ms\r\n"),
          "header");
    CHECK(!strcmp(pf_scope_labels[PF_SCOPE_VIEWS], "RenderViews") &&
              !strcmp(pf_scope_labels[PF_SCOPE_FX_BATCH], "RenderFXShaderBatch"),
          "scope labels");

    printf("labels\n");
    CHECK(pf_scope_column("UpdateShadowMap") == PF_SCOPE_SHADOW, "shadow");
    CHECK(pf_scope_column("Rendering mesh\tFXShader\tIUNPIKE") == PF_SCOPE_FX_MESH, "fx mesh");
    CHECK(pf_scope_column("Rendering mesh\tDX8Render\tRJNEWS") == PF_SCOPE_DX8_MESH, "dx8 mesh");
    CHECK(pf_scope_column("RenderFXShaderBatch") == PF_SCOPE_FX_BATCH, "fx batch");
    CHECK(pf_scope_column("RenderRoads") == -1, "no column");
    CHECK(pf_is_mesh_label("Rendering mesh\tFXShader\tX") && !pf_is_mesh_label("RenderViews"),
          "mesh label");
    pf_scope_name("Rendering mesh\tFXShader\tIUNPIKE", name, sizeof name);
    CHECK(!strcmp(name, "Rendering mesh FXShader"), "mesh name %s", name);
    pf_scope_name("a,b\tc", name, sizeof name);
    CHECK(!strcmp(name, "a;b c"), "cleaned name %s", name);
    pf_scope_name("RenderVolumeShadowsAndMoreText", name, 8);
    CHECK(!strcmp(name, "RenderV"), "truncated name %s", name);

    printf("full row\n");
    memset(&r, 0, sizeof r);
    r.time_s = 3;
    r.frames = 30;
    r.frame_us = 500010;
    r.frame_max_us = 25000;
    r.logic_us = 60000;
    r.render_us = 300000;
    r.skel_render_us = 90000;
    r.skel_other_us = 3000;
    r.skel_render_calls = 12000;
    r.vis_us = 15000;
    r.cust_us = 120000;
    r.flush_us = 150000;
    r.present_us = 90000;
    r.scope_us[PF_SCOPE_SHADOW] = 30000;
    r.scope_us[PF_SCOPE_VIEWS] = 240000;
    r.draws = 45000;
    r.rest_us = 30000;
    r.scope_us[PF_SCOPE_FX_BATCH] = 60000;
    r.reflect_skipped = 15;
    r.shadow_skipped = 7;
    r.shadow_meshes = 3000;
    r.view_meshes = 6000;
    r.shadow_full_rate = 4;
    r.fx_groups = 3000;
    r.fx_merge = 900;
    r.fx_merge_anylight = 1500;
    r.inst.meshes = 6000;
    r.inst.draws = 900;
    r.inst.groups = r.inst.leaders = 300;
    r.inst.small = 30;
    r.inst.game_inst = 60;
    r.inst.no_shader = 90;
    r.inst.mismatch = 15;
    r.inst.verify = 30;
    r.inst.verify_samples = 6;
    r.inst.verify_bad = 1;
    r.inst.bonelist = 3000;
    r.inst.shadow[0] = 2400;  /* meshes */
    r.inst.shadow[1] = 300;   /* draws */
    r.inst.shadow[4] = 60;    /* small */
    r.inst.shadow[12] = 4;    /* verify samples: whole row */
    r.inst.shadow[13] = 1;    /* verify bad: whole row */
    r.inst.shadow[14] = 1200; /* bonelist */
    r.inst.shadow_off = 90;
    r.inst.shadow_split = 150;
    r.inst.shadow_inst_us = 45000;
    r.idle_us = 300000;
    r.waits = 30;
    r.extra_frames = 30;
    r.extra_us = 150000;
    r.extra_late = 1;
    r.extra_units = 3000;
    r.wait_us = 600000;
    r.outside_us = 150000;
    r.pump_us = 30000;
    r.sleeps = 900;
    r.budget_us = 540000;
    r.extra_cost_us = 1500;
    r.extra_max_us = 12345;
    r.late_ms = 7;
    r.held = 60;
    r.extra_shadows = 29;
    r.extra_reflections = 3;
    r.gap_us = 999000;
    r.gaps = 120;
    r.gap_max_us = 11500;
    r.disp_err_us = 15000;
    r.rate_changes = 4;
    r.unit_steps = 200;
    r.unit_uneven = 17;
    r.extra_clocks = 5;
    r.clock_requests = 90;
    r.clocks_noted = 60;
    r.clock_unseen = 2;
    r.fx_groups_pass[0] = 1500;
    r.fx_groups_pass[1] = 300;
    r.fx_groups_pass[2] = 1200;
    r.sp.batch_tests = 3000;
    r.sp.batch_split_game = 1500;
    r.sp.batch_split_now = 600;
    r.sp.stencil_shadow = 60;
    r.sp.stencil_other = 90;
    r.sp.particles_skipped = 29;
    r.sp.particles_kept = 1;
    r.sp.verify_samples = 2;
    r.sp.verify_bad = 0;
    r.logic_calls = 27;
    r.cu.skipped = 5;
    r.cu.tracked = 30;
    r.cu.behind_ms = 300;
    r.cu.behind_max_ms = 25;
    r.cu.render_us = 21000;
    r.cu.after_us = 3500;
    r.cu.waits_cut = 4;
    r.extra_ref_us = 6500;
    r.cu.logic_ticks = 5;
    r.cu.ticks_on = 1;
    r.sp.batch_light_fixed = 45;
    r.sp.batch_light_missed = 0;
    r.lt.calls[0] = 300;
    r.lt.us[0] = 60000;
    r.lt.pre_us[0] = 30000;
    r.lt.env_us[0] = 15000;
    r.lt.scene_tested[0] = 600;
    r.lt.scene_kept[0] = 90;
    r.lt.dyn_tested[0] = 1200;
    r.lt.dyn_kept[0] = 150;
    r.lt.calls[2] = 900;
    r.lt.us[2] = 120000;
    r.lt.pre_us[2] = 90000;
    r.lt.env_us[2] = 30000;
    r.lt.scene_tested[2] = 1800;
    r.lt.scene_kept[2] = 300;
    r.lt.dyn_tested[2] = 3600;
    r.lt.dyn_kept[2] = 600;
    r.lt.skipped = 300;
    r.lt.decided = 900;
    r.lt.reuse_hits = 450;
    r.lt.reuse_misses = 150;
    r.lt.fallback = 2;
    r.lt.verify_samples = 8;
    r.lt.extra_hits = 40;
    r.lt.extra_misses = 3;
    r.have = 524287 | PF_HAVE_CATCHUP;
    strcpy(expect, "3,30,16.667,25.000,2.000,10.000,3.000,0.100,400,0,0,7.50,"
                   "0.500,4.000,5.000,3.000,1.000,0.000,8.000");
    for (n = 0; n < 11; n++)
        strcat(expect, ",0.000");
    strcat(expect, ",1500,1.000,2.000,15,7,100,0,200,4,100,30,50");
    strcat(expect, ",200,30,10,10,1,0,2,3,0,0,1,1,6,1");
    strcat(expect, ",10.000,30,5.000,1,100,30,20.000,5.000,1.000,900,18.000,1.500,12.345,7,60,29,3,8.325,11.500,0.500,4,200,17,100,5");
    strcat(expect, ",80,10,0,0,2,0,0,0,0,0,0,0,4,1,40,3,5,1.500,90,60,2");
    strcat(expect, ",50,10,40,100,50,20,2,3,29,1,2,0");
    /* The speed is game frames / 30 (30 here), not the 27 logic calls. */
    strcat(expect, ",5,27,100.0,10.000,25,21.000,3.500,4,6.500,5");
    strcat(expect, ",10,2.000,1.000,0.500,20,3,40,5,0,0.000,0.000,0.000,0,0,0,0"
                   ",30,4.000,3.000,1.000,60,10,120,20,10,30,15,5,2,8,0,0,45,0,40,3,0,0,0,0,0,0,0,0.000\r\n");
    n = pf_format_row(&r, buf, sizeof buf);
    CHECK(n == strlen(buf) && !strcmp(buf, expect), "full row:\n%s%s", buf, expect);
    r.fps_blocked[0] = 12; r.fps_blocked[1] = 3;
    r.fps_blocked[2] = 4; r.fps_blocked[3] = 5;
    r.fps_probes[0] = 2; r.fps_probes[1] = 1; r.fps_probes[2] = 1;
    r.fps_learned_us = 30614;
    pf_format_row(&r, buf, sizeof buf);
    CHECK(strstr(buf, ",12,3,4,5,2,1,1,30.614\r\n"), "recovery diagnostics: %s", buf);
    r.extra_ref_us = 0;
    r.cu.ticks_on = 0;
    n = pf_format_row(&r, buf, sizeof buf);
    CHECK(n && strstr(buf, ",3.500,4,,,10,2.000,"),
          "no draw timed, no logic frame counter: both columns empty:\n%s", buf);

    printf("frame only\n");
    memset(&r, 0, sizeof r);
    r.time_s = 7;
    r.frames = 30;
    r.frame_us = 300000;
    r.frame_max_us = 12345;
    r.rest_us = 300000;
    n = pf_format_row(&r, buf, sizeof buf);
    strcpy(expect, "7,30,10.000,12.345,,,,,,0,0,");
    commas(expect, 20);
    strcat(expect, "10.000,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,");
    commas(expect, 21 + 12 + 8 + 2 + 32 + 2 + 2 + 8);
    strcat(expect, "\r\n");
    CHECK(n && !strcmp(buf, expect), "frame only:\n%s%s", buf, expect);

    printf("skeleton without calls\n");
    r.have = PF_HAVE_SKEL;
    r.rest_us = 0;
    n = pf_format_row(&r, buf, sizeof buf);
    strcpy(expect, "7,30,10.000,12.345,,,0.000,0.000,0,0,0,");
    commas(expect, 19);
    strcat(expect, ",0.000,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,");
    commas(expect, 21 + 12 + 8 + 2 + 32 + 2 + 2 + 8);
    strcat(expect, "\r\n");
    CHECK(n && !strcmp(buf, expect), "no skeleton calls:\n%s%s", buf, expect);
    CHECK(pf_format_row(&r, buf, 10) == 0, "too small a buffer is refused");

    printf("scope row\n");
    n = pf_format_scope_row(60, "RenderViews", 45, 30, 240000, buf, sizeof buf);
    CHECK(n && !strcmp(buf, "60,RenderViews,1.50,8.000\r\n"), "scope row: %s", buf);
    CHECK(!strcmp(pf_scope_header, "time_s,scope,calls_per_frame,ms_per_frame\r\n"),
          "scope header");

    printf("clock\n");
    pf_clock_start(&c, 500, 1000);
    CHECK(!pf_clock_tick(&c, 1400, &closed), "1400 inside second 0");
    CHECK(pf_clock_tick(&c, 1500, &closed) && closed == 0 && c.row == 1, "1500 closes 0");
    CHECK(!pf_clock_tick(&c, 2499, &closed), "2499 inside second 1");
    CHECK(pf_clock_tick(&c, 2500, &closed) && closed == 1 && c.row == 2, "2500 closes 1");
    CHECK(pf_clock_tick(&c, 5200, &closed) && closed == 2 && c.row == 4,
          "a long frame closes 2, opens 4");
    CHECK(!pf_clock_tick(&c, 5499, &closed), "5499 inside second 4");
    CHECK(pf_clock_tick(&c, 5500, &closed) && closed == 4 && c.row == 5, "5500 closes 4");

    if (g_failures) {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("OK: row tests passed\n");
    return 0;
}
