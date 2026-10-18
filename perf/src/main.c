/* bfme2perf: a Miles plugin (.asi) that mss32.dll loads from the game's mss\
 * folder at AIL_startup. It pins itself so Miles can't unload it, registers
 * no audio provider and installs the timing hooks. */
#include <windows.h>

#include "btnclock.h"
#include "bulk.h"
#include "catchup.h"
#include "config.h"
#include "cusites.h"
#include "fxguard.h"
#include "inst.h"
#include "lights.h"
#include "ltsites.h"
#include "shadowpass.h"
#include "spsites.h"
#include "log.h"
#include "notice.h"
#include "passflag.h"
#include "patch.h"
#include "queueinsert.h"
#include "queuesort.h"
#include "scan.h"
#include "sites.h"
#include "stubs.h"
#include "fps.h"
#include "fpsites.h"
#include "timing.h"
#include "uistate.h"

#define PERF_VERSION "0.9.11-beta.10"

static WCHAR g_log_path[MAX_PATH];

static unsigned ms_since_process_start(void)
{
    FILETIME create, exit_time, kernel, user, now;
    ULARGE_INTEGER a, b;
    if (!GetProcessTimes(GetCurrentProcess(), &create, &exit_time, &kernel, &user))
        return 0;
    GetSystemTimeAsFileTime(&now);
    a.LowPart = create.dwLowDateTime;
    a.HighPart = create.dwHighDateTime;
    b.LowPart = now.dwLowDateTime;
    b.HighPart = now.dwHighDateTime;
    return (unsigned)((b.QuadPart - a.QuadPart) / 10000);
}

/* out = this module's path with its extension replaced by ext. 0 on failure. */
static int sibling_path(HINSTANCE inst, const WCHAR *ext, WCHAR *out)
{
    DWORD n = GetModuleFileNameW(inst, out, MAX_PATH), dot = n, i;
    if (n == 0 || n >= MAX_PATH)
        return 0;
    for (i = n; i > 0 && out[i - 1] != L'\\'; i--) {
        if (out[i - 1] == L'.') {
            dot = i - 1;
            break;
        }
    }
    for (i = 0; ext[i]; i++) {
        if (dot + i >= MAX_PATH - 1)
            return 0;
        out[dot + i] = ext[i];
    }
    out[dot + i] = 0;
    return 1;
}

static void log_module(const WCHAR *name)
{
    WCHAR path[MAX_PATH];
    HMODULE h = GetModuleHandleW(name);
    if (h && GetModuleFileNameW(h, path, MAX_PATH))
        fu_logf("module %ls: %ls", name, path);
    else
        fu_logf("module %ls: not loaded", name);
}

static int all_set(pf_site_mask mask, pf_site_mask bits)
{
    return (mask & bits) == bits;
}

static unsigned g_batch_scan; /* RVA of the FX batch's checked group scan, or 0 */

/* Finds what the shadow-pass fixes need, decides which of their sites to
 * install and logs why one stays off. */
static void plan_shadow_pass(const pf_config *cfg, const pf_image *img,
                             const volatile unsigned char *flag, const char *flag_why,
                             int *enabled)
{
    pf_sp_data d;
    const char *why = pf_sp_find(img, &d);
    int batch = cfg->shadow_batch, stencil = cfg->stencil_flushes,
        particles = cfg->shadow_particles, verify = cfg->shadow_verify;

    g_batch_scan = d.batch_scan;
    if (!batch && !stencil && !particles && !verify)
        return;
    if (!flag) {
        fu_logf("render: %s not found: ShadowBatchIgnoreLight, NoStencilFlushes, "
                "ShadowSkipParticles and ShadowPassVerify stay off",
                flag_why ? flag_why : "the shadow-map flag");
        return;
    }
    if (why) {
        fu_logf("render: %s not found: ShadowSkipParticles%s off", why,
                d.gd ? "" : ", NoStencilFlushes=2 and ShadowPassVerify");
        particles = 0;
        if (!d.device)
            verify = 0;
    }
    pf_sp_init(flag, &d, batch, stencil, particles, verify);
    pf_sp_set_sites(PF_STENCIL_OCCLUDEE, PF_STENCIL_OCCLUDER, PF_STENCIL_OTHER,
                    PF_SHADOW_PARTICLES);
    /* Inert until the effect-file guard's thread proves the effects. A merged
     * group's setup is handed the light environment the game would have
     * stored (fx_group, lenv_set). */
    enabled[PF_SAME_BATCH] = batch;
    if (batch)
        enabled[PF_FX_GROUP] = enabled[PF_LENV_SET] = 1;
    enabled[PF_STENCIL_OCCLUDEE] = enabled[PF_STENCIL_OCCLUDER] = enabled[PF_STENCIL_OTHER] =
        pf_sp_stencil_mode() != 0;
    /* The verify reads the shadow map back where the particle walk starts. */
    enabled[PF_SHADOW_PARTICLES] = pf_sp_particles_mode() != 0 || verify;
    if (verify && d.device)
        enabled[PF_SHADOW] = enabled[PF_SHADOW_INFO] = 1;
}

/* Checks renderOneObject's code before anything is patched and decides which
 * light sites to install; returns 0 if they all stay off. */
static int plan_lights(const pf_config *cfg, const pf_image *img,
                       const volatile unsigned char *flag, int *enabled)
{
    pf_lt_data d;
    const char *why;
    unsigned k;

    if (!cfg->timing && !cfg->light_skip && !cfg->light_cull)
        return 0;
    why = pf_lt_find(img, &d);
    if (why) {
        /* Set_Light_Environment's site may stay: ShadowBatchIgnoreLight uses it. */
        for (k = PF_LIGHT_FIRST; k <= PF_LIGHT_LAST; k++)
            if (k != PF_LENV_SET)
                enabled[k] = 0;
        fu_logf("render: %s doesn't match: LightSkipShadow, LightCull, LightReuse and the "
                "renderOneObject timers stay off",
                why);
        return 0;
    }
    pf_lt_init(&d, flag, cfg->light_skip, cfg->light_cull, cfg->light_reuse, cfg->light_verify);
    enabled[PF_ROO_GLIGHTS] = enabled[PF_ROO_RESET_ND] = enabled[PF_ROO_LISTS] =
        enabled[PF_ROO_ENV] = 1;
    /* LightCull's snapshot is taken once per pass: Customized_Render tells
     * when one starts. */
    if (cfg->light_cull)
        enabled[PF_VIS] = enabled[PF_CUST] = enabled[PF_FLUSH] = 1;
    /* The stale-light count looks at every draw, and asks the device whether
     * it uses fixed-function lighting. */
    if (cfg->timing && cfg->light_skip) {
        pf_sp_data spd;
        pf_sp_find(img, &spd);
        pf_lt_set_device(spd.device);
        enabled[PF_DRAW] = 1;
    }
    return 1;
}

static void log_lights(const pf_config *cfg, pf_site_mask installed)
{
    pf_site_mask cuts = PF_BIT(PF_ROO_GLIGHTS) | PF_BIT(PF_ROO_RESET_ND) |
                        PF_BIT(PF_ROO_LISTS) | PF_BIT(PF_ROO_ENV);
    pf_site_mask timers = PF_BIT(PF_ROO) | PF_BIT(PF_ROO_RESET) | PF_BIT(PF_ROO_ADD_STATIC) |
                          PF_BIT(PF_ROO_ADD_DYN) | PF_BIT(PF_ROO_RENDER) |
                          PF_BIT(PF_ROO_RENDER2) | PF_BIT(PF_LENV_SET);
    pf_lt_set_installed(all_set(installed, cuts), (installed & PF_BIT(PF_CUST)) != 0);
    pf_lt_set_timing(cfg->timing && all_set(installed, cuts | timers), pf_timing_pass);
    pf_sp_set_light_cut(pf_lt_skip_active, pf_lt_skip_off);
    fu_logf("render: light environments: LightSkipShadow=%d (%s), LightCull=%d (%s), "
            "LightReuse=%d (%s), LightVerify=%d%s",
            cfg->light_skip,
            pf_lt_skip_on() ? "no build in the shadow-map pass" : "the game's build",
            cfg->light_cull,
            pf_lt_cull_on() ? "light lists walked once per pass" : "the game's loops",
            cfg->light_reuse,
            pf_lt_reuse_on() ? "matching builds reused" : "off", cfg->light_verify,
            cfg->timing && all_set(installed, cuts | timers)
                ? "; renderOneObject timed by pass (lt_* columns)"
                : "");
}

/* Reading every archive's directory takes about a second: done here, off the
 * loader lock, while the game loads. */
static DWORD WINAPI fxguard_thread(LPVOID unused)
{
    pf_fxguard_result g;
    int proven;
    (void)unused;
    proven = pf_fxguard_check(NULL, GetCommandLineW(), &g);
    pf_sp_set_batch_proven(proven);
    fu_log_event("render: effect files: %u archives read, %u shaders\\compiled\\*.fxo found, %u "
                 "proven; ShadowBatchIgnoreLight %s%s",
                 g.archives, g.effects, g.proven, pf_sp_batch_on() ? "active" : "off: ",
                 pf_sp_batch_on() ? ""
                 : proven         ? "its light environment hooks aren't installed"
                                  : g.why);
    return 0;
}

static void log_shadow_pass(const pf_config *cfg, const pf_image *img, const pf_result *res,
                            pf_site_mask installed)
{
    pf_site_mask stencil = PF_BIT(PF_STENCIL_OCCLUDEE) | PF_BIT(PF_STENCIL_OCCLUDER) |
                       PF_BIT(PF_STENCIL_OTHER);
    if (installed & PF_BIT(PF_SAME_BATCH)) {
        HANDLE t;
        int light = (installed & PF_BIT(PF_FX_GROUP)) && (installed & PF_BIT(PF_LENV_SET)) &&
                    g_batch_scan && res[PF_SAME_BATCH].rva == g_batch_scan + PF_SP_SCAN_SAME &&
                    res[PF_FX_GROUP].rva == g_batch_scan + PF_SP_SCAN_SETUP;
        pf_sp_set_batch_light(light);
        if (!light)
            fu_logf("render: the FX batch's group scan, setup call or Set_Light_Environment "
                    "isn't where it was checked: ShadowBatchIgnoreLight off (a merged group "
                    "couldn't store the game's light environment)");
        pf_sp_set_same_batch(res[PF_SAME_BATCH].trampoline);
        t = CreateThread(NULL, 0, fxguard_thread, NULL, 0, NULL);
        if (t)
            CloseHandle(t);
        else
            fu_logf("render: can't start the effect-file check: ShadowBatchIgnoreLight off");
    } else {
        pf_sp_set_same_batch(0);
    }
    if (installed & PF_BIT(PF_SHADOW_PARTICLES)) {
        unsigned char *const *mgr = pf_sp_forwarder(img, res[PF_SHADOW_PARTICLES].trampoline);
        pf_sp_set_particle_manager(mgr);
        if (!mgr && cfg->shadow_particles)
            fu_logf("render: the particle call doesn't go to the expected forwarder: "
                    "ShadowSkipParticles off");
    } else {
        pf_sp_set_particle_manager(0);
    }
    if ((installed & stencil) != stencil && pf_sp_stencil_mode())
        fu_logf("render: the stencil hooks aren't installed: NoStencilFlushes off");
    if (cfg->shadow_batch || cfg->stencil_flushes || cfg->shadow_particles || cfg->shadow_verify)
        fu_logf("render: shadow pass: ShadowBatchIgnoreLight=%d (%s), NoStencilFlushes=%d (%s), "
                "ShadowSkipParticles=%d (%s), ShadowPassVerify=%d",
                cfg->shadow_batch,
                installed & PF_BIT(PF_SAME_BATCH)
                    ? "groups ignore light and fog colour once the effect files are checked"
                    : "the game's batches",
                cfg->stencil_flushes,
                (installed & stencil) != stencil || !pf_sp_stencil_mode() ? "off"
                : pf_sp_stencil_mode() == 1
                    ? "shadow pass"
                    : "shadow pass, and other passes while shadow mapping is on",
                cfg->shadow_particles,
                !pf_sp_particles_mode() ? "off"
                : pf_sp_particles_mode() == 1 ? "skipped when no smudge can be added"
                                              : "always skipped",
                cfg->shadow_verify);
}

/* After installing: catch-up runs only with all of its hooks in. */
static void setup_catchup(const pf_config *cfg, const pf_cu_sites *cu, pf_site_mask installed,
                          const unsigned char *base)
{
    pf_cu_settings s;
    int ok = all_set(installed, PF_BIT(PF_FRAME) | PF_BIT(PF_FPS_WAIT) | PF_BIT(PF_DISPLAY_DRAW) |
                                    PF_BIT(PF_CU_RENDER));
    int notice = cfg->catchup_notice && cu->device && (installed & PF_BIT(PF_END_SCENE));

    s.enabled = ok;
    s.max_skips = (unsigned)cfg->catchup_max_skips;
    s.notice = notice;
    s.notice_min = (unsigned)cfg->catchup_notice_min;
    pf_notice_text(cfg->catchup_text, s.text, sizeof s.text);
    pf_cu_init(&s, ok ? cu : NULL);
    pf_cu_set_extras(pf_fps_extras_in);
    pf_fps_set_hold(pf_cu_behind);
    if (!ok) {
        fu_logf("render: catch-up: its hooks aren't all installed, renders are never skipped");
        return;
    }
    fu_logf("render: catch-up on: when a game frame would end more than a render's cost behind "
            "the frame limiter's schedule, its render (shadow map, reflection, scene, UI, "
            "present) is skipped, at most %d in a row, and the limiter's next wait ends at the "
            "schedule; logic, network, input, the client, UI and audio updates run every frame. "
            "Never in menus, the shell, loading or pause, while placing a building or "
            "scrolling, or after a frame with extra frames. No-render path at %08X, scene at "
            "%08X, UI animations at %08X",
            cfg->catchup_max_skips, (unsigned)(cu->no_render - base),
            (unsigned)((const unsigned char *)cu->scene - base),
            (unsigned)((const unsigned char *)cu->ui_anim - base));
    if (cu->logic_frame && cu->logic_fps)
        fu_logf("render: catch-up: game speed from TheGameLogic's frame counter (+%X, read "
                "only) at the game's %u logic frames per second (CSV logic_ticks)",
                cu->logic_frame, *cu->logic_fps);
    else
        fu_logf("render: catch-up: TheGameLogic's frame counter wasn't found; game speed is "
                "game frames / 30");
    if (notice)
        fu_logf("render: catch-up notice \"%s\" in the top right corner once %u render(s) were "
                "skipped within %u s, kept while %u are, and for %u s after",
                s.text, s.notice_min, PF_CU_NOTICE_WINDOW_MS / 1000, (s.notice_min + 1) / 2,
                PF_CU_NOTICE_MS / 1000);
    else if (cfg->catchup_notice)
        fu_logf("render: catch-up notice off: %s", cu->device ? "the present hook isn't installed"
                                                              : "the D3D device wasn't found");
}

static void install_hooks(const pf_config *cfg, const WCHAR *csv_path, const WCHAR *scopes_path)
{
    pf_image img;
    pf_result res[PF_SITE_COUNT];
    void *targets[PF_SITE_COUNT];
    int enabled[PF_SITE_COUNT], any = 0;
    unsigned i, btn_data_off = 0;
    pf_site_mask installed = 0;
    const volatile unsigned char *shadow_flag = 0;
    const char *shadow_flag_why = 0, *cu_why = "off";
    pf_cu_sites cu;
    int lights;
    pf_bulk_sites bulk;
    const char *bulk_why = "off";

    if (!pf_image_open(&img, GetModuleHandleW(NULL))) {
        fu_logf("exe : no usable .text section, no hooks");
        return;
    }
    fu_logf("exe : .text crc32 %08X, %u bytes, timestamp %08X",
            pf_crc32(img.base + img.text_rva, img.text_size), img.text_size, img.timestamp);
    /* Discover before mesh, DIP and event hooks alter verified bodies. */
    if (cfg->instancing && cfg->bulk_render)
        bulk_why = pf_bulk_find(&img, &bulk);
    if (cfg->queue_insert) {
        const char *why;
        int applied = pf_queue_insert_install(&img, &why);
        fu_logf("render: QueueInsert=%d: %s", applied, why);
    } else {
        fu_logf("render: QueueInsert=0, original queue insertion");
    }
    if (cfg->queue_sort) {
        const char *why;
        int applied = pf_queue_sort_install(&img, &why);
        fu_logf("render: QueueSort=%d: %s", applied, why);
    } else {
        fu_logf("render: QueueSort=0, original queue sorting");
    }
    for (i = 0; i < PF_SITE_COUNT; i++)
        enabled[i] = cfg->timing && (i != PF_DRAW || cfg->detail);
    enabled[PF_REFLECT] = cfg->reflect_interval > 1;
    enabled[PF_SHADOW] = enabled[PF_SHADOW_INFO] = cfg->shadow_interval > 1;
    enabled[PF_INST_MESH] = enabled[PF_INST_DIP] = cfg->instancing;
    enabled[PF_FPS_WAIT] = enabled[PF_FPS_AVG] = 0;
    enabled[PF_HLOD_SET] = enabled[PF_HLOD_DTOR] = 0;
    enabled[PF_VIEWS] = enabled[PF_PARTICLES] = enabled[PF_SCENE_UPDATE] =
        enabled[PF_DISPLAY_DRAW] = 0;
    enabled[PF_BUTTON_DRAW] = enabled[PF_CLOCK_SET] = enabled[PF_CLOCK_SET_LEFT] = 0;
    enabled[PF_SAME_BATCH] = enabled[PF_STENCIL_OCCLUDEE] = enabled[PF_STENCIL_OCCLUDER] =
        enabled[PF_STENCIL_OTHER] = enabled[PF_SHADOW_PARTICLES] = 0;
    enabled[PF_CU_RENDER] = 0;
    if (cfg->instancing || cfg->shadow_batch || cfg->stencil_flushes || cfg->shadow_particles ||
        cfg->shadow_verify || cfg->light_skip)
        shadow_flag_why = pf_shadow_flag_find(&img, &shadow_flag);
    plan_shadow_pass(cfg, &img, shadow_flag, shadow_flag_why, enabled);
    lights = plan_lights(cfg, &img, shadow_flag, enabled);
    if (lights && cfg->light_skip && !shadow_flag)
        fu_logf("render: %s not found: LightSkipShadow stays off",
                shadow_flag_why ? shadow_flag_why : "the shadow-map flag");
    for (i = 0; i < PF_SITE_COUNT; i++)
        any |= enabled[i];
    if (cfg->max_fps > 30) {
        pf_fps_sites fs;
        const char *why = pf_fps_find(&img, &fs);
        pf_fps_init(why ? NULL : &fs, why, (unsigned)cfg->max_fps, cfg->frame_smoothing, cfg->log);
        if (why) {
            fu_logf("render: MaxFPS=%d: %s not found, the game keeps its own frame rate",
                    cfg->max_fps, why);
        } else {
            /* Extra frames reuse the stored shadow map and reflection, and
             * hold the view update, the particles and the scene's update list. */
            enabled[PF_FPS_WAIT] = enabled[PF_FPS_AVG] = 1;
            enabled[PF_REFLECT] = enabled[PF_SHADOW] = enabled[PF_SHADOW_INFO] = 1;
            enabled[PF_VIEWS] = enabled[PF_PARTICLES] = enabled[PF_SCENE_UPDATE] = 1;
            enabled[PF_HLOD_SET] = enabled[PF_HLOD_DTOR] = cfg->frame_smoothing >= 2;
            /* The game's own draw is timed: what an extra frame not measured
             * yet is expected to cost (FrameSmoothing=3 also lags units there). */
            enabled[PF_DISPLAY_DRAW] = 1;
            why = pf_btn_find(&img, &pf_sites[PF_BUTTON_DRAW], &btn_data_off);
            if (why)
                fu_logf("render: the command button clock draw doesn't match (%s): extra frames "
                        "draw command buttons without their progress clock",
                        why);
            enabled[PF_BUTTON_DRAW] = !why;
            enabled[PF_CLOCK_SET] = enabled[PF_CLOCK_SET_LEFT] = !why;
        }
    }
    if (cfg->catchup) {
        cu_why = pf_cu_find(&img, &cu);
        if (cu_why) {
            fu_logf("render: catch-up: %s not found, renders are never skipped", cu_why);
        } else {
            /* The frame's start and end, the limiter's wait, the game's draw,
             * the render's start; the notice at the present. */
            enabled[PF_FRAME] = enabled[PF_FPS_WAIT] = enabled[PF_FPS_AVG] = 1;
            enabled[PF_DISPLAY_DRAW] = enabled[PF_CU_RENDER] = 1;
            if (cfg->catchup_notice)
                enabled[PF_END_SCENE] = 1;
        }
    }
    if (!bulk_why) {
        /* These identify the render thread and preserve native scope counts,
         * including when CSV output itself is disabled. */
        enabled[PF_FRAME] = enabled[PF_EVENT_BEGIN] = enabled[PF_EVENT_END] =
            enabled[PF_FX_GROUP] = 1;
    }
    any |= enabled[PF_REFLECT] | enabled[PF_SHADOW] | enabled[PF_INST_MESH] | enabled[PF_FPS_WAIT] |
           enabled[PF_SAME_BATCH] | enabled[PF_STENCIL_OCCLUDEE] | enabled[PF_SHADOW_PARTICLES] |
           enabled[PF_CU_RENDER];
    if (!any) {
        fu_logf("remaining hooks: none wanted ([Timing] Enabled=0, ReflectionInterval=1, ShadowInterval=1, "
                "Instancing=0, MaxFPS=30, shadow-pass fixes and light cuts off, catch-up off)");
        return;
    }
    if (!pf_timing_open(pf_sites, cfg->timing ? csv_path : NULL,
                        cfg->timing ? scopes_path : NULL)) {
        fu_logf("timing: can't create %ls, remaining hooks not installed", csv_path);
        return;
    }
    pf_timing_set_detail(cfg->detail);
    pf_timing_set_reflect((unsigned)cfg->reflect_interval);
    pf_timing_set_shadow((unsigned)cfg->shadow_interval);
    if (enabled[PF_SHADOW]) {
        pf_ui_state ui;
        const char *why = pf_ui_find(&img, &ui);
        if (why)
            fu_logf("render: building placement not found (%s): the shadow map stays at the "
                    "interval while placing",
                    why);
        else
            fu_logf("render: TheInGameUI at %08X, pending build +%X, scrolling +%X: the shadow "
                    "map is drawn every frame while placing a building%s",
                    (unsigned)((const unsigned char *)ui.ui - img.base), ui.pending_off,
                    ui.scroll_off, cfg->shadow_scrolling ? " and while the camera scrolls" : "");
        pf_timing_set_ui(&ui, cfg->shadow_scrolling);
    }
    pf_timing_set_sp_verify(cfg->shadow_verify && enabled[PF_SHADOW]);
    if (cfg->instancing) {
        const volatile unsigned char *flag = shadow_flag;
        const char *why = shadow_flag_why;
        pf_inst_init(cfg->instancing_verify);
        pf_inst_set_sites(PF_INST_MESH, PF_INST_DIP);
        pf_inst_set_shadow(flag, cfg->shadow_instancing);
        if (why)
            fu_logf("render: %s not found: the shadow-map pass is told only by its shaders "
                    "(ShadowInstancing=%d)",
                    why, cfg->shadow_instancing);
        else
            fu_logf("render: shadow-map flag at %08X; ShadowInstancing=%d: %s",
                    (unsigned)((const unsigned char *)flag - img.base), cfg->shadow_instancing,
                    cfg->shadow_instancing ? "the shadow map's unit meshes are instanced too"
                                           : "the game draws every mesh of the shadow map");
        pf_timing_set_inst(pf_inst_enter, cfg->timing ? pf_inst_take : 0);
        /* Groups are drawn instanced in extra frames too; inst.c counts and
         * verifies only in the game's own frames. */
        pf_fps_set_extra_listener(pf_inst_set_extra);
    }
    /* The batch test is called for every FX record of every pass: its call
     * goes straight to the plugin's function, without a stub. */
    for (i = 0; i < PF_SITE_COUNT; i++)
        targets[i] = pf_enter_stubs[i];
    targets[PF_SAME_BATCH] = (void *)pf_sp_same_batch;
    pf_install(&img, pf_sites, PF_SITE_COUNT, enabled, cfg->timing ? PF_GROUP_FRAME : 0, targets,
               pf_timing_set_next, res);
    for (i = 0; i < PF_SITE_COUNT; i++) {
        if (res[i].status == PF_ST_APPLIED) {
            installed |= PF_BIT(i);
            fu_logf("hook %s (%s): applied at %08X", pf_sites[i].id, pf_sites[i].function,
                    res[i].rva);
        } else {
            fu_logf("hook %s (%s): %s, %u match(es)", pf_sites[i].id, pf_sites[i].function,
                    pf_status_name(res[i].status), res[i].matches);
        }
    }
    if (cfg->timing && !(installed & PF_BIT(PF_FRAME))) {
        pf_timing_discard(csv_path, scopes_path);
        fu_logf("timing: no hooks installed (the frame hook is required)");
        return;
    }
    if (cfg->instancing && cfg->bulk_render) {
        const pf_site_mask required = PF_BIT(PF_FRAME) | PF_BIT(PF_EVENT_BEGIN) |
            PF_BIT(PF_EVENT_END) | PF_BIT(PF_FX_GROUP) | PF_BIT(PF_INST_MESH) | PF_BIT(PF_INST_DIP);
        int applied = 0;
        if (!bulk_why && !all_set(installed, required))
            bulk_why = "required render/instancing hooks were not installed";
        if (!bulk_why)
            applied = pf_bulk_install(&img, &bulk, &bulk_why);
        fu_logf("render: BulkRender=%d: %s", applied, bulk_why);
    }
    if (cfg->catchup && !cu_why)
        setup_catchup(cfg, &cu, installed, img.base);
    pf_timing_set_installed(installed);
    log_shadow_pass(cfg, &img, res, installed);
    if (lights)
        log_lights(cfg, installed);
    if (cfg->max_fps > 30 && enabled[PF_FPS_WAIT]) {
        if (installed & PF_BIT(PF_SCENE_UPDATE))
            pf_fps_set_scene_skip(img.base + res[PF_SCENE_UPDATE].rva + 17);
        pf_fps_set_installed((installed & PF_BIT(PF_FPS_WAIT)) != 0,
                             (installed & PF_BIT(PF_FPS_AVG)) != 0,
                             all_set(installed, PF_BIT(PF_VIEWS) | PF_BIT(PF_SCENE_UPDATE)),
                             installed & PF_BIT(PF_PARTICLES) ? img.base + res[PF_PARTICLES].rva
                                                            : NULL);
        pf_fps_set_units(all_set(installed, PF_BIT(PF_HLOD_SET) | PF_BIT(PF_HLOD_DTOR)),
                         (installed & PF_BIT(PF_DISPLAY_DRAW)) != 0);
        pf_fps_set_passes(cfg->extra_shadows, cfg->extra_reflections);
        if (installed & PF_BIT(PF_BUTTON_DRAW)) {
            pf_btn_init(btn_data_off);
            if (!pf_fps_stopped())
                fu_logf("render: command button clocks (production, upgrades, recharges) are "
                        "drawn in every extra frame: the requests the control bar made for the "
                        "next draw are set back after each extra frame's push button draws, so "
                        "the game's next draw gets them too (button data at window +%X); the "
                        "control bar's requests are %s",
                        btn_data_off,
                        all_set(installed, PF_BIT(PF_CLOCK_SET) | PF_BIT(PF_CLOCK_SET_LEFT))
                            ? "counted (clock_requests, clocks_noted, clock_unseen)"
                            : "not counted (their hooks aren't installed)");
        } else if (enabled[PF_BUTTON_DRAW] && !pf_fps_stopped()) {
            fu_logf("render: the command button clock hook isn't installed: extra frames draw "
                    "command buttons without their progress clock");
        }
        if (pf_fps_stopped())
            fu_logf("render: MaxFPS=%d: extra frames off (%s)", cfg->max_fps, pf_fps_stopped());
        else
            fu_logf("render: extra frames %s the shadow map (ExtraFrameShadows=%d) and %s the "
                    "water reflection (ExtraFrameReflections=%d)",
                    cfg->extra_shadows ? "redraw" : "reuse the game frame's", cfg->extra_shadows,
                    cfg->extra_reflections ? "redraw" : "reuse the game frame's",
                    cfg->extra_reflections);
        if (!pf_fps_stopped())
            fu_logf("render: MaxFPS=%d: up to %d extra frame(s) per game frame, drawn only in the "
                    "frame limiter's idle time; logic, input and network stay on the game's own "
                    "frames; particles, the view update and the scene's update list (W3D "
                    "emitters, dynamic light fades) stay at the game's rate; "
                    "FrameSmoothing=%d (%s)",
                    cfg->max_fps, cfg->max_fps / 30 - 1, cfg->frame_smoothing,
                    cfg->frame_smoothing <= 1
                        ? "the last frame repeated, the camera as the game has it"
                    : cfg->frame_smoothing == 2
                        ? "units moved ahead, the camera as the game has it"
                        : "units between the last two game frames, the camera as the game "
                          "has it");
        if (!pf_fps_stopped())
            fu_logf("render: an extra frame's expected cost is the slowest of the last 64 game "
                    "frames that drew one, changed only by extra frames drawn or by frames "
                    "where only it kept them out; until measured, and as its floor, %s. Lates "
                    "that persist rest extra frames 1, 2, then 4 minutes; a 4th time turns them "
                    "off for the session",
                    installed & PF_BIT(PF_DISPLAY_DRAW)
                        ? "what the game's own draw costs (display_draw)"
                        : "10 ms (display_draw isn't installed)");
    }
    if (cfg->timing)
        fu_logf("timing: writing %ls (one row per second) and %ls (once a minute)", csv_path,
                scopes_path);
    if (installed & PF_BIT(PF_REFLECT) && cfg->reflect_interval > 1)
        fu_logf("render: the water reflection is redrawn every %dth frame (at most %d frame(s) "
                "old), and at once after a stall",
                cfg->reflect_interval, cfg->reflect_interval - 1);
    else if (cfg->reflect_interval > 1)
        fu_logf("render: the reflection hook isn't installed, the game draws the reflection every "
                "frame");
    if (all_set(installed, PF_BIT(PF_SHADOW) | PF_BIT(PF_SHADOW_INFO)) && cfg->shadow_interval > 1)
        fu_logf("render: the shadow map is redrawn every %dth frame (at most %d frame(s) old, "
                "with its own matrix), and at once after a stall",
                cfg->shadow_interval, cfg->shadow_interval - 1);
    else if (cfg->shadow_interval > 1)
        fu_logf("render: the shadow hooks aren't installed, the game draws the shadow map every "
                "frame");
    if (all_set(installed, PF_BIT(PF_INST_MESH) | PF_BIT(PF_INST_DIP))) {
        fu_logf("render: instancing on for %u unit shader variants (ordinary groups retain their native "
                "leader; BulkRender includes qualified skeletal leaders)%s",
                pf_inst_variant_count(),
                cfg->instancing_verify
                    ? "; verify on: every 5 s a few groups are drawn both ways off screen and "
                      "compared, and a shader variant that differs twice is switched off"
                    : "");
    } else if (cfg->instancing) {
        pf_timing_set_inst(0, 0);
        fu_logf("render: the instancing hooks aren't installed, the game draws every mesh");
    }
}

int __stdcall RIB_Main(unsigned provider, unsigned up_down)
{
    fu_log_event("RIB_Main provider=%X up_down=%u t=%u ms", provider, up_down,
                 ms_since_process_start());
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    WCHAR path[MAX_PATH], exe[MAX_PATH], ini[MAX_PATH], csv[MAX_PATH], scopes[MAX_PATH];
    HMODULE self = NULL;
    pf_config cfg;
    int pinned;

    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    DisableThreadLibraryCalls(inst);
    pinned = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_PIN,
                                (LPCWSTR)inst, &self) != 0;

    if (!sibling_path(inst, L".log", g_log_path) || !sibling_path(inst, L".ini", ini) ||
        !sibling_path(inst, L"_timing.csv", csv) || !sibling_path(inst, L"_scopes.csv", scopes))
        return TRUE;
    pf_config_load(&cfg, ini);
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH))
        exe[0] = 0;
    if (cfg.log)
        fu_log_open(g_log_path);
    fu_logf("bfme2perf " PERF_VERSION);
    if (GetModuleFileNameW(inst, path, MAX_PATH))
        fu_logf("self: %ls", path);
    fu_logf("host: %ls", exe);
    fu_logf("loaded %u ms after process start, pinned=%d", ms_since_process_start(), pinned);
    log_module(L"dinput8.dll");
    log_module(L"d3d9.dll");
    log_module(L"mss32.dll");
    fu_logf("cmd : %ls", GetCommandLineW());
    fu_logf("game: %s", pf_detect_game(GetCommandLineW(), exe));
    fu_logf("ini : %ls (%s)", ini, cfg.ini_found ? "found" : "missing, using defaults");
    fu_logf("settings: [Perf] Enabled=%d Log=%d, [Timing] Enabled=%d Detail=%d, "
            "[Render] ReflectionInterval=%d ShadowInterval=%d ShadowFullWhileScrolling=%d "
            "Batching=%d QueueInsert=%d QueueSort=%d BulkRender=%d Instancing=%d InstancingVerify=%d ShadowInstancing=%d MaxFPS=%d "
            "FrameSmoothing=%d ExtraFrameShadows=%d ExtraFrameReflections=%d "
            "ShadowBatchIgnoreLight=%d NoStencilFlushes=%d ShadowSkipParticles=%d "
            "ShadowPassVerify=%d LightSkipShadow=%d LightCull=%d LightReuse=%d LightVerify=%d, "
            "[CatchUp] Enabled=%d MaxSkips=%d Notice=%d NoticeText=%s NoticeMinSkips=%d, "
            "[Skeleton] Threads=%d Verify=%d (the skeleton pre-pass is not in this build)",
            cfg.enabled, cfg.log, cfg.timing, cfg.detail, cfg.reflect_interval,
            cfg.shadow_interval, cfg.shadow_scrolling, cfg.batching, cfg.queue_insert, cfg.queue_sort, cfg.bulk_render, cfg.instancing,
            cfg.instancing_verify, cfg.shadow_instancing, cfg.max_fps, cfg.frame_smoothing,
            cfg.extra_shadows, cfg.extra_reflections, cfg.shadow_batch, cfg.stencil_flushes,
            cfg.shadow_particles, cfg.shadow_verify, cfg.light_skip, cfg.light_cull,
            cfg.light_reuse, cfg.light_verify, cfg.catchup, cfg.catchup_max_skips,
            cfg.catchup_notice, cfg.catchup_text, cfg.catchup_notice_min, cfg.threads,
            cfg.verify);
    if (!cfg.instancing)
        fu_logf("render: Instancing=0, the game draws every unit mesh (no hook)");
    if (cfg.max_fps_requested != cfg.max_fps)
        fu_logf("render: MaxFPS=%d isn't 30, 60, 90 or 120, using %d", cfg.max_fps_requested,
                cfg.max_fps);
    if (cfg.batching)
        fu_logf("render: Batching=1: the FX group merge is not in this build; with [Timing] "
                "Enabled=1 the CSV counts the groups it could merge (fx_merge)");
    if (cfg.reflect_requested != cfg.reflect_interval)
        fu_logf("render: ReflectionInterval=%d is outside 1..4, using %d", cfg.reflect_requested,
                cfg.reflect_interval);
    if (cfg.shadow_requested != cfg.shadow_interval)
        fu_logf("render: ShadowInterval=%d is outside 1..4, using %d", cfg.shadow_requested,
                cfg.shadow_interval);
    if (cfg.reflect_interval == 1)
        fu_logf("render: ReflectionInterval=1, the water reflection is drawn every frame as in "
                "the game (no hook)");
    if (cfg.shadow_interval == 1)
        fu_logf("render: ShadowInterval=1, the shadow map is drawn every frame as in the game "
                "(no hook)");
    if (cfg.enabled)
        install_hooks(&cfg, csv, scopes);
    else
        fu_logf("master switch off ([Perf] Enabled=0), no hooks");
    /* fu_log_event opens the file per line; the startup handle doesn't share writes. */
    fu_log_close();
    if (cfg.log)
        fu_log_event_path(g_log_path);
    return TRUE;
}
