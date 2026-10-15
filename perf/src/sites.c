#include "sites.h"

const pf_site pf_sites[PF_SITE_COUNT] = {
    { "frame", "GameEngine::update",
      "55 8B EC 51 53 56 8B F1 8B 0D ?? ?? ?? ?? 8B 01 FF 50 28 8B 0D ?? ?? ??",
      0, 5, 0, PF_GROUP_FRAME, 0, 0, 0x002325A0 },
    { "logic", "GameLogic::update",
      "B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 81 EC 74 01 00 00 53 56 8B F1 57 8D 4D D8",
      0, 5, 4, PF_GROUP_LOGIC, 0, 0, 0x0022E4E8 },
    { "render", "RTS3DScene::Render",
      "55 8B EC 83 EC 28 53 56 8B 35 ?? ?? ?? ?? 85 F6 57 8B F9 89 7D E4 0F 84",
      0, 6, 4, PF_GROUP_RENDER, 0, 0, 0x0007177E },
    { "skel_hlod", "HLodClass::Update_Sub_Object_Transforms",
      "83 EC 4C 53 55 56 57 8B D9 E8 ?? ?? ?? ?? 33 C9 39 8B 20 01 00 00 89 4C",
      0, 5, 0, PF_GROUP_SKEL, 0, 0, 0x0019CD70 },
    { "skel_anim", "Animatable3DObjClass::Update_Sub_Object_Transforms",
      "83 EC 30 56 8B F1 8B 8E FC 00 00 00 85 C9 74 71 80 B9 F4 00 00 00 00 75",
      0, 6, 0, PF_GROUP_SKEL, 0, 0, 0x001A5050 },
    { "vis", "RTS3DScene::Visibility_Check",
      "B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 EC 60 53 56 8B F1 8D 9E BC 00 00 00 57",
      0, 5, 4, PF_GROUP_SCENE, 0, 0, 0x00070B83 },
    { "cust", "RTS3DScene::Customized_Render",
      "55 8B EC 83 EC 0C 53 56 33 F6 57 8B F9 89 B7 F0 07 00 00 89 B7 F8 07 00",
      0, 6, 4, PF_GROUP_SCENE, 0, 0, 0x0006FE84 },
    { "flush", "RTS3DScene::Flush",
      "B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 81 EC A0 03 00 00 53 57 FF 75 08 8B F9 89",
      0, 5, 4, PF_GROUP_SCENE, 0, 0, 0x00070F44 },
    { "present", "DX8Wrapper::End_Scene",
      "64 A1 00 00 00 00 6A FF 68 ?? ?? ?? ?? 50 64 89 25 00 00 00 00 53 33 DB",
      0, 6, 0, PF_GROUP_PRESENT, 0, 0, 0x001225E0 },
    { "event_begin", "BFME2ScopedRenderEvent::BFME2ScopedRenderEvent",
      "8B 44 24 04 85 C0 56 8B F1 0F 84 ?? ?? ?? ?? 55 57 8B 3D ?? ?? ?? ?? 68",
      0, 6, 12, PF_GROUP_EVENTS, 1, 0, 0x00117690 },
    { "event_end", "BFME2ScopedRenderEvent::~BFME2ScopedRenderEvent",
      "83 C4 08 5F 5D 8B C6 5E C2 0C 00 CC CC CC CC CC CC CC CC CC E9 ?? ?? ?? ?? CC CC",
      20, 5, 0, PF_GROUP_EVENTS, 1, 0, 0x00117740 },
    { "draw", "BfmeDrawOps::Draw",
      "A1 ?? ?? ?? ?? 83 EC 10 85 C0 57 8B 7C 24 20 74 08 3B C7 0F 83 ?? ?? ??",
      0, 5, 0, PF_GROUP_DRAWS, 1, 0, 0x0011FD40 },
    { "reflect", "W3DDisplay::draw, the water reflection update call",
      "FF B7 04 01 00 00 8B 0D ?? ?? ?? ?? FF 35 ?? ?? ?? ?? C6 45 FC 02 E8 ?? ?? ?? ?? 8D 8D "
      "A8 FE FF FF 88 5D FC E8",
      22, 5, 8, PF_GROUP_REFLECT, 1, 1, 0x00049E5D },
    { "shadow", "W3DDisplay::draw, the UpdateShadowMap call",
      "FF 35 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? C6 45 FC 01 E8 ?? ?? ?? ?? 8D 8D A8 FE FF FF 88 5D FC "
      "E8",
      16, 5, 4, PF_GROUP_SHADOW, 1, 1, 0x00049DF5 },
    { "shadow_info", "the SetShadowMapInfo call in the shadow matrix setter",
      "8B 0D ?? ?? ?? ?? 8D 45 C4 50 6A 00 E8 ?? ?? ?? ?? 5E 83 C5 78 C9 C3",
      12, 5, 12, PF_GROUP_SHADOW, 1, 1, 0x0007BB37 },
    { "fx_group", "the FX shader batch, the per-group setup call",
      "68 ?? ?? ?? ?? 8D 8D 8C FD FF FF E8 ?? ?? ?? ?? 8B 0B C6 45 FC 01 E8 ?? ?? ?? ?? 8B 06 68 "
      "FF FF 00 00",
      22, 5, 0, PF_GROUP_BATCH, 1, 1, 0x00173CB8 },
    /* From the pass loop to the chunk-of-one Render_With_FX_Material call, so
     * the frame offsets the instancing reads are pinned: [EBP-0x14] pass,
     * [EBP-0x24] pass count, [EBP-0x2C] max instances, [EBP+8] group end,
     * EDI the record, EBX the group's first record, ECX = [EDI] the mesh. */
    { "inst_mesh", "the FX shader batch, the per-mesh Render_With_FX_Material call",
      "83 65 EC 00 83 7D DC 00 0F 8E ?? ?? ?? ?? FF 75 EC 8B 06 8B CE FF 50 0C 3B 5D 08 8B 03 8B "
      "80 C4 00 00 00 8B 80 BC 00 00 00 8B 40 24 8B FB 89 45 D4 0F 84 ?? ?? ?? ?? 8B 45 08 2B C7 "
      "C1 F8 05 39 45 D4 89 45 F0 8D 45 D4 7C 03 8D 45 F0 8B 08 8B C1 C1 E0 05 03 C7 83 F9 01 89 "
      "4D F0 89 45 E4 0F 8F ?? ?? ?? ?? 8D 85 CC FE FF FF 68 ?? ?? ?? ?? 50 E8 ?? ?? ?? ?? 8B 07 "
      "59 59 8B 88 C4 00 00 00 E8 ?? ?? ?? ?? 85 C0 74 0F 8B 07 8B 88 C4 00 00 00 E8 ?? ?? ?? ?? "
      "EB 05 B8 ?? ?? ?? ?? 50 8D 85 CC FE FF FF 50 E8 ?? ?? ?? ?? 59 59 6A 00 68 ?? ?? ?? ?? 8D "
      "85 CC FE FF FF 50 8D 8D 4C FB FF FF E8 ?? ?? ?? ?? 6A 01 51 8B C4 89 30 FF 46 04 8B 0F C6 "
      "45 FC 02 89 65 F0 E8 ?? ?? ?? ?? 8D 8D 4C FB FF FF",
      216, 5, 8, PF_GROUP_INST, 1, 1, 0x00173DAD },
    { "inst_dip", "BfmeDrawOps::Draw, the DrawIndexedPrimitive call",
      "68 ?? ?? ?? ?? 56 57 E8 ?? ?? ?? ?? 0F B7 0D ?? ?? ?? ?? 03 4C 24 30 A1 ?? ?? ?? ?? 83 05 "
      "?? ?? ?? ?? 01 8B 10 83 C4 0C 57 51 0F B7 0D ?? ?? ?? ?? 56 0F B7 35 ?? ?? ?? ?? 53 03 CE "
      "51 8B 4C 24 34 51 50 FF 92 48 01 00 00 83 05",
      67, 6, 28, PF_GROUP_INST, 1, 2, 0x0011FEC5 },
    { "fps_wait", "GameEngine::execute, the frame limiter's Sleep call",
      "53 FF 15 ?? ?? ?? ?? FF 15 ?? ?? ?? ?? 8B F8 2B 05 ?? ?? ?? ?? 3B C6 72 E7 8B 75 E0 89 3D",
      1, 6, 4, PF_GROUP_FPS, 1, PF_CALL_INDIRECT, 0x0023A1DD },
    { "fps_avg", "W3DDisplay::draw, the average frame rate update call",
      "8B CF E8 ?? ?? ?? ?? 8B CF E8 ?? ?? ?? ?? 8B 47 30 3D",
      2, 5, 0, PF_GROUP_FPS, 1, 1, 0x0004B7B5 },
    /* Compares the 12 components and returns if unchanged, else sets the
     * transform and marks the sub-objects dirty. */
    { "hlod_set", "HLodClass::Set_Transform",
      "51 53 55 56 57 8B 7C 24 18 89 4C 24 10 33 ED 8D 59 18 33 C9 8B F7 8B D3 F3 0F 10 02 0F 2E "
      "06 9F F6 C4 44 7A 24",
      0, 5, 4, PF_GROUP_UNITS, 1, 0, 0x0019AE20 },
    /* push -1; push handler: the EH prolog, then the HLod vtables are put back. */
    { "hlod_dtor", "HLodClass::~HLodClass",
      "6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 51 56 8B F1 89 74 24 04 C7 "
      "06 ?? ?? ?? ?? C7 46 08 ?? ?? ?? ?? C7 44 24 10 01 00 00 00 E8",
      0, 7, 0, PF_GROUP_UNITS, 1, 0, 0x0019E6A0 },
    /* Win32GameEngine::serviceWindowsOS, the message pump that update tail
     * calls each frame: PeekMessage, then Get/Translate/Dispatch per message. */
    { "pump", "Win32GameEngine::serviceWindowsOS",
      "B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 EC 20 53 57 8B 3D ?? ?? ?? ?? 33 DB 53 53 53 53 8D 45 D4 50 "
      "FF D7 85 C0 74 70 56 BE 13 01 00 00",
      0, 5, 0, PF_GROUP_PUMP, 0, 0, 0x00041A7D },
    /* W3DDisplay's frame: unless the game is paused (GameLogic +0x125),
     * TheDisplay->updateViews() (vtable +0xA0: each view's camera motion and
     * its drawables' client update), then TheFXParticleSystemManager->update()
     * (vtable +0x28). Both step once per draw, so extra frames hold them. */
    { "views", "W3DDisplay's frame, the updateViews call",
      "A1 ?? ?? ?? ?? 38 98 25 01 00 00 75 0F 8B 06 8B CE FF 90 A0 00 00 00 E8 ?? ?? ?? ?? 8B 0D "
      "?? ?? ?? ?? 8B 01 FF 50 28",
      17, 6, 0, PF_GROUP_HOLD, 1, 2, 0x00049D35 },
    /* Steps every particle system one frame per call (its own once-per-client-
     * frame guard is only on with GlobalData +0xD45). */
    { "particles", "the FX particle system manager's update",
      "B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 EC 28 0F 57 C0 53 57 8B F9 F3 0F 11 47 60 A1 ?? ?? ?? ?? 33 "
      "DB 38 98 C2 09 00 00 74 14",
      0, 5, 0, PF_GROUP_HOLD, 1, 0, 0x001F5123 },
    /* W3DDisplay::draw, wrapped: FrameSmoothing=3 puts the units it showed one
     * game frame behind back when it returns. */
    { "display_draw", "W3DDisplay::draw",
      "B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 EC 30 A1 ?? ?? ?? ?? 85 C0 57 8B F9 89 7D EC 74 0F 50 FF 15",
      0, 5, 0, PF_GROUP_LAG, 0, 0, 0x0004B788 },
    /* The push button draw (window, instance data), cdecl, the whole
     * function: the check-like draw (Zero Hour's command buttons), the color
     * draw or the image draw. Each draws the progress clock the control bar
     * asked for on the button and clears the request (btnclock.c checks the
     * three). */
    { "button_draw", "W3DGadgetPushButtonDraw",
      "55 8B EC 83 EC 10 56 57 8B 7D 08 57 E8 ?? ?? ?? ?? 84 C0 59 74 0B FF 75 0C 57 E8 ?? ?? ?? "
      "?? EB 57 83 BF 84 00 00 00 00 74 45 8B 75 0C F6 46 0A 20 74 33 8D 45 FC 50 8D 45 F8 50 8B "
      "CF E8 ?? ?? ?? ?? 8D 45 F4 50 8D 45 F0 50 8B CF E8 ?? ?? ?? ?? 8B 86 7C 01 00 00 01 45 F8 "
      "8B 86 80 01 00 00 01 45 FC 56 EB 0C 56 57 E8 ?? ?? ?? ?? EB 09 FF 75 0C 57 E8 ?? ?? ?? ?? "
      "59 59 5F 5E C9 C3",
      0, 6, 0, PF_GROUP_BUTTONS, 1, 0, 0x000A5A3E },
    /* The control bar's clock requests on a button (window, percent, color),
     * cdecl: the data's first byte is set to 1 (clock) or 2 (remaining time).
     * Probes, counted only, to tell whether requests reach the draws. */
    { "clock_set", "GadgetButtonDrawClock",
      "55 8B EC 83 7D 08 00 74 29 8B 4D 08 E8 ?? ?? ?? ?? 85 C0 75 05 E8 ?? ?? ?? ?? 8B 4D 0C 89 48 "
      "04 8B 4D 10 89 48 08 8B 4D 08 50 C6 00 01 E8",
      0, 7, 0, PF_GROUP_CLOCKS, 1, 0, 0x00329ED8 },
    { "clock_set_left", "GadgetButtonDrawInverseClock",
      "55 8B EC 83 7D 08 00 74 29 8B 4D 08 E8 ?? ?? ?? ?? 85 C0 75 05 E8 ?? ?? ?? ?? 8B 4D 0C 89 48 "
      "04 8B 4D 10 89 48 08 8B 4D 08 50 C6 00 02 E8",
      0, 7, 0, PF_GROUP_CLOCKS, 1, 0, 0x00329F0C },
    /* FXShaderRenderer::PerformRendering's "same batch" test of the group's
     * first mesh against the next record's (Is_Rendering_Identical, thiscall,
     * one argument): ShadowBatchIgnoreLight calls pf_sp_same_batch here
     * directly, without a stub (it runs for every record of every pass). */
    { "same_batch", "PerformRendering, the Is_Rendering_Identical call",
      "8B FB 2B FE 8B 06 8B 0B 50 E8 ?? ?? ?? ?? 84 C0 74 34 8B 43 04 3B 46 04 75 2C",
      9, 5, 4, PF_GROUP_SBATCH, 1, 1, 0x00173C2B },
    /* RTS3DScene::flushOccludedObjectsIntoStencil: where each list keeps the
     * template's SHADOW_VOLUME_NON_SELF_* bits (+0x5F2 & 0x380), which put the
     * object between two mesh flushes. Stolen and run from the trampoline. */
    { "stencil_occludee", "flushOccludedObjectsIntoStencil, and ecx, 0x380",
      "8B 4B 04 66 8B 89 F2 05 00 00 83 C0 0C 81 E1 80 03 00 00 09 08 FF 45 F8",
      13, 6, 0, PF_GROUP_STENCIL, 1, 0, 0x00070257 },
    { "stencil_occluder", "flushOccludedObjectsIntoStencil, shr eax, 3; and eax, 0x70",
      "8B 40 04 8B 40 04 66 8B 80 F2 05 00 00 C1 E8 03 83 E0 70 89 45 F4 74",
      13, 6, 0, PF_GROUP_STENCIL, 1, 0, 0x00070597 },
    { "stencil_other", "flushOccludedObjectsIntoStencil, shr ebx, 3; and ebx, 0x70",
      "8B 40 04 8B 40 04 66 8B 98 F2 05 00 00 C1 EB 03 83 E3 70 74",
      13, 6, 0, PF_GROUP_STENCIL, 1, 0, 0x0007069D },
    /* RTS3DScene::Flush: the particle manager's render (cdecl forwarder, one
     * argument), in pass mode 0 with polygon mode 0. */
    { "shadow_particles", "RTS3DScene::Flush, the particle render call",
      "39 9F EC 07 00 00 C7 45 FC 0E 00 00 00 75 0E 39 5F 18 75 09 FF 75 08 E8 ?? ?? ?? ?? 59 E8",
      23, 5, 0, PF_GROUP_SPART, 1, 1, 0x000716EE },
    /* W3DDisplay's frame, after the view update, the particle update and the
     * terrain's per-camera updates, where the render starts (the shadow map,
     * then the reflection, Begin_Render .. End_Render). Catch-up continues a
     * frame without its render at the frame's own no-render path from here
     * (cusites.c). Stolen: mov eax, [TheGameLogic]. */
    { "catchup_render", "W3DDisplay's frame, the start of its render",
      "74 0B FF B7 04 01 00 00 E8 ?? ?? ?? ?? A1 ?? ?? ?? ?? 3B C3 74 09 38 58 6D 0F 85 ?? ?? ?? "
      "?? 39 1D ?? ?? ?? ?? 74 43",
      13, 5, 0, PF_GROUP_CATCHUP, 1, 0, 0x00049DAB },
    /* RTS3DScene::Customized_Render's update loop, at one object's
     * On_Frame_Update call (mode 0, neither the projection override nor the
     * shadow-map pass): load the object, call its vtable +0x34. W3D particle
     * buffers emit and dynamic lights fade one step per call, so extra frames
     * hold it. Stolen: mov ecx, [ebx+0xC]; lea eax, [ecx-8]. 17 bytes on,
     * `mov ebx, [ebx+4]` takes the next node. */
    { "scene_update", "Customized_Render, an object's On_Frame_Update call",
      "8B 5F 7C 8D 47 78 3B D8 0F 84 ?? ?? ?? ?? 8B 73 0C 8D 46 F8 F7 DE 1B F6 23 F0 8B 06 8B CE "
      "FF 50 0C 83 F8 04 75 09 83 7D F4 00 89 75 FC 75 23 80 3D ?? ?? ?? ?? 00 75 1A 80 3D ?? ?? ?? "
      "?? 00 75 11 8B 4B 0C 8D 41 F8 F7 D9 1B C9 23 C8 8B 01 FF 50 34 8B 5B 04 8D 47 78 3B D8 75 B2",
      65, 6, 0, PF_GROUP_HOLD, 1, 0, 0x0006FF49 },
    /* RTS3DScene::renderOneObject (thiscall: render info, object, local
     * player, unused; ret 0x10), wrapped for its time per pass. Its light
     * environment lives at [EBP-0x2A0]; ltsites.c checks the code between
     * the sites below that the plugin relies on. */
    { "roo", "RTS3DScene::renderOneObject",
      "B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 81 EC 94 02 00 00 53 56 8B F1 57 33 FF 8D",
      0, 5, 16, PF_GROUP_LTIME, 0, 0, 0x0006F48E },
    /* The light environment's Reset with the object's centre and ambient:
     * where its build starts, for an object with a drawable... */
    { "roo_reset", "renderOneObject, the light environment's Reset (drawable)",
      "8D 86 50 01 00 00 89 45 D8 8D 45 C0 50 8D 45 88 50 8D 8D 60 FD FF FF E8 ?? ?? ?? ?? 8B CF 8D "
      "9F 9C 00 00 00",
      9, 7, 0, PF_GROUP_LTIME, 1, 0, 0x0006F735 },
    /* ...the loop that adds the global lights (tinted) for such an object... */
    { "roo_glights", "renderOneObject, the global lights loop",
      "F3 0F 11 45 E4 33 DB 39 9E 60 01 00 00 0F 8E ?? ?? ?? ?? 8B 45 D8 8D 3C 98",
      5, 8, 0, PF_GROUP_LIGHTS, 1, 0, 0x0006F8D4 },
    /* ...and Reset followed by the global lights for an object without one. */
    { "roo_reset_nd", "renderOneObject, the light environment's Reset (no drawable)",
      "EB 03 8B 45 D8 89 45 D8 8D 45 C0 50 8D 45 88 50 8D 8D 60 FD FF FF E8 ?? ?? ?? ?? 33 DB 83 3D",
      8, 7, 0, PF_GROUP_LIGHTS, 1, 0, 0x0006FB11 },
    /* The scene's light list, then its dynamic lights: each point light the
     * object's bounding sphere touches, and every other light, is added. */
    { "roo_lists", "renderOneObject, the scene and dynamic light lists",
      "80 7D F0 00 0F 85 ?? ?? ?? ?? 8B 9E 94 00 00 00 EB 6F 8B 7B 0C 8D 47 F8 F7 DF 1B FF 23 F8",
      10, 6, 0, PF_GROUP_LIGHTS, 1, 0, 0x0006FC16 },
    { "roo_add_static", "renderOneObject, Add_Light for a scene light",
      "84 C0 59 59 74 13 57 8D 8D 60 FD FF FF E8 ?? ?? ?? ?? C7 45 AC 01 00 00 00 8B 5B 04 8D 86 90 "
      "00 00 00",
      6, 7, 0, PF_GROUP_LTIME, 1, 0, 0x0006FC77 },
    { "roo_add_dyn", "renderOneObject, Add_Light for a dynamic light",
      "F7 D9 1B C9 23 C8 51 8D 8D 60 FD FF FF E8 ?? ?? ?? ?? C7 45 AC 01 00 00 00 8B 5B 04 8D 86 18 "
      "01 00 00",
      6, 7, 0, PF_GROUP_LTIME, 1, 0, 0x0006FD12 },
    /* The render info gets its light environment (EAX): the build's end. */
    { "roo_env", "renderOneObject, the render info's light environment set",
      "8D 85 60 FD FF FF 89 47 28 8B 45 9C 85 C0 74 1D 80 B8 E4 00 00 00 00 74",
      6, 6, 0, PF_GROUP_LIGHTS, 1, 0, 0x0006FD8A },
    /* robj->Render(rinfo), the usual call and the partly-fogged one. */
    { "roo_render", "renderOneObject, the object's Render call",
      "E8 ?? ?? ?? ?? EB 0D 8B 03 57 8B CB FF 50 30 EB 03 8B 7D 08 83 67 28 00",
      7, 5, 0, PF_GROUP_LTIME, 1, 0, 0x0006FE39 },
    { "roo_render2", "renderOneObject, the object's Render call (shroud pass)",
      "8B CF E8 ?? ?? ?? ?? 8B 03 57 8B CB FF 50 30 A1 ?? ?? ?? ?? 85 C0 74 10",
      7, 5, 0, PF_GROUP_LTIME, 1, 0, 0x0006FE0F },
    /* DX8Wrapper::Set_Light_Environment (cdecl, one argument): the device's
     * fixed-function lights and ambient. Its own group: the renderOneObject
     * timers and ShadowBatchIgnoreLight both need it. */
    { "lenv_set", "DX8Wrapper::Set_Light_Environment",
      "55 8B EC 81 EC 88 00 00 00 8B 45 08 39 05 ?? ?? ?? ?? 75 0D 85 C0 74 09",
      0, 9, 0, PF_GROUP_LENV, 1, 0, 0x001228A0 },
};
