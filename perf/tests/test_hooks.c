/* Installs the timing hooks on small assembly functions in this exe
 * (prologues shaped like the game's), calls them and checks the return
 * values, registers, call counts, markers, draws, present and the CSVs. */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#include "btnclock.h"
#include "fps.h"
#include "lights.h"
#include "patch.h"
#include "presentgap.h"
#include "shadowpass.h"
#include "stubs.h"
#include "timing.h"
#include "uistate.h"

/* flushOccludedObjectsIntoStencil's occludee test: ECX holds the template's
 * shadow type (here SHADOW_VOLUME_NON_SELF_1 in 0xF9), `and ecx, 0x380`
 * keeps the NON_SELF bits; returns them. */
__asm__(".text\n"
        ".p2align 4\n"
        "_fake_stencil:\n"
        "    movl $0x5EED00F9, %ecx\n"
        "    andl $0x380, %ecx\n"
        "    movl %ecx, %eax\n"
        "    ret\n");
extern char fake_stencil[];

/* renderOneObject's shape: four stack arguments, `ret $16`; returns their
 * weighted sum. And the global lights loop: 1 when it runs, 2 when the
 * stub sends it on to the skip's continuation. */
__asm__(".text\n"
        ".p2align 4\n"
        "_fake_roo:\n"
        "    movl $0x5EED00FB, %eax\n"
        "    movl 4(%esp), %eax\n"
        "    addl 8(%esp), %eax\n"
        "    addl 12(%esp), %eax\n"
        "    addl 12(%esp), %eax\n"
        "    addl 16(%esp), %eax\n"
        "    ret $16\n"
        ".p2align 4\n"
        "_fake_glights:\n"
        "    movl $0x5EED00FD, %ecx\n"
        "    movl $1, %eax\n"
        "    ret\n"
        "_fake_glights_skip:\n"
        "    movl $2, %eax\n"
        "    ret\n");
extern char fake_roo[], fake_glights[], fake_glights_skip[];
typedef int(__stdcall *roo_fn)(int, int, int, int);

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

const char g_label_views[] = "RenderViews";
const char g_label_mesh[] = "Rendering mesh\tFXShader\tTEST";
char g_marker[16], g_marker2[16];
int g_reflect_runs, g_shadow_runs, g_frame_no, g_seen_matrix0, g_sleeps, g_particle_steps,
    g_display_draws, g_clocks_drawn, g_clocks_check_like;
/* What the fake W3DDisplay::draw does after its particle update (NULL: nothing). */
void (*g_draw_inner)(void);
extern char fake_sleep[];
extern char fake_present[];
void *g_fake_import = fake_sleep;

/* frame: shadow(3); logic(7); marker("RenderViews") { render(9); mesh
 * marker { draw; draw } }; anim; reflect(1, 2); set_info(0, matrix whose
 * first dword is the frame number, 0); present(1). render calls hlod, hlod
 * calls anim. Each marker `mov eax, 0x5EED00nn` makes its signature unique. */
__asm__(".text\n"
        ".p2align 4\n"
        "_fake_frame:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    pushl %ecx\n"
        "    pushl %ebx\n"
        "    movl $0x5EED0001, %eax\n"
        "    incl _g_frame_no\n"
        "    pushl $3\n"
        "    movl $0x5EED00DD, %eax\n"
        "    call _fake_shadow\n"
        "    pushl $7\n"
        "    call _fake_logic\n"
        "    pushl $0\n"
        "    pushl $0\n"
        "    pushl $_g_label_views\n"
        "    movl $_g_marker, %ecx\n"
        "    call _fake_event_begin\n"
        "    pushl $9\n"
        "    call _fake_render\n"
        "    pushl $0\n"
        "    pushl $0\n"
        "    pushl $_g_label_mesh\n"
        "    movl $_g_marker2, %ecx\n"
        "    call _fake_event_begin\n"
        "    call _fake_draw\n"
        "    call _fake_draw\n"
        "    movl $_g_marker2, %ecx\n"
        "    call _fake_event_end\n"
        "    movl $_g_marker, %ecx\n"
        "    call _fake_event_end\n"
        "    call _fake_anim\n"
        "    movl $0x5EED00CC, %eax\n"
        "    pushl $2\n"
        "    pushl $1\n"
        "    call _fake_reflect\n"
        "    subl $64, %esp\n"
        "    movl _g_frame_no, %eax\n"
        "    movl %eax, (%esp)\n"
        "    movl %esp, %edx\n"
        "    pushl $0\n"
        "    pushl %edx\n"
        "    pushl $0\n"
        "    movl $0x5EED00EF, %eax\n"
        "    call _fake_set_info\n"
        "    addl $64, %esp\n"
        "    pushl $1\n"
        "    call _fake_present\n"
        "    addl $4, %esp\n"
        "    movl $41, %eax\n"
        "    popl %ebx\n"
        "    popl %ecx\n"
        "    popl %ebp\n"
        "    ret\n"
        ".p2align 4\n"
        "_fake_logic:\n"
        "    movl $0x5EED0002, %eax\n"
        "    movl 4(%esp), %eax\n"
        "    addl $1, %eax\n"
        "    ret $4\n"
        ".p2align 4\n"
        "_fake_render:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    subl $0x28, %esp\n"
        "    movl $0x5EED0003, %eax\n"
        "    call _fake_hlod\n"
        "    leave\n"
        "    ret $4\n"
        ".p2align 4\n"
        "_fake_hlod:\n"
        "    subl $0x4C, %esp\n"
        "    pushl %ebx\n"
        "    pushl %ebp\n"
        "    movl $0x5EED0004, %eax\n"
        "    call _fake_anim\n"
        "    popl %ebp\n"
        "    popl %ebx\n"
        "    addl $0x4C, %esp\n"
        "    ret\n"
        ".p2align 4\n"
        "_fake_anim:\n"
        "    subl $0x30, %esp\n"
        "    pushl %esi\n"
        "    movl %ecx, %esi\n"
        "    movl $0x5EED0005, %eax\n"
        "    popl %esi\n"
        "    addl $0x30, %esp\n"
        "    ret\n"
        ".p2align 4\n"
        "_fake_reflect:\n"
        "    incl _g_reflect_runs\n"
        "    movl $0x5EED000D, %eax\n"
        "    ret $8\n"
        ".p2align 4\n"
        "_fake_shadow:\n"
        "    incl _g_shadow_runs\n"
        "    ret $4\n"
        ".p2align 4\n"
        "_fake_set_info:\n"
        "    movl 8(%esp), %eax\n"
        "    movl (%eax), %eax\n"
        "    movl %eax, _g_seen_matrix0\n"
        "    ret $12\n"
        /* cdecl (end, record, mesh, method), shaped like the batch's
         * per-group setup call: EBX = record, ECX = mesh, ESI = method,
         * [EBP+8] = the group's end. */
        ".p2align 4\n"
        "_fake_group:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    pushl %ebx\n"
        "    pushl %esi\n"
        "    movl 12(%ebp), %ebx\n"
        "    movl 16(%ebp), %ecx\n"
        "    movl 20(%ebp), %esi\n"
        "    movl $0x5EED00F1, %eax\n"
        "    call _fake_group_setup\n"
        "    popl %esi\n"
        "    popl %ebx\n"
        "    popl %ebp\n"
        "    ret\n"
        ".p2align 4\n"
        "_fake_group_setup:\n"
        "    ret\n"
        /* cdecl (object): `call [edx+0x148]` on the object's vtable with
         * (object, 5), like BfmeDrawOps::Draw's DrawIndexedPrimitive call. */
        ".p2align 4\n"
        "_fake_dip_caller:\n"
        "    pushl %ebx\n"
        "    movl 8(%esp), %eax\n"
        "    movl (%eax), %edx\n"
        "    pushl $5\n"
        "    pushl %eax\n"
        "    movl $0x5EED00F2, %ecx\n"
        "    .byte 0xFF, 0x92\n"
        "    .long 0x148\n"
        "    popl %ebx\n"
        "    ret\n"
        /* cdecl (mesh): thiscall fake_mesh_render(mesh, 0, 1), like the
         * batch's per-mesh call; the callee returns ECX + its second argument. */
        ".p2align 4\n"
        "_fake_mesh_caller:\n"
        "    movl 4(%esp), %ecx\n"
        "    pushl $1\n"
        "    pushl $0\n"
        "    movl $0x5EED00F3, %eax\n"
        "    call _fake_mesh_render\n"
        "    ret\n"
        ".p2align 4\n"
        "_fake_mesh_render:\n"
        "    movl %ecx, %eax\n"
        "    addl 8(%esp), %eax\n"
        "    ret $8\n"
        /* The limiter's `push ebx; call [Sleep]`, through a fake import slot. */
        ".p2align 4\n"
        "_fake_wait:\n"
        "    movl $0x5EED00F7, %eax\n"
        "    pushl $0\n"
        "    call *_g_fake_import\n"
        "    ret\n"
        ".p2align 4\n"
        "_fake_sleep:\n"
        "    incl _g_sleeps\n"
        "    ret $4\n"
        /* cdecl (object): thiscall object->vtable[0xA0](), through `call [eax+0xA0]`
         * like W3DDisplay's updateViews call. */
        ".p2align 4\n"
        "_fake_views_caller:\n"
        "    pushl %esi\n"
        "    movl 8(%esp), %esi\n"
        "    movl (%esi), %eax\n"
        "    movl %esi, %ecx\n"
        "    movl $0x5EED00F4, %edx\n"
        "    .byte 0xFF, 0x90\n"
        "    .long 0xA0\n"
        "    popl %esi\n"
        "    ret\n"
        /* Shaped like the particle manager's update and W3DDisplay::draw:
         * `mov eax, imm32` first. */
        ".p2align 4\n"
        "_fake_particles:\n"
        "    movl $0x5EED00F5, %eax\n"
        "    incl _g_particle_steps\n"
        "    ret\n"
        /* Customized_Render's update loop at one node (cdecl node -> next
         * node): load the object, call its On_Frame_Update (vtable +0x34),
         * take the next node 17 bytes after the site. */
        ".p2align 4\n"
        "_fake_scene_update:\n"
        "    pushl %ebx\n"
        "    movl 8(%esp), %ebx\n"
        "    movl $0x5EED00FB, %eax\n"
        "    .byte 0x8B, 0x4B, 0x0C, 0x8D, 0x41, 0xF8, 0xF7, 0xD9, 0x1B, 0xC9, 0x23, 0xC8\n"
        "    .byte 0x8B, 0x01, 0xFF, 0x50, 0x34, 0x8B, 0x5B, 0x04\n"
        "    movl %ebx, %eax\n"
        "    popl %ebx\n"
        "    ret\n"
        ".p2align 4\n"
        "_fake_display_draw:\n"
        "    movl $0x5EED00F6, %eax\n"
        "    call _fake_particles\n"
        "    movl _g_draw_inner, %eax\n"
        "    testl %eax, %eax\n"
        "    jz 1f\n"
        "    call *%eax\n"
        "1:  incl _g_display_draws\n"
        "    movl $0x5EED00F6, %eax\n"
        "    ret\n"
        /* cdecl (window, instance): like W3DGadgetPushButtonDraw, sends a
         * check-like button (data +0x20) to its own draw
         * and the others to the image draw; each draws the clock the button
         * data (window +0x2C) asks for and clears the request. */
        ".p2align 4\n"
        "_fake_button_draw:\n"
        "    movl $0x5EED00F8, %eax\n"
        "    movl 4(%esp), %ecx\n"
        "    movl 0x2C(%ecx), %edx\n"
        "    testl %edx, %edx\n"
        "    jz 2f\n"
        "    cmpb $0, 0x20(%edx)\n"
        "    je 1f\n"
        "    jmp _fake_check_like_draw\n"
        "1:  jmp _fake_image_draw\n"
        "2:  ret\n"
        ".p2align 4\n"
        "_fake_check_like_draw:\n"
        "    movl 4(%esp), %ecx\n"
        "    movl 0x2C(%ecx), %ecx\n"
        "    cmpb $0, (%ecx)\n"
        "    je 1f\n"
        "    incl _g_clocks_check_like\n"
        "    movb $0, (%ecx)\n"
        "1:  ret\n"
        ".p2align 4\n"
        "_fake_image_draw:\n"
        "    movl 4(%esp), %ecx\n"
        "    movl 0x2C(%ecx), %ecx\n"
        "    cmpb $0, (%ecx)\n"
        "    je 1f\n"
        "    incl _g_clocks_drawn\n"
        "    movb $0, (%ecx)\n"
        "1:  ret\n"
        /* cdecl (window, percent, color): the control bar's clock requests. */
        ".p2align 4\n"
        "_fake_clock_set:\n"
        "    movl $0x5EED00F9, %eax\n"
        "    movl 4(%esp), %ecx\n"
        "    movl 0x2C(%ecx), %ecx\n"
        "    movl 8(%esp), %eax\n"
        "    movl %eax, 4(%ecx)\n"
        "    movl 12(%esp), %eax\n"
        "    movl %eax, 8(%ecx)\n"
        "    movb $1, (%ecx)\n"
        "    ret\n"
        ".p2align 4\n"
        "_fake_clock_set_left:\n"
        "    movl $0x5EED00FA, %eax\n"
        "    movl 4(%esp), %ecx\n"
        "    movl 0x2C(%ecx), %ecx\n"
        "    movl 8(%esp), %eax\n"
        "    movl %eax, 4(%ecx)\n"
        "    movl 12(%esp), %eax\n"
        "    movl %eax, 8(%ecx)\n"
        "    movb $2, (%ecx)\n"
        "    ret\n"
        ".p2align 4\n"
        "_fake_present:\n"
        "    movl %fs:0, %eax\n"
        "    movl $0x5EED0008, %eax\n"
        "    ret\n"
        ".p2align 4\n"
        "_fake_event_begin:\n"
        "    movl 4(%esp), %eax\n"
        "    testl %eax, %eax\n"
        "    movl $0x5EED0009, %eax\n"
        "    movl %ecx, %eax\n"
        "    ret $12\n"
        /* The destructor is a lone jmp rel32, found from the bytes before it. */
        ".p2align 4\n"
        "_fake_end_ctx:\n"
        "    movl $0x5EED00EE, %eax\n"
        "    ret\n"
        "_fake_event_end:\n"
        "    .byte 0xE9\n"
        "    .long _fake_event_end_body - . - 4\n"
        "_fake_event_end_body:\n"
        "    movl $0x5EED000A, %eax\n"
        "    ret\n"
        ".p2align 4\n"
        "_fake_draw:\n"
        "    movl _g_marker, %eax\n"
        "    movl $0x5EED000B, %eax\n"
        "    ret\n"
        /* 0 if ebx/esi/edi/ebp and anim's eax survive a wrapped call. */
        ".p2align 4\n"
        "_check_regs:\n"
        "    pushl %ebx\n"
        "    pushl %esi\n"
        "    pushl %edi\n"
        "    pushl %ebp\n"
        "    movl $0x11111111, %ebx\n"
        "    movl $0x22222222, %esi\n"
        "    movl $0x33333333, %edi\n"
        "    movl $0x44444444, %ebp\n"
        "    call _fake_anim\n"
        "    cmpl $0x5EED0005, %eax\n"
        "    jne 1f\n"
        "    cmpl $0x11111111, %ebx\n"
        "    jne 1f\n"
        "    cmpl $0x22222222, %esi\n"
        "    jne 1f\n"
        "    cmpl $0x33333333, %edi\n"
        "    jne 1f\n"
        "    cmpl $0x44444444, %ebp\n"
        "    jne 1f\n"
        "    xorl %eax, %eax\n"
        "    jmp 2f\n"
        "1:  movl $1, %eax\n"
        "2:  popl %ebp\n"
        "    popl %edi\n"
        "    popl %esi\n"
        "    popl %ebx\n"
        "    ret\n");

/* Assembly labels, called through typed pointers: a __stdcall prototype
 * would link against the decorated _fake_logic@4. */
extern char fake_frame[], fake_logic[], fake_anim[], check_regs[], fake_group[];
extern char fake_event_begin[], fake_event_end[];
typedef void (*group_fn)(const void *end, const void *record, const void *mesh, const void *method);

/* A batch test on the light environments' values only. */
static unsigned char __attribute__((thiscall)) same_env(void *self, void *other)
{
    return **(unsigned **)((unsigned char *)self + PF_MESH_LIGHT_ENV) ==
           **(unsigned **)((unsigned char *)other + PF_MESH_LIGHT_ENV);
}
typedef void(__attribute__((thiscall)) * begin_fn)(void *self, const char *label, int a, int b);
typedef void(__attribute__((thiscall)) * end_fn)(void *self);
typedef int (*int_fn)(void);
typedef int(__stdcall *logic_fn)(int);

static int call_frame(void) { return ((int_fn)(void *)fake_frame)(); }
static int call_logic(int x) { return ((logic_fn)(void *)fake_logic)(x); }
static int call_anim(void) { return ((int_fn)(void *)fake_anim)(); }
static int call_check_regs(void) { return ((int_fn)(void *)check_regs)(); }

static const pf_site g_sites[PF_SITE_COUNT] = {
    { "frame", "fake_frame", "55 89 E5 51 53 B8 01 00 ED 5E", 0, 5, 0, PF_GROUP_FRAME, 0, 0, 0 },
    { "logic", "fake_logic", "B8 02 00 ED 5E 8B 44 24 04", 0, 5, 4, PF_GROUP_LOGIC, 0, 0, 0 },
    { "render", "fake_render", "55 89 E5 83 EC 28 B8 03 00 ED 5E", 0, 6, 4, PF_GROUP_RENDER, 0, 0,
      0 },
    { "skel_hlod", "fake_hlod", "83 EC 4C 53 55 B8 04 00 ED 5E", 0, 5, 0, PF_GROUP_SKEL, 0, 0, 0 },
    { "skel_anim", "fake_anim", "83 EC 30 56 89 CE B8 05 00 ED 5E", 0, 6, 0, PF_GROUP_SKEL, 0, 0,
      0 },
    { "vis", "unused", "CC", 0, 5, 4, PF_GROUP_SCENE, 0, 0, 0 },
    { "cust", "unused", "CC", 0, 5, 4, PF_GROUP_SCENE, 0, 0, 0 },
    { "flush", "unused", "CC", 0, 5, 4, PF_GROUP_SCENE, 0, 0, 0 },
    { "present", "fake_present", "64 A1 00 00 00 00 B8 08 00 ED 5E", 0, 6, 0, PF_GROUP_PRESENT, 0,
      0, 0 },
    { "event_begin", "fake_event_begin", "8B 44 24 04 85 C0 B8 09 00 ED 5E", 0, 6, 12,
      PF_GROUP_EVENTS, 1, 0, 0 },
    { "event_end", "fake_event_end", "B8 EE 00 ED 5E C3 E9 ?? ?? ?? ??", 6, 5, 0, PF_GROUP_EVENTS,
      1, 0, 0 },
    { "draw", "fake_draw", "A1 ?? ?? ?? ?? B8 0B 00 ED 5E", 0, 5, 0, PF_GROUP_DRAWS, 1, 0, 0 },
    { "reflect", "fake_reflect call", "B8 CC 00 ED 5E 6A 02 6A 01 E8 ?? ?? ?? ??", 9, 5, 8,
      PF_GROUP_REFLECT, 1, 1, 0 },
    { "shadow", "fake_shadow call", "B8 DD 00 ED 5E E8 ?? ?? ?? ??", 5, 5, 4, PF_GROUP_SHADOW, 1,
      1, 0 },
    { "shadow_info", "fake_set_info call", "B8 EF 00 ED 5E E8 ?? ?? ?? ??", 5, 5, 12,
      PF_GROUP_SHADOW, 1, 1, 0 },
    { "fx_group", "fake_group setup call", "B8 F1 00 ED 5E E8 ?? ?? ?? ??", 5, 5, 0,
      PF_GROUP_BATCH, 1, 1, 0 },
    { "inst_mesh", "fake_mesh_render call", "B8 F3 00 ED 5E E8 ?? ?? ?? ??", 5, 5, 8,
      PF_GROUP_INST, 1, 1, 0 },
    { "inst_dip", "fake_dip_caller indirect call", "B9 F2 00 ED 5E FF 92 48 01 00 00", 5, 6, 8,
      PF_GROUP_INST, 1, 2, 0 },
    { "fps_wait", "fake_wait's call [import]", "B8 F7 00 ED 5E 6A 00 FF 15 ?? ?? ?? ??", 7, 6, 4,
      PF_GROUP_FPS, 1, PF_CALL_INDIRECT, 0 },
    { "fps_avg", "unused", "CC", 0, 5, 0, PF_GROUP_SCENE, 1, 1, 0 },
    { "hlod_set", "unused", "CC", 0, 5, 4, PF_GROUP_UNITS, 1, 0, 0 },
    { "hlod_dtor", "unused", "CC", 0, 7, 0, PF_GROUP_UNITS, 1, 0, 0 },
    { "pump", "unused", "CC", 0, 5, 0, PF_GROUP_PUMP, 0, 0, 0 },
    { "views", "fake_views_caller indirect call", "BA F4 00 ED 5E FF 90 A0 00 00 00", 5, 6, 0,
      PF_GROUP_HOLD, 1, 2, 0 },
    { "particles", "fake_particles", "B8 F5 00 ED 5E FF 05", 0, 5, 0, PF_GROUP_HOLD, 1, 0, 0 },
    { "display_draw", "fake_display_draw", "B8 F6 00 ED 5E E8", 0, 5, 0, PF_GROUP_LAG, 0, 0, 0 },
    { "button_draw", "fake_button_draw", "B8 F8 00 ED 5E 8B 4C 24 04", 0, 5, 0, PF_GROUP_BUTTONS,
      1, 0, 0 },
    { "clock_set", "fake_clock_set", "B8 F9 00 ED 5E 8B 4C 24 04", 0, 5, 0, PF_GROUP_CLOCKS, 1, 0,
      0 },
    { "clock_set_left", "fake_clock_set_left", "B8 FA 00 ED 5E 8B 4C 24 04", 0, 5, 0,
      PF_GROUP_CLOCKS, 1, 0, 0 },
    { "same_batch", "unused", "CC", 0, 5, 4, PF_GROUP_SBATCH, 1, 1, 0 },
    { "stencil_occludee", "fake_stencil's and ecx, 0x380", "B9 F9 00 ED 5E 81 E1 80 03 00 00", 5,
      6, 0, PF_GROUP_STENCIL, 1, 0, 0 },
    /* Not installed here: outside the stencil group so it installs alone. */
    { "stencil_occluder", "unused", "CC", 0, 6, 0, PF_GROUP_COUNT, 1, 0, 0 },
    { "stencil_other", "unused", "CC", 0, 6, 0, PF_GROUP_COUNT, 1, 0, 0 },
    { "shadow_particles", "unused", "CC", 0, 5, 0, PF_GROUP_SPART, 1, 1, 0 },
    { "catchup_render", "unused", "CC", 0, 5, 0, PF_GROUP_CATCHUP, 1, 0, 0 },
    { "scene_update", "fake_scene_update's object load", "B8 FB 00 ED 5E 8B 4B 0C 8D 41 F8", 5, 6,
      0, PF_GROUP_HOLD, 1, 0, 0 },
    /* The light sites: the wrap and the global lights skip; the rest aren't
     * installed (outside the groups so these two install alone). */
    { "roo", "fake_roo", "B8 FB 00 ED 5E 8B 44 24 04", 0, 5, 16, PF_GROUP_LTIME, 0, 0, 0 },
    { "roo_reset", "unused", "CC", 0, 7, 0, PF_GROUP_COUNT, 1, 0, 0 },
    { "roo_glights", "fake_glights", "B9 FD 00 ED 5E B8 01 00 00 00", 0, 5, 0, PF_GROUP_LIGHTS, 1,
      0, 0 },
    { "roo_reset_nd", "unused", "CC", 0, 7, 0, PF_GROUP_COUNT, 1, 0, 0 },
    { "roo_lists", "unused", "CC", 0, 6, 0, PF_GROUP_COUNT, 1, 0, 0 },
    { "roo_add_static", "unused", "CC", 0, 7, 0, PF_GROUP_COUNT, 1, 0, 0 },
    { "roo_add_dyn", "unused", "CC", 0, 7, 0, PF_GROUP_COUNT, 1, 0, 0 },
    { "roo_env", "unused", "CC", 0, 6, 0, PF_GROUP_COUNT, 1, 0, 0 },
    { "roo_render", "unused", "CC", 0, 5, 0, PF_GROUP_COUNT, 1, 0, 0 },
    { "roo_render2", "unused", "CC", 0, 5, 0, PF_GROUP_COUNT, 1, 0, 0 },
    { "lenv_set", "unused", "CC", 0, 9, 0, PF_GROUP_COUNT, 1, 0, 0 },
};

extern char fake_dip_caller[], fake_mesh_caller[], fake_mesh_render[];
typedef int (*obj_fn)(void *obj);
static int __stdcall triple(void *self, int x)
{
    (void)self;
    return 3 * x;
}
/* thiscall, as the views slot is called: 21 if `this` arrived. */
static int __attribute__((thiscall)) triple_this(void *self)
{
    return self ? 21 : 0;
}
/* Extra frames driven through fps.c: a fake display whose draw is a whole
 * hooked game frame, and a fake limiter site with the game clock. */
extern char fake_particles[];
static void *g_xdisplay_vt[13];
static struct { void **vt; } g_xdisplay_obj;
static void *g_xdisplay, *g_xnull, *g_xhwnd = (void *)1;
static unsigned g_xnow, g_xframe_start = 500000, g_xword[8], g_xengine[32];
static int g_xscreenshots, g_extra_draws;
static unsigned char g_xcrc_flag, g_xsite[64];
static DWORD __stdcall x_time(void) { return g_xnow; }
static int __stdcall x_iconic(void *h) { (void)h; return 0; }
static void *g_xtime_slot = (void *)x_time;
extern char fake_button_draw[], fake_clock_set[], fake_clock_set_left[];
typedef void (*button_fn)(void *window, void *instance);
typedef void (*clock_set_fn)(void *window, int percent, unsigned color);
static void *g_xbuttons[4]; /* the windows the frame's UI draws, NULL-ended */

/* The window manager's draw of the frame's buttons. */
static void draw_buttons(void)
{
    unsigned i;
    for (i = 0; g_xbuttons[i]; i++)
        ((button_fn)(void *)fake_button_draw)(g_xbuttons[i], NULL);
}

/* A node of the scene's update list (next +4, interface +0xC) and its
 * object (the interface 8 bytes in), whose On_Frame_Update counts. */
typedef struct scene_node {
    struct scene_node *prev, *next;
    void *pad;
    unsigned char *iface;
} scene_node;
static int g_light_steps;
static void __attribute__((thiscall)) light_frame_update(void *self)
{
    (void)self;
    g_light_steps++;
}
static void *g_light_vt[16];
static struct {
    void **vt;
    unsigned char pad[0x20];
} g_light;
static scene_node g_scene_nodes[2];
static scene_node *g_xscene, *g_xscene_next;
extern char fake_scene_update[];
typedef scene_node *(*scene_update_fn)(scene_node *node);

static void __attribute__((thiscall)) extra_draw(void *self)
{
    (void)self;
    g_extra_draws++;
    call_frame();
    if (g_xscene)
        g_xscene_next = ((scene_update_fn)(void *)fake_scene_update)(g_xscene);
    draw_buttons();
    g_xnow += 1;
}

/* The limiter's wait `elapsed` ms into a 33 ms game frame. */
static void extra_wait(unsigned elapsed)
{
    static void *stack[16];
    static unsigned char frame[64];
    void **slot = stack + 12;
    g_xnow = g_xframe_start + elapsed;
    *(unsigned **)frame = g_xengine; /* [EBP-0x20], EBP = frame + 0x20 */
    slot[0] = g_xsite + 5;
    slot[-8] = frame + 0x20;
    slot[-9] = (void *)33;
    pf_fps_enter(PF_FPS_WAIT, slot, NULL, 4);
}

/* A building's command button (check-like, as Zero Hour's command buttons
 * are), an image button, and a button the frames don't draw. */
static unsigned char g_cmd[0x40], g_img_win[0x40], g_hid[0x40], g_bdata[3][0x24];

/* `frames` game frames in GameClient::update's order: the frame's draw (the
 * buttons through the hooked push button draw), then the in-game UI's
 * update (the control bar asking the clocks if `ask`), then the limiter's
 * wait and its extra frames. Returns the frames drawn, game and extra. */
static unsigned clock_frames(unsigned frames, int ask)
{
    unsigned i;
    int before = g_extra_draws;
    for (i = 0; i < frames; i++) {
        g_xframe_start += 1000;
        call_frame();
        draw_buttons();
        if (ask) {
            ((clock_set_fn)(void *)fake_clock_set_left)(g_cmd, 40 + (int)i, 0x80FFFFFFu);
            ((clock_set_fn)(void *)fake_clock_set)(g_img_win, 70, 0x80FFFFFFu);
        }
        extra_wait(5);
    }
    return frames + (unsigned)(g_extra_draws - before);
}

static void spin_us(unsigned us)
{
    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    do
        QueryPerformanceCounter(&b);
    while ((unsigned long long)(b.QuadPart - a.QuadPart) * 1000000ull / (unsigned long long)f.QuadPart <
           us);
}

/* A game draw: 1 ms, three objects through the wrapped renderOneObject,
 * then 2 ms. Its time ends at the draw's return, not at an object's. */
static int g_inner_objects;
static void draw_with_objects(void)
{
    int i;
    spin_us(1000);
    for (i = 0; i < 3; i++)
        g_inner_objects += ((roo_fn)(void *)fake_roo)(1, 2, 3, 4) == 13;
    spin_us(2000);
}

static void *g_fake_vt[83];
static void **g_fake_obj[1] = { g_fake_vt };
static int g_inst_calls[2];
static void *g_inst_next[2];

static void *count_inst(unsigned k, void **slot, void *next)
{
    int dip = k == PF_INST_DIP;
    (void)slot;
    g_inst_calls[dip]++;
    g_inst_next[dip] = next;
    return next;
}

static void marker(const char *label)
{
    ((begin_fn)(void *)fake_event_begin)(g_marker2, label, 0, 0);
    ((end_fn)(void *)fake_event_end)(g_marker2);
}

static DWORD WINAPI other_thread(LPVOID unused)
{
    (void)unused;
    return (DWORD)call_anim();
}

static DWORD WINAPI other_present_thread(LPVOID unused)
{
    (void)unused;
    return (DWORD)((int (*)(int))(void *)fake_present)(1);
}

static int read_file(const WCHAR *path, char *buf, unsigned cap)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, 0, NULL);
    DWORD got = 0;
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    ReadFile(h, buf, cap - 1, &got, NULL);
    CloseHandle(h);
    buf[got] = 0;
    return 1;
}

/* The n-th comma-separated field of line (0-based) into out. */
static void field(const char *line, unsigned n, char *out, unsigned cap)
{
    unsigned i = 0;
    while (n && *line) {
        if (*line++ == ',')
            n--;
    }
    while (*line && *line != ',' && *line != '\r' && i + 1 < cap)
        out[i++] = *line++;
    out[i] = 0;
}

int main(void)
{
    WCHAR csv[MAX_PATH], scopes[MAX_PATH];
    char text[4096], f[32];
    const char *row;
    pf_image img;
    pf_result res[PF_SITE_COUNT];
    int enabled[PF_SITE_COUNT] = { 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                   1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 1,
                                   1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
    pf_site_mask installed = 0;
    unsigned runs_before, shadow_before;
    int seen[3];
    pf_timing_stats s;
    HANDLE t;
    DWORD code = 0;
    unsigned i;

    GetTempPathW(MAX_PATH, csv);
    wcscpy(scopes, csv);
    wcscat(csv, L"bfme2perf-hooks-test.csv");
    wcscat(scopes, L"bfme2perf-hooks-test-scopes.csv");

    printf("unhooked\n");
    CHECK(call_frame() == 41 && call_logic(5) == 6 && call_check_regs() == 0, "plain calls");

    printf("install\n");
    CHECK(pf_image_open(&img, GetModuleHandleW(NULL)), "no .text in the test exe");
    CHECK(pf_timing_open(g_sites, csv, scopes), "can't create %ls", csv);
    pf_timing_set_reflect(2);
    pf_timing_set_shadow(2);
    pf_install(&img, g_sites, PF_SITE_COUNT, enabled, PF_GROUP_FRAME, pf_enter_stubs,
               pf_timing_set_next, res);
    for (i = 0; i < PF_SITE_COUNT; i++) {
        if (!enabled[i])
            continue;
        CHECK(res[i].status == PF_ST_APPLIED, "%s: %s (%u matches)", g_sites[i].id,
              pf_status_name(res[i].status), res[i].matches);
        installed |= PF_BIT(i);
    }
    pf_timing_set_installed(installed);

    printf("stencil site\n");
    {
        static volatile unsigned char flag;
        pf_sp_data d;
        memset(&d, 0, sizeof d);
        CHECK(((int_fn)(void *)fake_stencil)() == 0x80, "fix off: the bits stay (%X)",
              ((int_fn)(void *)fake_stencil)());
        pf_sp_init(&flag, &d, 0, 1, 0, 0);
        pf_sp_set_sites(PF_STENCIL_OCCLUDEE, PF_STENCIL_OCCLUDER, PF_STENCIL_OTHER,
                        PF_SHADOW_PARTICLES);
        flag = 1;
        CHECK(((int_fn)(void *)fake_stencil)() == 0, "shadow pass: ECX zeroed through the stub");
        flag = 0;
        CHECK(((int_fn)(void *)fake_stencil)() == 0x80, "other passes: untouched");
        pf_sp_init(&flag, &d, 0, 0, 0, 0);
    }

    printf("calls\n");
    printf("instancing sites\n");
    g_fake_vt[82] = (void *)triple;
    CHECK(((obj_fn)(void *)fake_dip_caller)(g_fake_obj) == 15,
          "indirect call without a handler goes to [edx+0x148]");
    CHECK(((obj_fn)(void *)fake_mesh_caller)((void *)100) == 101,
          "per-mesh call without a handler goes to the callee");
    pf_timing_set_inst(count_inst, 0);
    CHECK(((obj_fn)(void *)fake_dip_caller)(g_fake_obj) == 15, "indirect call with a handler");
    CHECK(((obj_fn)(void *)fake_mesh_caller)((void *)200) == 201, "per-mesh call with a handler");
    CHECK(g_inst_calls[0] == 1 && g_inst_calls[1] == 1, "handler calls %d %d", g_inst_calls[0],
          g_inst_calls[1]);
    CHECK(g_inst_next[1] == (void *)triple, "indirect call: next is the vtable slot");
    CHECK(g_inst_next[0] == (void *)fake_mesh_render, "per-mesh call: next is the callee");
    pf_timing_set_inst(0, 0);

    runs_before = (unsigned)g_reflect_runs;
    shadow_before = (unsigned)g_shadow_runs;
    g_frame_no = 0;
    CHECK(call_logic(5) == 6, "logic before the first frame");
    for (i = 0; i < 3; i++) {
        CHECK(call_frame() == 41, "frame %u", i);
        seen[i] = g_seen_matrix0;
    }
    t = CreateThread(NULL, 0, other_thread, NULL, 0, NULL);
    WaitForSingleObject(t, INFINITE);
    GetExitCodeThread(t, &code);
    CloseHandle(t);
    CHECK(code == 0x5EED0005, "anim on another thread returned %08lX", (unsigned long)code);

    pf_timing_get_stats(&s);
    CHECK(s.frames == 3 && s.logic_calls == 3 && s.render_calls == 3, "frames %u logic %u render %u",
          s.frames, s.logic_calls, s.render_calls);
    CHECK(s.skel_render_calls == 3 && s.skel_other_calls == 3,
          "skeleton during render %u, elsewhere %u", s.skel_render_calls, s.skel_other_calls);
    CHECK(s.passed_through == 5, "passed through %u (1 early, 3 nested, 1 other thread)",
          s.passed_through);
    CHECK(s.stale == 0, "stale %u", s.stale);
    CHECK(s.scopes == 6 && s.stale_scopes == 0, "markers %u, stale %u", s.scopes, s.stale_scopes);
    CHECK(s.draws == 6, "draws %u", s.draws);
    CHECK(s.presents == 3, "presents %u", s.presents);
    {
        pf_present_row pr;
        unsigned before = s.presents;
        pf_present_take(&pr);
        CHECK(pr.calls == 3 && pr.gaps == 2, "completed presents %u/%u", pr.calls, pr.gaps);
        ((int (*)(int))(void *)fake_present)(0);
        pf_present_take(&pr);
        pf_timing_get_stats(&s);
        CHECK(!pr.calls && !pr.gaps && s.presents == before, "flip=0 counted a present");
        ((int (*)(int))(void *)fake_present)(1);
        pf_present_take(&pr);
        pf_timing_get_stats(&s);
        CHECK(pr.calls == 1 && pr.gaps == 1 && s.presents == before + 1,
              "flip=1 missing completed present");
        t = CreateThread(NULL, 0, other_present_thread, NULL, 0, NULL);
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
        pf_present_take(&pr);
        pf_timing_get_stats(&s);
        CHECK(!pr.calls && s.presents == before + 1,
              "wrong-thread completion polluted presentation sample");
    }
    CHECK((unsigned)g_reflect_runs - runs_before == 2, "reflection ran %u times in 3 frames",
          (unsigned)g_reflect_runs - runs_before);
    CHECK(s.reflect_runs == 2 && s.reflect_forced == 1 && s.reflect_skipped == 1,
          "reflect runs %u forced %u skipped %u", s.reflect_runs, s.reflect_forced,
          s.reflect_skipped);
    CHECK((unsigned)g_shadow_runs - shadow_before == 2, "shadow map drawn %u times in 3 frames",
          (unsigned)g_shadow_runs - shadow_before);
    CHECK(s.shadow_runs == 2 && s.shadow_forced == 1 && s.shadow_skipped == 1 &&
              s.shadow_restored == 1,
          "shadow runs %u forced %u skipped %u restored %u", s.shadow_runs, s.shadow_forced,
          s.shadow_skipped, s.shadow_restored);
    CHECK(seen[0] == 1 && seen[1] == 1 && seen[2] == 3,
          "the shaders got the matrices of frames %d, %d, %d (want 1, 1, 3)", seen[0], seen[1],
          seen[2]);

    printf("indirect call hook\n");
    {
        extern char fake_wait[];
        int before = g_sleeps;
        ((int_fn)(void *)fake_wait)();
        CHECK(g_sleeps == before + 1, "the hooked call [import] didn't reach its target");
        CHECK((unsigned char)fake_wait[7] == 0xE8 && (unsigned char)fake_wait[12] == 0x90,
              "site bytes %02X .. %02X", (unsigned char)fake_wait[7],
              (unsigned char)fake_wait[12]);
    }

    printf("held per-draw updates pass through on the game's own frames\n");
    {
        extern char fake_views_caller[], fake_particles[], fake_display_draw[];
        static void *views_vt[41];
        static void **views_obj[1] = { views_vt };
        int steps = g_particle_steps, draws = g_display_draws;
        views_vt[0xA0 / 4] = (void *)triple_this;
        CHECK(((obj_fn)(void *)fake_views_caller)(views_obj) == 21,
              "call [eax+0xA0] didn't reach the vtable slot with its this");
        CHECK((unsigned char)fake_views_caller[14] == 0xE8 &&
                  (unsigned char)fake_views_caller[19] == 0x90,
              "views site bytes %02X .. %02X", (unsigned char)fake_views_caller[14],
              (unsigned char)fake_views_caller[19]);
        CHECK(((int_fn)(void *)fake_particles)() == 0x5EED00F5 && g_particle_steps == steps + 1,
              "the particle update didn't run");
        CHECK(((int_fn)(void *)fake_display_draw)() == 0x5EED00F6 &&
                  g_display_draws == draws + 1 && g_particle_steps == steps + 2,
              "the wrapped display draw didn't run");
    }

    printf("registers\n");
    CHECK(call_check_regs() == 0, "a register or the return value changed");
    pf_timing_get_stats(&s);
    CHECK(s.skel_other_calls == 4, "check_regs's anim not timed: %u", s.skel_other_calls);

    printf("rows\n");
    pf_timing_flush();
    CHECK(read_file(csv, text, sizeof text), "no CSV");
    printf("%s", text);
    CHECK(strncmp(text, "time_s,frames,", 14) == 0, "no header");
    row = strstr(text, "\r\n");
    row = row ? row + 2 : "";
    field(row, 1, f, sizeof f);
    CHECK(!strcmp(f, "3"), "frames field %s", f);
    field(row, 4, f, sizeof f);
    CHECK(f[0] != 0, "logic column empty");
    field(row, 5, f, sizeof f);
    CHECK(f[0] != 0, "render column empty");
    field(row, 8, f, sizeof f);
    CHECK(!strcmp(f, "1"), "anim_on_screen %s", f);
    field(row, 12, f, sizeof f);
    CHECK(f[0] == 0, "vis_ms filled although its hook is off: %s", f);
    field(row, 15, f, sizeof f);
    CHECK(f[0] != 0, "present_ms empty");
    field(row, 16, f, sizeof f);
    CHECK(!strcmp(f, "0.000"), "shadow_ms %s", f);
    field(row, 18, f, sizeof f);
    CHECK(f[0] != 0, "views_ms empty");
    field(row, 30, f, sizeof f);
    CHECK(!strcmp(f, "2"), "draws %s", f);
    field(row, 31, f, sizeof f);
    CHECK(f[0] != 0, "rest_ms empty");
    field(row, 28, f, sizeof f);
    CHECK(f[0] != 0, "fx_mesh_ms empty");
    field(row, 33, f, sizeof f);
    CHECK(!strcmp(f, "1"), "reflect_skipped %s", f);
    field(row, 34, f, sizeof f);
    CHECK(!strcmp(f, "1"), "shadow_skipped %s", f);
    field(row, 35, f, sizeof f);
    CHECK(!strcmp(f, "0"), "shadow_meshes %s", f);
    field(row, 37, f, sizeof f);
    CHECK(!strcmp(f, "1"), "view_meshes %s", f);
    CHECK(read_file(scopes, text, sizeof text), "no scopes CSV");
    printf("%s", text);
    CHECK(strstr(text, "time_s,scope,calls_per_frame,ms_per_frame\r\n") == text, "scopes header");
    CHECK(strstr(text, ",RenderViews,1.00,") != NULL, "no RenderViews row");
    CHECK(strstr(text, ",Rendering mesh FXShader,1.00,") != NULL, "no per-mesh row by kind");

    printf("detail off\n");
    pf_timing_set_detail(0);
    CHECK(call_frame() == 41, "frame with Detail=0");
    pf_timing_get_stats(&s);
    CHECK(s.scopes == 7, "Detail=0 still timed the mesh marker: %u markers", s.scopes);
    pf_timing_flush();
    CHECK(read_file(csv, text, sizeof text), "no CSV");
    row = strstr(text, "\r\n");
    row = row ? strstr(row + 2, "\r\n") : NULL;
    row = row ? row + 2 : "";
    field(row, 37, f, sizeof f);
    CHECK(!strcmp(f, "1"), "view_meshes with Detail=0: %s", f);

    printf("placement and scrolling\n");
    {
        static unsigned char fake_ui[0x800];
        static void *the_ui = fake_ui;
        pf_ui_state ui = { (void *const *)&the_ui, 0x53C, 0x7F8 };
        unsigned before;
        int runs[4];

        /* Frame 4 (Detail=0) was skipped, so frame 5 would run anyway. */
        pf_timing_set_ui(&ui, 0);
        CHECK(call_frame() == 41, "frame 5");
        *(void **)(fake_ui + 0x53C) = fake_ui;
        for (i = 0; i < 2; i++) {
            before = (unsigned)g_shadow_runs;
            CHECK(call_frame() == 41, "placing frame %u", i);
            runs[i] = (int)((unsigned)g_shadow_runs - before);
        }
        CHECK(runs[0] == 1 && runs[1] == 1, "shadow map while placing: %d, %d (want 1, 1)",
              runs[0], runs[1]);
        *(void **)(fake_ui + 0x53C) = 0;
        fake_ui[0x7F8] = 1;
        for (i = 0; i < 2; i++) {
            before = (unsigned)g_shadow_runs;
            CHECK(call_frame() == 41, "scrolling frame %u", i);
            runs[i] = (int)((unsigned)g_shadow_runs - before);
        }
        CHECK(runs[0] == 0 && runs[1] == 1,
              "scrolling without the option: %d, %d (want the interval: 0, 1)", runs[0], runs[1]);
        pf_timing_set_ui(&ui, 1);
        for (i = 0; i < 2; i++) {
            before = (unsigned)g_shadow_runs;
            CHECK(call_frame() == 41, "scrolling frame %u with the option", i);
            runs[i + 2] = (int)((unsigned)g_shadow_runs - before);
        }
        CHECK(runs[2] == 1 && runs[3] == 1, "scrolling with the option: %d, %d (want 1, 1)",
              runs[2], runs[3]);
        fake_ui[0x7F8] = 0;
        the_ui = 0;
        CHECK(call_frame() == 41, "frame with no InGameUI");
        pf_timing_get_stats(&s);
        CHECK(s.shadow_held == 4, "redraws held at full rate: %u", s.shadow_held);
    }

    printf("FX groups\n");
    {
        /* Records { mesh, method count, method }; the light environment is
         * at mesh+0xC8. A (2 records), B, C share method M; D uses N. */
        static unsigned char mesh[4][0x100];
        static void *rec[6][8];
        static char method_m, method_n, env_e, env_f;
        void *const envs[4] = { &env_e, &env_e, &env_f, &env_e };
        group_fn group = (group_fn)(void *)fake_group;
        pf_timing_stats before;

        for (i = 0; i < 4; i++)
            *(void **)(mesh[i] + 0xC8) = envs[i];
        for (i = 0; i < 5; i++) {
            rec[i][0] = mesh[i < 2 ? 0 : i - 1];
            rec[i][1] = (void *)1;
            rec[i][2] = i == 4 ? &method_n : &method_m;
        }
        pf_timing_get_stats(&before);
        group(rec[2], rec[0], mesh[0], &method_m);
        marker(g_label_mesh);
        marker(g_label_mesh);
        group(rec[3], rec[2], mesh[1], &method_m);
        marker(g_label_mesh);
        group(rec[4], rec[3], mesh[2], &method_m);
        marker(g_label_mesh);
        group(rec[5], rec[4], mesh[3], &method_n);
        marker(g_label_mesh);
        marker(g_label_views);
        group(rec[3], rec[2], mesh[1], &method_n);
        pf_timing_get_stats(&s);
        CHECK(s.fx_groups - before.fx_groups == 5, "groups %u", s.fx_groups - before.fx_groups);
        CHECK(s.fx_merge - before.fx_merge == 1, "mergeable A->B only: %u",
              s.fx_merge - before.fx_merge);
        CHECK(s.fx_merge_anylight - before.fx_merge_anylight == 2,
              "mergeable ignoring lights A->B, B->C: %u",
              s.fx_merge_anylight - before.fx_merge_anylight);
    }

    printf("ShadowBatchIgnoreLight's group setup through the stub\n");
    {
        /* Three meshes the game would put in three groups, merged: the
         * setup's Set_Light_Environment gets the last one's environment. */
        static volatile unsigned char flag;
        static unsigned char mesh[3][0x330];
        static void *rec[4][8];
        static unsigned env[3] = { 1, 2, 3 };
        pf_sp_data d;
        void *arg[2] = { 0, &env[0] };

        memset(&d, 0, sizeof d);
        for (i = 0; i < 3; i++) {
            *(void **)(mesh[i] + PF_MESH_LIGHT_ENV) = &env[i];
            rec[i][0] = mesh[i];
            rec[i][1] = (void *)1;
        }
        pf_sp_init(&flag, &d, 1, 0, 0, 0);
        pf_sp_set_same_batch((void *)same_env);
        pf_sp_set_batch_light(1);
        pf_sp_set_batch_proven(1);
        flag = 1;
        CHECK(pf_sp_same_batch(mesh[0], mesh[1]) && pf_sp_same_batch(mesh[0], mesh[2]),
              "the scan merges the three");
        ((group_fn)(void *)fake_group)(rec[3], rec[0], mesh[0], 0);
        pf_sp_light_env(arg);
        CHECK(arg[1] == &env[2], "the setup stores the last game group's environment");
        flag = 0;
        pf_sp_init(&flag, &d, 0, 0, 0, 0);
    }

    printf("extra frames and the stored shadow map\n");
    {
        pf_fps_sites fs;
        int first_run, extra_seen, before_runs;
        unsigned j;

        memset(&fs, 0, sizeof fs);
        g_xdisplay_vt[12] = (void *)extra_draw;
        g_xdisplay_obj.vt = g_xdisplay_vt;
        g_xdisplay = &g_xdisplay_obj;
        g_xsite[0] = 0xFF;
        *(void ***)(g_xsite + 8) = &g_xtime_slot;
        *(unsigned **)(g_xsite + 16) = &g_xframe_start;
        fs.display = &g_xdisplay;
        fs.draw = (void *)extra_draw;
        fs.hwnd = &g_xhwnd;
        fs.is_iconic = (void *)x_iconic;
        fs.client = &g_xnull;
        fs.last_client_frame = &g_xword[0];
        fs.sync_acc = &g_xword[1];
        fs.last_draw_ms = &g_xword[2];
        fs.skip_count = &g_xword[3];
        fs.init_flags = &g_xword[4];
        fs.screenshots = &g_xscreenshots;
        fs.sync_time = &g_xword[5];
        fs.prev_sync_time = &g_xword[6];
        fs.logic_seed = &g_xword[7];
        fs.verify_crc = &g_xcrc_flag;
        fs.logic = &g_xnull;
        fs.tactical_view = &g_xnull;
        fs.particles = &g_xnull;

        for (j = 0; j < 2; j++) {
            pf_fps_init(&fs, NULL, 60, 0, 0);
            pf_fps_set_installed(1, 1, 1, (void *)fake_particles);
            pf_fps_set_passes((int)(1 - j), 0);
            /* Game frames until one draws the shadow map; the next one reuses it. */
            for (i = 0, first_run = 0; i < 4 && !first_run; i++) {
                before_runs = g_shadow_runs;
                CHECK(call_frame() == 41, "game frame");
                first_run = g_shadow_runs != before_runs;
            }
            CHECK(first_run, "no game frame drew the shadow map");
            /* Its wait: one extra frame (shown 5 ms into the frame, so about 13 ms later). */
            before_runs = g_shadow_runs;
            g_xframe_start += 1000;
            {
                pf_present_row pr;
                unsigned regular;
                pf_timing_get_stats(&s);
                regular = s.presents;
                pf_present_take(&pr);
                extra_wait(5);
                pf_present_take(&pr);
                pf_timing_get_stats(&s);
                CHECK(pr.calls == 1 && pr.gaps == 1,
                      "extra completion missing: calls %u gaps %u", pr.calls, pr.gaps);
                CHECK(s.presents == regular, "extra present polluted regular stats");
            }
            extra_seen = g_seen_matrix0;
            CHECK(g_extra_draws == 1 + 2 * (int)j, "extra frames drawn %d", g_extra_draws);
            if (j == 0) {
                CHECK(g_shadow_runs == before_runs + 1,
                      "ExtraFrameShadows=1: the extra frame didn't draw the shadow map");
                /* The next game frame skips it (interval 2) and must get the
                 * matrix of the map the extra frame drew. */
                before_runs = g_shadow_runs;
                CHECK(call_frame() == 41, "game frame after the extra frame");
                CHECK(g_shadow_runs == before_runs && g_seen_matrix0 == extra_seen,
                      "after an extra frame's shadow map: ran %d, matrix of frame %d (want %d)",
                      g_shadow_runs - before_runs, g_seen_matrix0, extra_seen);
            } else {
                CHECK(g_shadow_runs == before_runs,
                      "ExtraFrameShadows=0: the extra frame drew the shadow map");
            }
            /* Now after a game frame that reused the stored map: an extra
             * frame that redraws it must get its own matrix, not the stored one. */
            for (i = 0, first_run = 1; i < 4 && first_run; i++) {
                before_runs = g_shadow_runs;
                CHECK(call_frame() == 41, "game frame");
                first_run = g_shadow_runs != before_runs;
            }
            CHECK(!first_run, "no game frame reused the shadow map");
            before_runs = g_shadow_runs;
            g_xframe_start += 1000;
            extra_wait(5);
            if (j == 0)
                CHECK(g_shadow_runs == before_runs + 1 && g_seen_matrix0 == g_frame_no,
                      "after a reused map: ran %d, matrix of frame %d (want its own, %d)",
                      g_shadow_runs - before_runs, g_seen_matrix0, g_frame_no);
            else
                CHECK(g_shadow_runs == before_runs && g_seen_matrix0 != g_frame_no,
                      "ExtraFrameShadows=0 after a reused map: ran %d, matrix of frame %d",
                      g_shadow_runs - before_runs, g_seen_matrix0);
            CHECK(!pf_fps_stopped(), "stopped: %s", pf_fps_stopped() ? pf_fps_stopped() : "no");
        }
        pf_timing_get_stats(&s);
        CHECK(s.shadow_extra == 2, "shadow maps drawn by extra frames: %u", s.shadow_extra);
        CHECK(s.shadow_fresh >= 1, "no game frame reused a map an extra frame had just drawn");

        printf("full-rate shadows redraw after every extra frame\n");
        {
            pf_timing_stats before;
            int draws = g_extra_draws;
            pf_timing_set_shadow(1);
            pf_fps_init(&fs, NULL, 60, 0, 0);
            pf_fps_set_installed(1, 1, 1, (void *)fake_particles);
            pf_fps_set_passes(1, 0);
            pf_timing_get_stats(&before);
            /* Each extra updates the saved map. Interval=1 must still draw
             * the following regular frame's map and use its new matrix. */
            for (i = 0; i < 3; i++) {
                before_runs = g_shadow_runs;
                CHECK(call_frame() == 41, "full-rate game frame");
                CHECK(g_shadow_runs == before_runs + 1 && g_seen_matrix0 == g_frame_no,
                      "full-rate regular frame reused a map: runs %d matrix %d want %d",
                      g_shadow_runs - before_runs, g_seen_matrix0, g_frame_no);
                g_xframe_start += 1000;
                extra_wait(5);
                CHECK(g_extra_draws == draws + (int)i + 1 && g_shadow_runs == before_runs + 2,
                      "full-rate extra frame missed its shadow draw");
            }
            before_runs = g_shadow_runs;
            CHECK(call_frame() == 41 && g_shadow_runs == before_runs + 1 &&
                      g_seen_matrix0 == g_frame_no,
                  "final regular frame did not redraw its shadow map");
            pf_timing_get_stats(&s);
            CHECK(s.shadow_fresh == before.shadow_fresh &&
                      s.shadow_skipped == before.shadow_skipped &&
                      s.shadow_runs == before.shadow_runs + 4 &&
                      s.shadow_extra == before.shadow_extra + 3,
                  "full-rate regular/extra shadow accounting");
            pf_timing_set_shadow(2);
            pf_fps_set_passes(0, 0);
        }

        printf("extra frames hold the scene's update list\n");
        {
            pf_fps_row row;
            int steps, draws;
            g_light_vt[0x34 / 4] = (void *)light_frame_update;
            g_light.vt = g_light_vt;
            g_scene_nodes[0].iface = (unsigned char *)&g_light + 8;
            g_scene_nodes[0].next = &g_scene_nodes[1];
            pf_fps_set_scene_skip(img.base + res[PF_SCENE_UPDATE].rva + 17);
            pf_fps_take_row(&row);
            steps = g_light_steps;
            CHECK(((scene_update_fn)(void *)fake_scene_update)(&g_scene_nodes[0]) ==
                          &g_scene_nodes[1] &&
                      g_light_steps == steps + 1,
                  "a game frame's update loop: object updated %d time(s), next node taken",
                  g_light_steps - steps);
            g_xscene = &g_scene_nodes[0];
            g_xscene_next = NULL;
            draws = g_extra_draws;
            g_xframe_start += 1000;
            extra_wait(5);
            g_xscene = NULL;
            CHECK(g_extra_draws == draws + 1, "no extra frame drawn (%d)", g_extra_draws - draws);
            CHECK(g_light_steps == steps + 1 && g_xscene_next == &g_scene_nodes[1],
                  "an extra frame updated the object (%d) or lost the list (%p)",
                  g_light_steps - steps - 1, (void *)g_xscene_next);
            pf_fps_take_row(&row);
            CHECK(row.held >= 1, "the hold isn't counted (%u)", row.held);
            CHECK(((scene_update_fn)(void *)fake_scene_update)(&g_scene_nodes[0]) ==
                          &g_scene_nodes[1] &&
                      g_light_steps == steps + 2,
                  "the next game frame updates it again");
        }

        printf("the game's draw is timed whole around renderOneObject's wraps\n");
        {
            extern char fake_display_draw[];
            static volatile unsigned char flag;
            pf_lt_data d;
            pf_lt_counts c;
            pf_fps_row row;
            pf_timing_stats s0, s1;
            unsigned k;
            int draws = g_display_draws;

            memset(&d, 0, sizeof d);
            pf_lt_init(&d, &flag, 0, 0, 0, 0);
            pf_lt_set_installed(1, 0);
            pf_lt_set_timing(1, pf_timing_pass);
            pf_lt_take(&c);
            pf_fps_take_row(&row);
            pf_timing_get_stats(&s0);
            g_inner_objects = 0;
            g_draw_inner = draw_with_objects;
            for (k = 0; k < 10; k++) {
                g_xframe_start += 1000;
                CHECK(call_frame() == 41, "game frame %u", k);
                ((begin_fn)(void *)fake_event_begin)(g_marker, g_label_views, 0, 0);
                CHECK(((int_fn)(void *)fake_display_draw)() == 0x5EED00F6, "draw %u", k);
                ((end_fn)(void *)fake_event_end)(g_marker);
            }
            g_draw_inner = 0;
            pf_fps_take_row(&row);
            pf_timing_get_stats(&s1);
            pf_lt_take(&c);
            CHECK(g_display_draws == draws + 10 && g_inner_objects == 30,
                  "draws %d, objects with their result %d", g_display_draws - draws,
                  g_inner_objects);
            CHECK(c.calls[2] == 30, "the objects are timed in the views pass: %u", c.calls[2]);
            /* An object's return ending the draw would give about 1 ms. */
            CHECK(row.ref_us >= 2900 && row.ref_us < 1000000,
                  "the game's draw (3 ms, objects inside) is the reference: %u us", row.ref_us);
            CHECK(s1.stale == s0.stale, "wrapped calls dropped: %u", s1.stale - s0.stale);
            pf_lt_set_timing(0, 0);
        }

        printf("extra frames and command button clocks, in GameClient::update's order\n");
        {
            pf_btn_stats b0, b;
            int check_like, image;
            unsigned n;

            *(void **)(g_cmd + 0x2C) = g_bdata[0];
            *(void **)(g_img_win + 0x2C) = g_bdata[1];
            *(void **)(g_hid + 0x2C) = g_bdata[2];
            g_bdata[0][0x20] = 1;
            g_xbuttons[0] = g_cmd;
            g_xbuttons[1] = g_img_win;
            pf_fps_init(&fs, NULL, 120, 0, 0);
            pf_fps_set_installed(1, 1, 1, (void *)fake_particles);
            pf_fps_set_passes(0, 0);
            pf_btn_init(0x2C);
            clock_frames(1, 1);
            pf_btn_get_stats(&b0);

            /* Production on both: every frame, the game's and the extra ones,
             * shows both clocks (the building's through the check-like draw). */
            check_like = g_clocks_check_like;
            image = g_clocks_drawn;
            n = clock_frames(10, 1);
            pf_btn_get_stats(&b);
            printf("  %u frames for 10 game frames\n", n);
            CHECK(n >= 20, "only %u frames for 10 game frames", n);
            CHECK(g_clocks_check_like - check_like == (int)n && g_clocks_drawn - image == (int)n,
                  "clocks shown in %d check-like, %d image of %u frames",
                  g_clocks_check_like - check_like, g_clocks_drawn - image, n);
            CHECK(b.requests - b0.requests == 20 && b.noted - b0.noted == 20 &&
                      b.asked - b0.asked == 2 * (n - 10) && b.unseen == b0.unseen,
                  "stats: requests %u, noted %u, asked %u, unseen %u", b.requests - b0.requests,
                  b.noted - b0.noted, b.asked - b0.asked, b.unseen - b0.unseen);
            CHECK(*(int *)(g_bdata[0] + 4) == 49 && g_bdata[0][0] == 2 && g_bdata[1][0] == 1,
                  "after: %d%%, requests %u %u (left for the game's next draw)",
                  *(int *)(g_bdata[0] + 4), g_bdata[0][0], g_bdata[1][0]);

            /* Without the plugin setting requests back (as up to 0.9.1): only
             * the first extra frame after each update shows them. */
            pf_btn_init(0);
            check_like = g_clocks_check_like;
            image = g_clocks_drawn;
            n = clock_frames(10, 1);
            CHECK(g_clocks_check_like - check_like == 11 && g_clocks_drawn - image == 11,
                  "unhelped: clocks shown in %d check-like, %d image of %u frames (want 11: "
                  "the first game draw and each first extra frame)",
                  g_clocks_check_like - check_like, g_clocks_drawn - image, n);

            /* Production done: no clock; a request on a button the frames
             * don't draw is counted as unseen. */
            pf_btn_init(0x2C);
            pf_btn_get_stats(&b0);
            check_like = g_clocks_check_like;
            clock_frames(1, 0);
            g_xframe_start += 1000;
            CHECK(call_frame() == 41, "game frame");
            draw_buttons();
            ((clock_set_fn)(void *)fake_clock_set_left)(g_hid, 5, 0x80FFFFFFu);
            extra_wait(5);
            pf_btn_get_stats(&b);
            CHECK(g_clocks_check_like == check_like && b.asked == b0.asked &&
                      b.unseen == b0.unseen + 1 && g_bdata[2][0] == 2,
                  "after production: %d clock(s), asked %u, unseen %u, hidden request %u",
                  g_clocks_check_like - check_like, b.asked - b0.asked, b.unseen - b0.unseen,
                  g_bdata[2][0]);
            g_xbuttons[0] = g_xbuttons[1] = 0;
            CHECK(!pf_fps_stopped(), "stopped: %s", pf_fps_stopped() ? pf_fps_stopped() : "no");
            pf_btn_init(0);
        }
        pf_fps_init(NULL, "off", 30, 0, 0);
    }

    printf("light sites\n");
    {
        static volatile unsigned char flag;
        pf_lt_data d;
        pf_lt_counts c;
        memset(&d, 0, sizeof d);
        d.glights_skip = (unsigned char *)fake_glights_skip;
        CHECK(((roo_fn)(void *)fake_roo)(1, 10, 100, 1000) == 1211 &&
                  ((int_fn)(void *)fake_glights)() == 1,
              "not set up: both run as written");
        pf_lt_init(&d, &flag, 1, 0, 0, 0);
        pf_lt_set_installed(1, 0);
        pf_lt_set_timing(1, pf_timing_pass);
        flag = 1;
        CHECK(((int_fn)(void *)fake_glights)() == 2, "shadow pass: the stub continues at the skip");
        flag = 0;
        CHECK(((int_fn)(void *)fake_glights)() == 1, "other passes: the loop runs");
        pf_lt_take(&c);
        ((begin_fn)(void *)fake_event_begin)(g_marker, g_label_views, 0, 0);
        CHECK(((roo_fn)(void *)fake_roo)(1, 10, 100, 1000) == 1211 &&
                  ((roo_fn)(void *)fake_roo)(2, 0, 0, 0) == 2,
              "renderOneObject's wrap keeps its result and stack");
        ((end_fn)(void *)fake_event_end)(g_marker);
        CHECK(((roo_fn)(void *)fake_roo)(3, 0, 0, 0) == 3, "outside a pass: still the result");
        pf_lt_take(&c);
        CHECK(c.calls[2] == 2 && c.calls[0] == 0 && c.calls[1] == 0,
              "two calls inside the views marker: %u %u %u", c.calls[0], c.calls[1], c.calls[2]);
        pf_lt_set_timing(0, 0);
        pf_lt_init(&d, &flag, 0, 0, 0, 0);
    }

    pf_timing_discard(csv, scopes);
    CHECK(GetFileAttributesW(scopes) == INVALID_FILE_ATTRIBUTES, "scopes CSV not deleted");
    if (g_failures) {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("OK: hook tests passed\n");
    return 0;
}
