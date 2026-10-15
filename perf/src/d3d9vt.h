/* The few Direct3D 9 interfaces the plugin calls, through the vtables of the
 * objects the game already created (no d3d9 import, no SDK header: the
 * build has no floating point, and Clear's depth is passed as its bits).
 * Slot numbers are checked against the SDK header in tests/test_inst.c. */
#ifndef PF_D3D9VT_H
#define PF_D3D9VT_H

#include <windows.h>

/* IDirect3DDevice9 */
#define PF_DEV_RELEASE 2
#define PF_DEV_GET_DEVICE_CAPS 7
#define PF_DEV_GET_BACK_BUFFER 18
#define PF_DEV_CREATE_VERTEX_BUFFER 26
#define PF_DEV_CREATE_RENDER_TARGET 28
#define PF_DEV_CREATE_DEPTH_STENCIL 29
#define PF_DEV_GET_RENDER_TARGET_DATA 32
#define PF_DEV_CREATE_OFFSCREEN_PLAIN 36
#define PF_DEV_SET_RENDER_TARGET 37
#define PF_DEV_GET_RENDER_TARGET 38
#define PF_DEV_SET_DEPTH_STENCIL 39
#define PF_DEV_GET_DEPTH_STENCIL 40
#define PF_DEV_CLEAR 43
#define PF_DEV_SET_VIEWPORT 47
#define PF_DEV_GET_VIEWPORT 48
#define PF_DEV_SET_RENDER_STATE 57
#define PF_DEV_GET_RENDER_STATE 58
#define PF_DEV_SET_SCISSOR 75
#define PF_DEV_GET_SCISSOR 76
#define PF_DEV_DRAW_INDEXED 82
#define PF_DEV_CREATE_DECL 86
#define PF_DEV_SET_DECL 87
#define PF_DEV_GET_DECL 88
#define PF_DEV_CREATE_VS 91
#define PF_DEV_SET_VS 92
#define PF_DEV_GET_VS 93
#define PF_DEV_SET_VS_CONST_F 94
#define PF_DEV_GET_VS_CONST_F 95
#define PF_DEV_SET_STREAM 100
#define PF_DEV_GET_STREAM 101
#define PF_DEV_SET_STREAM_FREQ 102
#define PF_DEV_GET_STREAM_FREQ 103
#define PF_DEV_SLOTS 104

/* IUnknown */
#define PF_UNK_ADDREF 1
#define PF_UNK_RELEASE 2
/* IDirect3DVertexShader9::GetFunction, IDirect3DVertexDeclaration9::GetDeclaration */
#define PF_VS_GET_FUNCTION 4
#define PF_DECL_GET_DECLARATION 4
/* IDirect3DVertexBuffer9 */
#define PF_VB_LOCK 11
#define PF_VB_UNLOCK 12
/* IDirect3DSurface9 */
#define PF_SURF_GET_DESC 12
#define PF_SURF_LOCK_RECT 13
#define PF_SURF_UNLOCK_RECT 14

/* D3DCAPS9 */
#define PF_CAPS_SIZE 304
#define PF_CAPS_VS_VERSION 196
#define PF_CAPS_MAX_VS_CONST 200

#define PF_D3DDECLTYPE_FLOAT1 0
#define PF_D3DDECLTYPE_UNUSED 17
#define PF_D3DDECLUSAGE_TEXCOORD 5
#define PF_STREAM_INDEXED 0x40000000u
#define PF_STREAM_INSTANCE 0x80000000u
#define PF_D3DPOOL_DEFAULT 0
#define PF_D3DPOOL_MANAGED 1
#define PF_D3DPOOL_SYSTEMMEM 2
#define PF_D3DUSAGE_WRITEONLY 8
#define PF_D3DFMT_A8R8G8B8 21
#define PF_D3DFMT_D24S8 75
#define PF_D3DFMT_R32F 114 /* the shadow map */
#define PF_D3DCLEAR_ALL 7 /* target, z-buffer, stencil */
#define PF_D3DLOCK_READONLY 0x10
#define PF_D3DRS_LIGHTING 137
#define PF_FLOAT_ONE 0x3F800000u

typedef struct {
    WORD stream, offset;
    BYTE type, method, usage, usage_index;
} pf_vertex_element;

typedef struct {
    DWORD x, y, width, height, min_z, max_z; /* min_z, max_z: float bits */
} pf_viewport;

typedef struct {
    int pitch;
    void *bits;
} pf_locked_rect;

typedef struct {
    UINT format, type;
    DWORD usage;
    UINT pool, multisample;
    DWORD quality;
    UINT width, height;
} pf_surface_desc;

#define PF_VT(obj) (*(void ***)(obj))

typedef ULONG(__stdcall *pf_release_fn)(void *self);
typedef HRESULT(__stdcall *pf_get_caps_fn)(void *dev, void *caps);
typedef HRESULT(__stdcall *pf_create_vb_fn)(void *dev, UINT len, DWORD usage, DWORD fvf, UINT pool,
                                            void **vb, HANDLE *shared);
typedef HRESULT(__stdcall *pf_create_rt_fn)(void *dev, UINT w, UINT h, UINT fmt, UINT ms,
                                            DWORD quality, BOOL lockable_or_discard, void **surf,
                                            HANDLE *shared);
typedef HRESULT(__stdcall *pf_create_plain_fn)(void *dev, UINT w, UINT h, UINT fmt, UINT pool,
                                               void **surf, HANDLE *shared);
typedef HRESULT(__stdcall *pf_two_ptr_fn)(void *dev, void *a, void *b);
typedef HRESULT(__stdcall *pf_set_rt_fn)(void *dev, DWORD index, void *surf);
typedef HRESULT(__stdcall *pf_get_rt_fn)(void *dev, DWORD index, void **surf);
typedef HRESULT(__stdcall *pf_ptr_fn)(void *dev, void *p);
typedef HRESULT(__stdcall *pf_clear_fn)(void *dev, DWORD count, const void *rects, DWORD flags,
                                        DWORD color, DWORD z_bits, DWORD stencil);
typedef HRESULT(__stdcall *pf_dip_fn)(void *dev, UINT type, INT base, UINT min_index, UINT verts,
                                      UINT start, UINT prims);
typedef HRESULT(__stdcall *pf_create_obj_fn)(void *dev, const void *desc, void **obj);
typedef HRESULT(__stdcall *pf_const_f_fn)(void *dev, UINT reg, void *data, UINT count);
typedef HRESULT(__stdcall *pf_set_stream_fn)(void *dev, UINT stream, void *vb, UINT offset,
                                             UINT stride);
typedef HRESULT(__stdcall *pf_get_stream_fn)(void *dev, UINT stream, void **vb, UINT *offset,
                                             UINT *stride);
typedef HRESULT(__stdcall *pf_set_freq_fn)(void *dev, UINT stream, UINT setting);
typedef HRESULT(__stdcall *pf_get_freq_fn)(void *dev, UINT stream, UINT *setting);
typedef HRESULT(__stdcall *pf_get_data_fn)(void *obj, void *data, UINT *size);
typedef HRESULT(__stdcall *pf_vb_lock_fn)(void *vb, UINT offset, UINT size, void **data,
                                          DWORD flags);
typedef HRESULT(__stdcall *pf_unlock_fn)(void *obj);
typedef HRESULT(__stdcall *pf_lock_rect_fn)(void *surf, pf_locked_rect *lr, const RECT *r,
                                            DWORD flags);

#endif
