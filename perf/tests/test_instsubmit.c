/* Failure-result contract of the actual production submission object.
 * The real-GPU fixture covers images; this mock makes persistent rejection
 * and an ordinary replay failing part-way through deterministic. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "instsubmit.h"

#define CHECK(x) do { if(!(x)){fprintf(stderr,"submit refused line %u: %s\n",__LINE__,#x);exit(1);} } while(0)
typedef struct {
    void *vs,*decl,*vb[16];
    unsigned offset[16],stride[16],freq[16],constants[256*4];
} State;
typedef struct {unsigned op,first,times;} Fault;
static State state,before;
static pf_submit transaction;
static pf_submit_desc desc;
static Fault faults[2];
static unsigned calls[23],total,instanced,ordinary;
static unsigned staged[256*4];
static void *vt[PF_DEV_SLOTS];
static struct {void **vt;} device={vt};

static int rejected(void)
{
    unsigned op=transaction.operation;
    CHECK(op && op<23);calls[op]++;total++;
    for(unsigned i=0;i<2;i++)
        if(faults[i].op==op && calls[op]>=faults[i].first &&
           calls[op]-faults[i].first<faults[i].times) return 1;
    return 0;
}
static HRESULT WINAPI constants_get(void *self,unsigned first,void *data,unsigned count)
{
    CHECK(self==&device && first+count<=256);
    if(rejected())return E_FAIL;
    memcpy(data,state.constants+first*4,count*16);return S_OK;
}
static HRESULT WINAPI constants_set(void *self,unsigned first,void *data,unsigned count)
{
    CHECK(self==&device && first+count<=256);
    if(rejected())return E_FAIL;
    memcpy(state.constants+first*4,data,count*16);return S_OK;
}
static HRESULT WINAPI shader(void *self,void *value)
{ CHECK(self==&device);if(rejected())return E_FAIL;state.vs=value;return S_OK; }
static HRESULT WINAPI decl(void *self,void *value)
{ CHECK(self==&device);if(rejected())return E_FAIL;state.decl=value;return S_OK; }
static HRESULT WINAPI frequency(void *self,unsigned stream,unsigned value)
{ CHECK(self==&device && stream<16);if(rejected())return E_FAIL;state.freq[stream]=value;return S_OK; }
static HRESULT WINAPI stream(void *self,unsigned index,void *value,unsigned offset,unsigned stride)
{
    CHECK(self==&device && index<16);if(rejected())return E_FAIL;
    state.vb[index]=value;
    /* Model native D3D9 retaining these when the stream becomes null. */
    if(value){state.offset[index]=offset;state.stride[index]=stride;}
    return S_OK;
}
static HRESULT WINAPI draw(void *self,unsigned type,int base,unsigned min,unsigned verts,unsigned start,unsigned prims)
{
    CHECK(self==&device && type==4 && base==7 && min==0 && verts==90 && start==6 && prims==30);
    if(rejected())return E_FAIL;
    if(state.freq[0]&PF_STREAM_INDEXED)instanced+=state.freq[0]&0x3fffffff;
    else {
        CHECK(state.vs==desc.game_vs && state.decl==desc.game_decl && state.freq[0]==1);
        CHECK(state.constants[desc.bone_register*4]==staged[ordinary*desc.bone_registers*4]);
        ordinary++;
    }
    return S_OK;
}
static void reset(void)
{
    memset(&transaction,0,sizeof transaction);memset(&state,0,sizeof state);
    memset(faults,0,sizeof faults);memset(calls,0,sizeof calls);total=instanced=ordinary=0;
    vt[PF_DEV_GET_VS_CONST_F]=constants_get;vt[PF_DEV_SET_VS_CONST_F]=constants_set;
    vt[PF_DEV_SET_VS]=shader;vt[PF_DEV_SET_DECL]=decl;
    vt[PF_DEV_SET_STREAM_FREQ]=frequency;vt[PF_DEV_SET_STREAM]=stream;vt[PF_DEV_DRAW_INDEXED]=draw;
    for(unsigned i=0;i<256*4;i++){state.constants[i]=i+1000;staged[i]=i+10000;}
    for(unsigned i=0;i<16;i++){state.freq[i]=1;state.vb[i]=(void *)(i+10);state.offset[i]=12;state.stride[i]=16;}
    state.vs=(void *)1;state.decl=(void *)3;
    desc=(pf_submit_desc){.dev=&device,.game_vs=(void *)1,.our_vs=(void *)2,
        .game_decl=(void *)3,.our_decl=(void *)4,.instance_vb=(void *)5,.previous_vb=state.vb[1],
        .instance_stream=1,.input_streams=1,.instance_offset=64,.previous_offset=12,.previous_stride=16,
        .instance_register=100,.instance_registers=32,.bone_register=20,.bone_registers=4,
        .dip={4,7,0,90,6,30}};
}
static void capture(void)
{ CHECK(pf_submit_capture(&transaction,&desc)==S_OK && transaction.valid);before=state; }
static void restored(void)
{ CHECK(!memcmp(&before,&state,sizeof state));CHECK(!transaction.open && !transaction.operation); }

int main(void)
{
    pf_submit_result r;unsigned cases=0;
    reset();capture();r=pf_submit_batch(&transaction,staged,3);
    CHECK(r.error==S_OK && r.committed==3 && !r.replayed && r.coherent && r.complete && transaction.valid);
    CHECK(instanced==3 && !ordinary);restored();cases++;

    reset();capture();faults[0]=(Fault){PF_SUBMIT_SAVE,1,1};r=pf_submit_batch(&transaction,staged,3);
    CHECK(FAILED(r.error) && r.failed_operation==PF_SUBMIT_SAVE && !r.committed && r.replayed==3 && r.coherent && r.complete && !transaction.valid);
    CHECK(!instanced && ordinary==3);restored();cases++;

    reset();capture();faults[0]=(Fault){PF_SUBMIT_RESTORE_SHADER,1,2};r=pf_submit_batch(&transaction,staged,3);
    CHECK(FAILED(r.error) && r.committed==3 && !r.replayed && !r.coherent && r.complete && !transaction.valid);
    CHECK(instanced==3 && !ordinary && state.vs==desc.our_vs && calls[PF_SUBMIT_RESTORE_SHADER]==2);cases++;

    reset();capture();faults[0]=(Fault){PF_SUBMIT_CONSTANTS,1,1};faults[1]=(Fault){PF_SUBMIT_RESTORE_SHADER,1,2};
    r=pf_submit_batch(&transaction,staged,3);
    CHECK(FAILED(r.error) && r.failed_operation==PF_SUBMIT_CONSTANTS && !r.committed && !r.replayed && !r.coherent && !r.complete);
    CHECK(!instanced && !ordinary && !transaction.valid);cases++;

    reset();capture();faults[0]=(Fault){PF_SUBMIT_PENDING_DRAW,2,1};r=pf_submit_pending(&transaction,staged,3);
    CHECK(FAILED(r.error) && r.failed_operation==PF_SUBMIT_PENDING_DRAW && r.replayed==1 && r.coherent && !r.complete && !transaction.valid);
    CHECK(!instanced && ordinary==1);restored();cases++;

    reset();capture();faults[0]=(Fault){PF_SUBMIT_DRAW,1,1};faults[1]=(Fault){PF_SUBMIT_REPLAY_DRAW,2,1};
    r=pf_submit_batch(&transaction,staged,3);
    CHECK(FAILED(r.error) && r.failed_operation==PF_SUBMIT_DRAW && !r.committed && r.replayed==1 && r.coherent && !r.complete && !transaction.valid);
    CHECK(!instanced && ordinary==1);restored();cases++;

    for(unsigned failures=1;failures<=2;failures++) {
        reset();capture();faults[0]=(Fault){PF_SUBMIT_PENDING_RESTORE,1,failures};r=pf_submit_pending(&transaction,staged,3);
        CHECK(FAILED(r.error) && r.replayed==3 && r.complete && !transaction.valid && r.coherent==(failures==1));
        CHECK(calls[PF_SUBMIT_PENDING_RESTORE]==2);
        if(failures==1)restored();else CHECK(memcmp(&before,&state,sizeof state));cases++;
    }

    reset();faults[0]=(Fault){PF_SUBMIT_CAPTURE_BONES,1,1};before=state;
    CHECK(FAILED(pf_submit_capture(&transaction,&desc)) && !transaction.valid);restored();cases++;

    reset();desc.previous_vb=NULL;state.vb[1]=NULL;capture();r=pf_submit_batch(&transaction,staged,3);
    CHECK(r.error==S_OK && r.complete && calls[PF_SUBMIT_SEED_NULL_STREAM]==1);restored();cases++;

    reset();desc.instance_stream=3;desc.input_streams=5;desc.previous_vb=state.vb[3];capture();
    faults[0]=(Fault){PF_SUBMIT_INPUT_FREQ,2,1};r=pf_submit_batch(&transaction,staged,3);
    CHECK(FAILED(r.error) && r.replayed==3 && r.coherent && r.complete && calls[PF_SUBMIT_RESTORE_INPUT_FREQ]==2);restored();cases++;

    for(unsigned invalid=0;invalid<6;invalid++) {
        reset();before=state;
        if(invalid==0)desc.instance_register=~0u;
        if(invalid==1)desc.instance_registers=257;
        if(invalid==2)desc.bone_registers=3;
        if(invalid==3)desc.bone_register=255;
        if(invalid==4)desc.input_streams=2;
        if(invalid==5)desc.instance_stream=16;
        CHECK(pf_submit_capture(&transaction,&desc)==E_INVALIDARG && !transaction.valid && !total);restored();cases++;
    }
    reset();capture();unsigned prior=total;r=pf_submit_batch(&transaction,staged,9);
    CHECK(r.error==E_INVALIDARG && r.coherent && !r.complete && total==prior);restored();cases++;
    transaction.open=1;r=pf_submit_pending(&transaction,staged,1);
    CHECK(r.error==E_UNEXPECTED && !r.coherent && !r.complete && total==prior && transaction.open);cases++;
    printf("SUBMIT_RESULT,cases,%u\n",cases);return 0;
}
