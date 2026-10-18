/* Exercise production capture/submission against COM objects that record
 * destruction. Tombstones stay allocated so a bad restore fails an oracle
 * rather than causing an unrelated access violation. No game or GPU starts. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/inst.c"

#define REQUIRE(c) do { if (!(c)) { fprintf(stderr,"ownership refused line %u: %s\n",__LINE__,#c);exit(1); } } while(0)
typedef struct { void **vt; unsigned refs, dead; } Object;
static void *object_vt[3], *device_vt[PF_DEV_SLOTS];
static struct { void **vt; } device={device_vt};
static Object native_vs, our_vs, native_decl, our_decl, native_vb, instance_vb;
static Object *bound_vs, *bound_decl, *bound_vb;
static unsigned dead_uses, draws, sets, get_calls, reject_stream, reject_shader;
static unsigned offset=12,stride=16;

void fu_log_event(const char *format,...) { (void)format; }
void *pf_skip_thunk(unsigned bytes) { return (void *)(bytes+1); }

static ULONG WINAPI obj_add(void *self)
{
    Object *o=self;
    if(o->dead){dead_uses++;return 0;}
    return ++o->refs;
}
static ULONG WINAPI obj_release(void *self)
{
    Object *o=self;
    if(o->dead || !o->refs){dead_uses++;return 0;}
    if(!--o->refs)o->dead=1;
    return o->refs;
}
static HRESULT get(Object *object,void *out)
{ *(void **)out=object;if(object)obj_add(object);return S_OK; }
static HRESULT WINAPI get_vs(void *dev,void *out) { (void)dev;return get(bound_vs,out); }
static HRESULT WINAPI get_decl(void *dev,void *out) { (void)dev;return get(bound_decl,out); }
static HRESULT bind(Object **slot,void *value)
{
    Object *o=value;
    if(o && o->dead){dead_uses++;return E_FAIL;}
    if(o)obj_add(o);
    if(*slot)obj_release(*slot);
    *slot=o;return S_OK;
}
static HRESULT WINAPI set_vs(void *dev,void *value) { (void)dev;return bind(&bound_vs,value); }
static HRESULT WINAPI set_decl(void *dev,void *value) { (void)dev;return bind(&bound_decl,value); }
static HRESULT WINAPI get_freq(void *dev,UINT stream,UINT *value)
{ (void)dev;(void)stream;*value=1;return S_OK; }
static HRESULT WINAPI set_freq(void *dev,UINT stream,UINT value)
{ (void)dev;(void)stream;(void)value;return S_OK; }
static HRESULT WINAPI get_stream(void *dev,UINT stream,void **out,UINT *off,UINT *step)
{
    (void)dev;REQUIRE(stream==1);*out=NULL;get_calls++;
    if(reject_stream)return E_FAIL;
    *off=offset;*step=stride;return get(bound_vb,out);
}
static HRESULT WINAPI set_stream(void *dev,UINT stream,void *value,UINT off,UINT step)
{
    (void)dev;REQUIRE(stream==1);sets++;
    HRESULT hr=bind(&bound_vb,value);
    if(SUCCEEDED(hr)){offset=off;stride=step;}
    return hr;
}
static HRESULT WINAPI constants(void *dev,UINT first,void *data,UINT count)
{ (void)dev;(void)first;(void)data;(void)count;return S_OK; }
static HRESULT WINAPI create_shader(void *dev,const void *code,void **out)
{ (void)dev;(void)code;*out=reject_shader?NULL:&our_vs;return reject_shader?E_FAIL:S_OK; }
static HRESULT WINAPI draw(void *dev,UINT type,INT base,UINT min,UINT verts,UINT start,UINT prims)
{ (void)dev;(void)type;(void)base;(void)min;(void)verts;(void)start;(void)prims;draws++;return S_OK; }

static void reset_case(int null_stream)
{
    pf_inst_init(0);
    object_vt[1]=(void *)obj_add;object_vt[2]=(void *)obj_release;
    native_vs=(Object){object_vt,2,0};our_vs=(Object){object_vt,1,0};
    native_decl=(Object){object_vt,2,0};our_decl=(Object){object_vt,1,0};
    native_vb=(Object){object_vt,null_stream?0u:1u,(unsigned)null_stream};
    instance_vb=(Object){object_vt,1,0};
    bound_vs=&native_vs;bound_decl=&native_decl;bound_vb=null_stream?NULL:&native_vb;
    offset=12;stride=16;dead_uses=draws=sets=get_calls=reject_stream=reject_shader=0;
    device_vt[PF_DEV_GET_VS]=(void *)get_vs;device_vt[PF_DEV_GET_DECL]=(void *)get_decl;
    device_vt[PF_DEV_SET_VS]=(void *)set_vs;device_vt[PF_DEV_SET_DECL]=(void *)set_decl;
    device_vt[PF_DEV_GET_STREAM_FREQ]=(void *)get_freq;device_vt[PF_DEV_SET_STREAM_FREQ]=(void *)set_freq;
    device_vt[PF_DEV_GET_STREAM]=(void *)get_stream;device_vt[PF_DEV_SET_STREAM]=(void *)set_stream;
    device_vt[PF_DEV_GET_VS_CONST_F]=device_vt[PF_DEV_SET_VS_CONST_F]=(void *)constants;
    device_vt[PF_DEV_CREATE_VS]=(void *)create_shader;device_vt[PF_DEV_DRAW_INDEXED]=(void *)draw;
    g=(group){0};g.mode=M_LEADER;g.armed=1;g.skinned=1;g.nbones=1;g.stride=2;
    g_dev=&device;g_ivb=&instance_vb;g_failed=0;g_can_read_consts=1;
    g_vs_n=1;g_vs[0]=(vs_entry){&native_vs,-1,-1};
    for(unsigned i=0;i<PF_INST_VARIANTS;i++)if(pf_inst_variants[i].joints && pf_inst_variants[i].family!=3){
        g_vs[0].variant=i;g.joints=pf_inst_variants[i].joints;g_ours[i]=&our_vs;g_ours_failed[i]=0;break;
    }
    REQUIRE(g_vs[0].variant>=0);
    g_decl=(pf_decl_cache){0};g_decl.count=1;
    g_decl.entries[0]=(pf_decl_entry){&native_decl,&our_decl,1,1,0};
}

static void capture_leader(void)
{
    void *slot[8]={0};slot[1]=&device;
    capture(slot);
}

static unsigned char model[192],geom[64],mesh[0x320],other[0x320],tree[24],pivot[0x58];
static unsigned char records[5*32],frame[0x60];
static unsigned method[2]={0,3};
static short bone;
static void *container_vt[PF_OBJ_GET_HTREE/4+1];
static struct { void **vt; } container={container_vt};
static void *__attribute__((thiscall)) get_tree(void *self) { (void)self;return tree; }

static void game_context(void)
{
    memset(model,0,sizeof model);memset(geom,0,sizeof geom);memset(mesh,0,sizeof mesh);
    *(unsigned *)(model+PF_MODEL_FLAGS)=PF_MODEL_SKIN;
    *(void **)(model+PF_MODEL_GEOMETRY)=geom;
    *(void **)(geom+PF_GEOM_BONES_BEGIN)=&bone;*(void **)(geom+PF_GEOM_BONES_END)=&bone+1;
    *(unsigned *)(geom+PF_GEOM_JOINTS)=g.joints;
    *(void **)(mesh+PF_MESH_MODEL)=model;*(void **)(mesh+PF_MESH_CONTAINER)=&container;
    container_vt[PF_OBJ_GET_HTREE/4]=(void *)get_tree;
    *(unsigned *)(tree+PF_HTREE_PIVOTS)=1;*(void **)(tree+PF_HTREE_PIVOTS+4)=pivot;
    memcpy(other,mesh,sizeof mesh);*(unsigned *)(other+PF_MESH_BASE_VERTEX)=1;
    g.model=model;g.bonemap=&bone;g.method=method;method[1]=3;
    memset(frame,0,sizeof frame);
    *(void **)(frame+0x40+PF_FRAME_GROUP_END)=records+sizeof records;
    *(unsigned *)(frame+0x40+PF_FRAME_PASS_COUNT)=1;
    *(unsigned *)(frame+0x40+PF_FRAME_MAX_INSTANCES)=1;
    g.end=records+sizeof records;
}

static void finished(void)
{
    REQUIRE(!g_stream_owner && native_vb.refs==1 && !native_vb.dead && !dead_uses);
    REQUIRE(SUCCEEDED(set_stream(&device,1,NULL,0,0)) && native_vb.dead && !dead_uses);
    REQUIRE(native_vs.refs==2 && native_decl.refs==2 && our_vs.refs==1 && our_decl.refs==1 && instance_vb.refs==1);
}

int main(void)
{
    reset_case(0);capture_leader();REQUIRE(!g.capture_reason);
    REQUIRE(native_vb.refs==2);
    g.count=2;REQUIRE(draw_batch(0));g.count=2;REQUIRE(draw_batch(0));
    REQUIRE(!dead_uses && !native_vb.dead && bound_vb==&native_vb);
    REQUIRE(offset==12 && stride==16 && draws==2 && native_vb.refs==2);
    pf_inst_init(0);finished();
    unsigned cases=1;

    reset_case(0);capture_leader();capture_leader();evaluate();
    REQUIRE(g.mode==M_STOCK && g.dips==2);finished();cases++;

    reset_case(0);reject_stream=1;capture_leader();
    REQUIRE(g.capture_reason && get_calls==1);finished();cases++;

    reset_case(0);reject_shader=1;g_ours[g_vs[0].variant]=NULL;capture_leader();
    REQUIRE(g.capture_reason && !get_calls);finished();cases++;

    reset_case(0);g.stride=MAX_REGS;capture_leader();
    REQUIRE(g.capture_reason==REASON(bones) && !get_calls);finished();cases++;

    reset_case(0);capture_leader();game_context();g.mode=M_INST;g_failed=1;
    REQUIRE(member(mesh,records+32,method,(void *)1)==(void *)1 && g.mode==M_STOCK);
    finished();cases++;

    /* A model/base-vertex split flushes before releasing the old owner. */
    for(unsigned remaining=2;remaining<=3;remaining++) {
        reset_case(0);capture_leader();game_context();g.mode=M_INST;g.count=2;
        const unsigned char *rec=g.end-remaining*PF_RECORD_SIZE;
        REQUIRE(member(other,rec,method,(void *)1)==(void *)1);
        REQUIRE(draws==1 && g.mode==(remaining==3?M_LEADER:M_STOCK));finished();cases++;
    }

    reset_case(0);capture_leader();game_context();g.mode=M_INST;
    begin_group(mesh,method,1,frame+0x40,records,(void *)1);
    REQUIRE(g.mode==M_LEADER);finished();cases++;

    /* Last normal member stages and draws through the actual mesh handler. */
    reset_case(0);capture_leader();game_context();g.mode=M_INST;
    void *stack[24]={0},**slot=stack+12;
    slot[-4]=mesh;slot[-8]=frame+0x40;slot[-10]=records+4*32;slot[-6]=records;
    slot[1]=method;slot[2]=(void *)1;
    REQUIRE(mesh_call(slot,(void *)1)==pf_skip_thunk(8));
    REQUIRE(g.mode==M_IDLE && draws==1 && method[1]==2);finished();cases++;

    reset_case(1);capture_leader();REQUIRE(!g.capture_reason && !g_stream_owner);
    g.count=2;REQUIRE(draw_batch(0));pf_inst_init(0);
    REQUIRE(!dead_uses && !bound_vb && instance_vb.refs==1);cases++;
    printf("OWNERSHIP,cases,%u,dead_uses,%u\n",cases,dead_uses);return 0;
}
