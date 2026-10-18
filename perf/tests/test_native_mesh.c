/* Isolated local native mesh -> triangles -> draw contract. No game startup,
 * real GPU or installed hook. Geometry setup, source callbacks and rendering
 * statistics are controlled dependencies; normal cleanup is native. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stubs.h"
#define THIS __attribute__((thiscall))
#define REQUIRE(c) do { if(!(c)) { fprintf(stderr,"native mesh refused line %d: %s (case %u)\n",__LINE__,#c,cases); exit(1); } } while(0)
static unsigned cases;
static void *symbols[32];
static unsigned char mesh[0x320],model[0x100],geometry[0x40],material[0x120];
static unsigned char container[16],tree[24],sas[0xE0],state[0x290],vb[16];
static void *mesh_vt[60],*container_vt[60],*method_vt[8],*device_vt[83];
static struct {void **vt;unsigned refs;} method,device;
static unsigned changed,one=0x3F800000,threshold=0x3DCCCCCD,limit,draw_count,api_count;
static unsigned char emissive_mode,enabled,snapshot;
static void *sas_ptr=sas,*device_ptr=&device;
static void *anchor;
void **leader_slot;
static unsigned optimized,callback_kind,callback_depth,eligible,observed,pending,deferred;
static unsigned geometry_calls,dynamic_calls,validations,statistics,sorted,deletions,fault_mode;
static unsigned weak_nested,frame_witnesses,gpu_state[4];
static unsigned hook_mode;
static void **wrapped_slot;
static void *wrapped_return;
typedef struct {unsigned args[6],gpu[4];} Draw;
static Draw actual[8],saved;
static unsigned actual_n;
static unsigned u32(const void *p,unsigned off) {return *(const unsigned*)((const char*)p+off);}
static void put(void *p,unsigned off,unsigned v) {*(unsigned*)((char*)p+off)=v;}
static void pointer(void *p,unsigned off,void *v) {put(p,off,(unsigned)v);}
typedef void (THIS *mesh_fn)(void *,void *,unsigned);
typedef void (*tri_fn)(unsigned,unsigned,unsigned,unsigned);
typedef void (*draw_fn)(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);

static void record(const Draw *d)
{
    REQUIRE(actual_n<8); actual[actual_n++]=*d;
}

HRESULT __cdecl native_dip(void **slot)
{
    REQUIRE(slot[1]==&device);
    observed++;
    int chain=slot[0]==(char*)symbols[2]+(hook_mode?0x18A:0x18B) &&
              (slot[15]==(char*)symbols[1]+0x1D ||
               (hook_mode==2 && slot[15]==pf_exit_stubs[PF_DRAW])) &&
              slot[22]==(char*)symbols[0]+0x92F;
    int identity=slot+98==leader_slot;
    if(chain && identity) frame_witnesses++;
    if(chain && !identity) weak_nested++;
    Draw d={0};
    memcpy(d.args,slot+2,sizeof d.args); memcpy(d.gpu,gpu_state,sizeof d.gpu);
    int witness=chain && (fault_mode==1 || (fault_mode==2?slot+99==leader_slot:identity));
    if(optimized && eligible && observed==1 && witness) {
        /* Independent oracle: a weak return-address-only gate must fail
         * on a recursive call to the same native mesh body. */
        REQUIRE(chain && identity && !pending);
        saved=d; pending=1; deferred++;
    } else record(&d);
    return S_OK;
}

void dip_entry(void);
/* Use the real general-register stubs. The tiny C dispatcher models only
 * the instancing bypass and Draw return wrapping from timing.c; it does
 * not claim to test that file's complete accounting implementation. */
void *pf_enter(unsigned k,void **slot)
{
    if(k==PF_INST_MESH) {REQUIRE(slot==leader_slot);return symbols[0];}
    if(k==PF_INST_DIP) return dip_entry;
    REQUIRE(k==PF_DRAW && hook_mode==2);
    if(!wrapped_slot) {
        wrapped_slot=slot; wrapped_return=*slot; *slot=pf_exit_stubs[PF_DRAW];
    }
    return symbols[2];
}
void *pf_exit(unsigned k,void **slot)
{
    REQUIRE(k==PF_DRAW && wrapped_slot && slot==wrapped_slot+1);
    void *ret=wrapped_return; wrapped_slot=NULL; wrapped_return=NULL; return ret;
}
void native_mesh_call(void *,void *,void *,unsigned);
unsigned mesh_abi_errors;
__asm__(
".text\n.globl _dip_entry\n_dip_entry:\npushl %esp\ncall _native_dip\naddl $4,%esp\nret $28\n"
".globl _native_mesh_call\n_native_mesh_call:\n"
"pushl %ebp\nmovl %esp,%ebp\npushl %ebx\npushl %esi\npushl %edi\nsubl $8,%esp\n"
"movl %esp,-16(%ebp)\nmovl %fs:0,%eax\nmovl %eax,-20(%ebp)\n"
"movl 8(%ebp),%eax\nmovl 12(%ebp),%ecx\npushl 20(%ebp)\npushl 16(%ebp)\n"
"leal -4(%esp),%edx\nmovl %edx,_leader_slot\n"
"movl $0x13579bdf,%ebx\nmovl $0x2468ace0,%esi\nmovl $0x76543210,%edi\ncall *%eax\n"
"cmpl -16(%ebp),%esp\nje 1f\nincl _mesh_abi_errors\n1:\n"
"cmpl $0x13579bdf,%ebx\nje 2f\nincl _mesh_abi_errors\n2:\n"
"cmpl $0x2468ace0,%esi\nje 3f\nincl _mesh_abi_errors\n3:\n"
"cmpl $0x76543210,%edi\nje 4f\nincl _mesh_abi_errors\n4:\n"
"movl %fs:0,%eax\ncmpl -20(%ebp),%eax\nje 5f\nincl _mesh_abi_errors\n5:\n"
"leal -12(%ebp),%esp\npopl %edi\npopl %esi\npopl %ebx\npopl %ebp\nret\n");

static void THIS deleted(void *self) {REQUIRE(self==&method && !method.refs);deletions++;}
static void *THIS get_tree(void *self) {REQUIRE(self==container);return *(void**)(container+4);}
static void nested(unsigned kind)
{
    if(callback_kind!=kind || callback_depth) return;
    callback_depth++;
    if(kind==4) {
        method.refs++;
        /* Direct nested native call: leave the outer recorded call-slot
         * identity intact. Both return-address tests alone still match. */
        ((mesh_fn)symbols[0])(mesh,&method,1);
    } else ((tri_fn)symbols[1])(0,2,0,9);
    callback_depth--;
}
static void THIS dynamic(void *self,unsigned channel)
{
    REQUIRE(self==&method && channel==1); dynamic_calls++;
    nested(1); nested(4);
    gpu_state[0]=0x12345678; gpu_state[1]=u32(state,0x1EC);
    gpu_state[2]=u32(model,0x18); gpu_state[3]=0xABCDEF01;
}
static void THIS geometry_begin(void *self,unsigned base)
{
    REQUIRE(self==geometry && base==7); geometry_calls++; nested(2);
}
static void THIS emissive(void *self,unsigned *out)
{
    REQUIRE(self==material); out[0]=out[1]=out[2]=u32(material,0);
}
static void apply_state(void) {nested(3);}
static void stats(unsigned polys,unsigned vertices,void *s)
{
    REQUIRE(s==state); statistics+=polys+vertices;
}
static void sorting(unsigned prim,unsigned start,unsigned polys,unsigned first,unsigned vertices)
{
    REQUIRE(prim==4); sorted++;
    Draw d={{prim,0,first,vertices,start,polys},{0}};
    memcpy(d.gpu,gpu_state,sizeof d.gpu); record(&d);
}
static HRESULT WINAPI validate(void *self,unsigned *passes)
{
    REQUIRE(self==&device); *passes=1; validations++; return S_OK;
}
static void *THIS row_constructor(void *self) {return self;}
static void WINAPI array_constructor(void *start,unsigned stride,int count,void *(THIS *ctor)(void *))
{
    REQUIRE(stride==16 && count==4 && ctor==row_constructor);
    for(int i=0;i<count;i++) REQUIRE(ctor((char*)start+i*stride)==(char*)start+i*stride);
}
static void unexpected(void) {REQUIRE(!"unexercised helper or native exception");}

typedef struct {
    unsigned skin,flag,container,count,vertex_kind,index_kind,callback,refs,snapshot;
    unsigned early,disabled,limit,emissive,vertex_count,anchor_self;
} Scenario;
typedef struct {
    unsigned state[0x290/4],sas[0xE0/4],gpu[4]; Draw draws[8];
    unsigned count,changed,draw_count,api_count,refs,geometry,dynamic,validate,stats,sorted,deleted;
} Result;
static Result baseline;

static void run(Scenario s,unsigned fast)
{
    memset(mesh,0,sizeof mesh); memset(model,0,sizeof model); memset(geometry,0,sizeof geometry);
    memset(material,0,sizeof material); memset(state,0,sizeof state); memset(sas,0x5A,sizeof sas);
    memset(actual,0,sizeof actual); memset(gpu_state,0,sizeof gpu_state);
    changed=draw_count=api_count=0; enabled=!s.disabled; limit=s.limit;
    snapshot=(unsigned char)s.snapshot; emissive_mode=s.emissive!=0;
    optimized=fast; callback_kind=s.callback; callback_depth=0;
    eligible=s.refs>=2 && s.count==1 && !s.flag;
    observed=pending=deferred=actual_n=geometry_calls=dynamic_calls=validations=statistics=sorted=deletions=0;
    method.vt=method_vt; method.refs=s.refs; device.vt=device_vt;
    pointer(mesh,0,mesh_vt); pointer(container,0,container_vt);
    pointer(container,4,s.container==2?tree:NULL);
    pointer(mesh,0x7C,s.container?container:NULL);
    pointer(mesh,0xC4,s.early==1?NULL:model);
    pointer(model,0x94,material); pointer(model,0xBC,s.early==3?NULL:geometry);
    pointer(material,0xB8,s.early==2?NULL:geometry);
    if(s.early==5) {pointer(material,0xB8,NULL);pointer(material,0x108,geometry);}
    put(material,0,s.emissive==2?0:0x3F800000);
    pointer(state,4,s.emissive==3?NULL:material);
    put(model,0x18,s.skin?0x400:0); put(model,0x24,2); put(model,0x28,s.vertex_count);
    put(mesh,0x300,7); geometry[0]=(unsigned char)s.flag;
    put(geometry,0x20,1); put(mesh,0x18,one); put(mesh,0x18+20,one); put(mesh,0x18+40,one);
    anchor=mesh; pointer(mesh,0x310,s.anchor_self?&anchor:NULL);
    put(state,0x26C,s.vertex_kind); put(state,0x274,s.index_kind); pointer(state,0x280,vb);
    *(unsigned short*)(vb+12)=32;
    *(unsigned short*)(state+0x278)=3; *(unsigned short*)(state+0x27A)=19;
    *(unsigned short*)(state+0x27C)=5; *(unsigned short*)(state+0x28C)=4;
    native_mesh_call(hook_mode?pf_enter_stubs[PF_INST_MESH]:symbols[0],mesh,s.early==4?NULL:&method,s.count);
    REQUIRE(!mesh_abi_errors && !wrapped_slot && method.refs==s.refs-(s.early!=4));
    REQUIRE(deletions==(s.early!=4 && s.refs==1));
    if(pending) {
        REQUIRE(!memcmp(saved.gpu,gpu_state,sizeof gpu_state));
        record(&saved); pending=0;
    }
    if(fast && eligible && !s.early && !s.disabled && !s.limit && !s.emissive &&
       !s.callback && (s.vertex_kind==0 || s.vertex_kind==2) &&
       (s.index_kind==0 || s.index_kind==2)) REQUIRE(deferred==1);
    if(s.callback || !eligible || s.early==1 || s.early==2 || s.early==3 || s.early==4 ||
       s.disabled || s.limit>=2*s.count || s.emissive==2 || s.emissive==3)
        REQUIRE(!deferred);
    Result result={0};
    memcpy(result.state,state,sizeof state); memcpy(result.sas,sas,sizeof sas);
    memcpy(result.gpu,gpu_state,sizeof gpu_state); memcpy(result.draws,actual,sizeof actual);
    result.count=actual_n; result.changed=changed; result.draw_count=draw_count; result.api_count=api_count;
    result.refs=method.refs; result.geometry=geometry_calls; result.dynamic=dynamic_calls;
    result.validate=validations; result.stats=statistics; result.sorted=sorted; result.deleted=deletions;
    if(!fast) baseline=result; else REQUIRE(!memcmp(&baseline,&result,sizeof result));
    cases++;
}

#include "native_mesh_image.h"
static void load(const char *image,const char *manifest)
{
    for(unsigned i=7;i<19;i++) symbols[i]=unexpected;
    symbols[9]=array_constructor; symbols[17]=row_constructor;
    symbols[12]=geometry_begin; symbols[13]=emissive; symbols[14]=apply_state;
    symbols[15]=stats; symbols[16]=sorting;
    void *data[]={&changed,state,&one,&sas_ptr,&limit,&emissive_mode,&enabled,&snapshot,
                  &device_ptr,&draw_count,&api_count,&threshold};
    for(unsigned i=0;i<12;i++) symbols[19+i]=data[i];
    native_mesh_load(symbols,image,manifest);
    method_vt[0]=deleted; method_vt[4]=dynamic; container_vt[58]=get_tree;
    device_vt[70]=validate; device_vt[82]=dip_entry;
}

int main(int argc,char **argv)
{
    REQUIRE(argc==3 || argc==4); if(argc==4) fault_mode=(unsigned)atoi(argv[3]);
    load(argv[1],argv[2]);
    for(hook_mode=0;hook_mode<3;hook_mode++) {
    if(hook_mode==1) {
        unsigned char *site=(unsigned char*)symbols[2]+0x185; DWORD old;
        REQUIRE(VirtualProtect(symbols[2],4096,PAGE_READWRITE,&old));
        site[0]=0xE8; *(unsigned*)(site+1)=(unsigned)pf_enter_stubs[PF_INST_DIP]-(unsigned)(site+5); site[5]=0x90;
        REQUIRE(VirtualProtect(symbols[2],4096,PAGE_EXECUTE_READ,&old));
        REQUIRE(FlushInstructionCache(GetCurrentProcess(),symbols[2],4096));
    }
    if(hook_mode==2) {
        unsigned char *field=(unsigned char*)symbols[1]+0x19; DWORD old;
        REQUIRE(VirtualProtect(symbols[1],4096,PAGE_READWRITE,&old));
        *(unsigned*)field=(unsigned)pf_enter_stubs[PF_DRAW]-(unsigned)(field+4);
        REQUIRE(VirtualProtect(symbols[1],4096,PAGE_EXECUTE_READ,&old));
        REQUIRE(FlushInstructionCache(GetCurrentProcess(),symbols[1],4096));
    }
    for(unsigned skin=0;skin<2;skin++) for(unsigned flag=0;flag<2;flag++)
    for(unsigned c=0;c<3;c++) for(unsigned count=0;count<3;count++)
    for(unsigned v=0;v<4;v++) for(unsigned i=0;i<4;i++)
    for(unsigned cb=0;cb<5;cb++) for(unsigned refs=1;refs<=2;refs++) {
        Scenario s={skin,flag,c,count,v,i,cb,refs,v&1,0,0,0,0,9,c==2};
        run(s,0); run(s,1);
    }
    for(unsigned skin=0;skin<2;skin++) for(unsigned early=0;early<=5;early++)
    for(unsigned disabled=0;disabled<2;disabled++) for(unsigned l=0;l<2;l++)
    for(unsigned e=0;e<4;e++) for(unsigned n=0;n<3;n++) {
        Scenario s={skin,0,2,1,0,0,0,2,1,early,disabled,2*l,e,n,0};
        run(s,0); run(s,1);
    }
    }
    REQUIRE(frame_witnesses>100 && weak_nested>100);
    printf("Native mesh: %u cases passed; final-frame witnesses=%u recursive weak-chain controls=%u; direct/entry-stub/return-wrapped modes; draw order, post-state, owned references, early/zero draws, nested callbacks and ABI match\n",cases,frame_witnesses,weak_nested);
    for(unsigned i=0;i<7;i++) REQUIRE(VirtualFree(symbols[i],0,MEM_RELEASE));
    return 0;
}
