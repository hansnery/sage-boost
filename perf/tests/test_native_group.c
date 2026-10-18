/* Isolated native FX group control flow. No GPU, scene, game startup or
 * installed hook. Local verified bodies live in private executable pages.
 * The bulk replacement records logical draws; GPU failure and native
 * exception unwinding are deliberately outside this fixture's claim. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#define REQUIRE(c) do { if(!(c)) { fprintf(stderr,"native group refused line %d: %s (case %u)\n",__LINE__,#c,cases); exit(1); } } while(0)
#define THIS __attribute__((thiscall))
static unsigned cases;
typedef struct Method Method;
typedef struct { unsigned pad[9]; int maximum; } Geometry;
typedef struct { unsigned pad[3]; const char *name; } Name;
typedef struct { unsigned pad[4]; Name *name; unsigned pad2[42]; Geometry *geometry; } Model;
typedef struct { void **vt; unsigned id,key,supported,pad[2]; unsigned transform[12]; unsigned pad2[31]; Model *model; } Mesh;
struct Method { void **vt; int refs,passes,ok; Method *next; unsigned id,deleted; };
typedef struct { Mesh *mesh; int count; Method *methods[6]; } Task;
typedef struct { unsigned (*begin)[12],(*end)[12],(*cap)[12]; } Vector;
typedef struct { unsigned pad[9]; Vector vector; } Renderer;
_Static_assert(offsetof(Model,geometry)==0xBC && offsetof(Mesh,model)==0xC4 &&
               offsetof(Mesh,transform)==0x18 && sizeof(Task)==32 && offsetof(Renderer,vector)==0x24,
               "native layouts");
static Mesh meshes[64];
static Model models[64];
static Geometry geometry;
static Name names[64];
static Task tasks[64];
static Method methods[6];
static Renderer renderer;
static unsigned transforms[64][12];
static Vector *active_vector;
static void *sas=&active_vector;
static void *symbols[34];
static unsigned char *group_original,*group_hooked;
void *group_resume,*group_fallback;
static FARPROC sprintf_import,strncpy_import;
static void *begin_callback,*end_callback;
static const char prefix[]="SceneAnalyst";
static const char *labels[]={"RenderFXShaderBatch","Rendering mesh\tFXShader\t","(unnamed)","MeshFXShader","Rendering %d meshes\tFXShader\t%s"};
static unsigned optimized,mode,current_pass,drop_root,root_dropped,fault_mode;
static unsigned bulk_after=2;
static unsigned draws,bulk_draws,bulk_runs,hook_visits,marker_begin,marker_end,marker_hash;
static unsigned prep_count,finish_count,transform_count,chain_calls,pass_begins,pass_ends,method_begins,method_ends;
static Task *partial_group;
typedef struct { unsigned event,a,b; } Entry;
static Entry trace[8192];
static unsigned trace_n;
unsigned group_abi_errors;

static void record(unsigned event,unsigned a,unsigned b)
{
    REQUIRE(trace_n<sizeof(trace)/sizeof(trace[0]));
    trace[trace_n++]=(Entry){event,a,b};
}
static unsigned *native_frame(void)
{
    unsigned p;
    __asm__ volatile("movl %%fs:0,%0":"=r"(p));
    return (unsigned*)(p+12);
}
static void state(unsigned expected)
{
    unsigned *frame=native_frame();
    REQUIRE(frame[-1]==expected);
    REQUIRE(frame[-2]==(unsigned)symbols[19]);
}
static void THIS deleted(Method *self)
{
    REQUIRE(self->refs==0 && !self->deleted);
    self->deleted++;
    record('D',self->id,0);
}
static void release(Method *self)
{
    if(self) { REQUIRE(self->refs>0); if(!--self->refs) deleted(self); }
}
static void THIS set_next(Method *self,Method *next)
{
    REQUIRE(self && self->refs>0);
    chain_calls++;
    release(self->next);
    self->next=next; /* Native caller already owns this by-value argument. */
    record('C',self->id,next?next->id:99);
}
static int THIS method_begin(Method *self,int *passes,int flags)
{
    state(1); REQUIRE(flags==0xFFFF && self->refs>=2);
    *passes=self->passes; current_pass=0; method_begins++;
    record('B',self->id,self->passes);
    if(drop_root && !root_dropped) { release(self); root_dropped=1; }
    return self->ok;
}
static void THIS begin_pass(Method *self,unsigned pass)
{
    state(1); REQUIRE(pass<(unsigned)self->passes);
    current_pass=pass; pass_begins++; record('P',self->id,pass);
}
static void THIS end_pass(Method *self)
{
    state(1); REQUIRE(!active_vector);
    pass_ends++; record('p',self->id,current_pass);
}
static void THIS method_end(Method *self)
{
    state(1); method_ends++; record('E',self->id,0);
}
static int THIS compare(Mesh *self,Mesh *other) { return self->key==other->key; }
static void THIS prepare(Mesh *self)
{
    state(1); prep_count++; record('A',self->id,0);
}
static void THIS finish(Mesh *self)
{
    state(1); finish_count++; record('Z',self->id,0);
}
static void logical_draw(Mesh *mesh)
{
    REQUIRE(mesh>=meshes && mesh<meshes+64);
    draws++; record('R',mesh->id,current_pass);
}
static void THIS render(Mesh *self,Method *method,int count)
{
    REQUIRE(count>0 && count<=64 && method->refs>=2);
    state(count==1?2:3);
    if(count==1) { REQUIRE(!active_vector); logical_draw(self); }
    else {
        REQUIRE(active_vector && active_vector->end-active_vector->begin==count);
        REQUIRE((*active_vector->begin)[0]==self->id);
        for(int i=0;i<count;i++) {
            unsigned id=active_vector->begin[i][0]; REQUIRE(id<64);
            logical_draw(&meshes[id]);
        }
    }
    release(method); /* Callee consumes the native owning temporary. */
}
static void THIS validate_transform(Mesh *self)
{
    state(3); transform_count++;
    REQUIRE(self->transform[0]==self->id);
}
static void THIS vector_push(Vector *self,const unsigned *value)
{
    state(3); REQUIRE(self==&renderer.vector && self->end<self->cap);
    memcpy(self->end++,value,48);
}
static void THIS set_transforms(void *self,Vector *value)
{
    state(3); REQUIRE(self==sas && !active_vector && value==&renderer.vector);
    active_vector=value;
}
static void THIS clear_transforms(void *self)
{
    state(3); REQUIRE(self==sas && active_vector); active_vector=NULL;
}
static void *THIS vector_erase(Vector *self,void *first,void *last)
{
    state(3); REQUIRE(!active_vector && first==self->begin && last==self->end);
    self->end=self->begin; return first;
}
static void __stdcall on_begin(unsigned color,const unsigned short *text)
{
    REQUIRE(color==0); marker_begin++;
    for(unsigned i=0;text[i];i++) { REQUIRE(i<255); marker_hash=(marker_hash^text[i])*16777619u; }
}
static int on_end(void) { marker_end++; return 0; }
static int __cdecl unexpected_exception(void *a,void *b,void *c,void *d)
{
    (void)a; (void)b; (void)c; (void)d;
    REQUIRE(!"unexpected native exception (unwinding is not tested)"); return 0;
}

/* pushad snapshot: edi, esi, ebp, entry esp, ebx, edx, ecx, eax, flags. */
int group_bulk(unsigned *regs)
{
    unsigned *frame=(unsigned*)regs[2];
    Task *current=(Task*)regs[0],*first=(Task*)regs[4],*end=(Task*)frame[2];
    Method *method=(Method*)regs[1];
    hook_visits++;
    REQUIRE(frame==native_frame() && frame[-1]==1);
    REQUIRE(current>=first && current<end && end<=tasks+64);
    REQUIRE((Task*)frame[-7]==current+1 && regs[6]==1);
    REQUIRE(method==first->methods[0] && method->refs==(drop_root?1:2));
    if(!optimized || ((begin_callback || end_callback) && fault_mode!=3) || frame[-9]!=1 ||
       frame[-11]!=1 || first->count!=1 || (unsigned)(current-first)<bulk_after || mode==2 ||
       (mode==3 && partial_group==first)) return 0;
    unsigned count=0,remaining=(unsigned)(end-current);
    unsigned limit=mode==3?3:remaining;
    while(count<remaining && count<limit && current[count].mesh->supported) count++;
    if(!count) return 0;
    for(unsigned i=fault_mode==1?1:0;i<count;i++) logical_draw(current[i].mesh);
    if(fault_mode==2) method->refs++; /* Deliberately leaked owning temporary. */
    bulk_runs++; bulk_draws+=count;
    if(mode==3) partial_group=first;
    frame[-7]=(unsigned)(current+count);
    if(fault_mode==4 && count<remaining) frame[-7]+=sizeof(Task); /* Lost mesh. */
    return 1;
}
void group_bridge(void);
void native_group_call(void *,void *,Task *,Task *);
__asm__(
".text\n.globl _group_bridge\n_group_bridge:\n"
"pushfl\npushal\npushl %esp\ncall _group_bulk\naddl $4,%esp\ntestl %eax,%eax\nje 1f\n"
"popal\npopfl\njmp *_group_resume\n1:\npopal\npopfl\nleal -0x134(%ebp),%eax\njmp *_group_fallback\n"
".globl _native_group_call\n_native_group_call:\n"
"pushl %ebp\nmovl %esp,%ebp\npushl %ebx\npushl %esi\npushl %edi\nsubl $8,%esp\n"
"movl %esp,-16(%ebp)\nmovl %fs:0,%eax\nmovl %eax,-20(%ebp)\n"
"movl 8(%ebp),%eax\nmovl 12(%ebp),%ecx\npushl 20(%ebp)\npushl 16(%ebp)\n"
"movl $0x13579bdf,%ebx\nmovl $0x2468ace0,%esi\nmovl $0x76543210,%edi\ncall *%eax\n"
"cmpl -16(%ebp),%esp\nje 2f\nincl _group_abi_errors\n2:\n"
"cmpl $0x13579bdf,%ebx\nje 3f\nincl _group_abi_errors\n3:\n"
"cmpl $0x2468ace0,%esi\nje 4f\nincl _group_abi_errors\n4:\n"
"cmpl $0x76543210,%edi\nje 5f\nincl _group_abi_errors\n5:\n"
"movl %fs:0,%eax\ncmpl -20(%ebp),%eax\nje 6f\nincl _group_abi_errors\n6:\n"
"leal -12(%ebp),%esp\npopl %edi\npopl %esi\npopl %ebx\npopl %ebp\nret\n");

static void *method_vt[8]={deleted,NULL,method_begin,begin_pass,NULL,end_pass,method_end,set_next};
static void *mesh_vt[21]={[20]=validate_transform};
typedef struct { unsigned n,maximum,passes,ok,callbacks,split,name,drop,unsupported; } Scenario;
typedef struct {
    Entry trace[8192]; unsigned n,begins,ends,hash,draws,prep,finish,transforms,chain,pb,pe,mb,me;
    int refs[6]; unsigned deleted[6],next[6];
} Result;
static Result baseline;
static void run(Scenario s,unsigned fast,Result *out)
{
    optimized=fast!=0; mode=fast; drop_root=s.drop; root_dropped=0;
    partial_group=NULL; active_vector=NULL;
    draws=bulk_draws=bulk_runs=hook_visits=marker_begin=marker_end=trace_n=0;
    prep_count=finish_count=transform_count=chain_calls=pass_begins=pass_ends=method_begins=method_ends=0;
    marker_hash=2166136261u;
    memset(methods,0,sizeof(methods)); memset(tasks,0,sizeof(tasks));
    for(unsigned i=0;i<6;i++) methods[i]=(Method){method_vt,1,(int)s.passes,(int)s.ok,NULL,i,0};
    geometry.maximum=s.maximum;
    renderer.vector=(Vector){transforms,transforms,transforms+64};
    begin_callback=s.callbacks&1?(void*)on_begin:NULL;
    end_callback=s.callbacks&2?(void*)on_end:NULL;
    for(unsigned i=0;i<s.n;i++) {
        names[i].name=s.name==2?"":"fixture_model";
        models[i].name=s.name==1?NULL:&names[i]; models[i].geometry=&geometry;
        meshes[i].vt=mesh_vt; meshes[i].id=i; meshes[i].key=(s.split==1 && i>=s.n/2);
        meshes[i].model=&models[i]; meshes[i].supported=!s.unsupported || i!=s.n/2;
        meshes[i].transform[0]=i;
        tasks[i].mesh=&meshes[i]; tasks[i].count=s.split==4?0:1;
        tasks[i].methods[0]=&methods[(s.split==2 && i>=s.n/2)?1:0];
        if(s.split==3) { tasks[i].count=3; tasks[i].methods[1]=&methods[1]; tasks[i].methods[2]=&methods[2]; }
        if(s.split==5 && i>=s.n/2) { tasks[i].count=2; tasks[i].methods[1]=&methods[1]; }
    }
    native_group_call(fast?group_hooked:group_original,&renderer,tasks,tasks+s.n);
    REQUIRE(!group_abi_errors && !active_vector && renderer.vector.begin==renderer.vector.end);
    REQUIRE(pass_begins==pass_ends);
    if(s.callbacks&1) REQUIRE(marker_begin>=method_begins);
    if(s.callbacks==3) REQUIRE(marker_begin==marker_end);
    if(!s.callbacks) REQUIRE(!marker_begin && !marker_end);
    REQUIRE(draws==(s.ok && s.split!=4?s.n*s.passes:0));
    if(fast && s.n>bulk_after && s.maximum==1 && s.passes==1 && s.ok && !s.callbacks &&
       !s.split && fast!=2 && !s.unsupported) REQUIRE(bulk_draws>0 && bulk_runs>0);
    if(fast==2 || s.callbacks || s.passes!=1 || s.maximum!=1 || s.split==3) REQUIRE(!bulk_draws);
    Result result={0};
    result.n=trace_n; memcpy(result.trace,trace,trace_n*sizeof(*trace));
    result.begins=marker_begin; result.ends=marker_end; result.hash=marker_hash;
    result.draws=draws; result.prep=prep_count; result.finish=finish_count;
    result.transforms=transform_count; result.chain=chain_calls;
    result.pb=pass_begins; result.pe=pass_ends; result.mb=method_begins; result.me=method_ends;
    for(unsigned i=0;i<6;i++) {
        result.refs[i]=methods[i].refs; result.deleted[i]=methods[i].deleted;
        result.next[i]=methods[i].next?methods[i].next->id+1:0;
    }
    if(out) *out=result;
    else REQUIRE(!memcmp(&baseline,&result,sizeof result));
    /* Begin failure intentionally leaves the stock method chain attached.
     * Record that before fixture cleanup; don't "fix" native behavior. */
    for(unsigned i=0;i<6;i++) if(methods[i].next) { release(methods[i].next); methods[i].next=NULL; }
    for(unsigned i=0;i<6;i++) REQUIRE(methods[i].refs==(s.drop && i==0?0:1));
    cases++;
}
#include "native_group_image.h"
static void load(const char *image,const char *manifest,const char *crt_path)
{
    HMODULE crt=LoadLibraryA(crt_path); REQUIRE(crt);
    void *controlled[]={compare,prepare,render,finish,vector_push,set_transforms,clear_transforms,vector_erase};
    for(unsigned i=0;i<8;i++) symbols[9+i]=controlled[i];
    symbols[17]=(void*)GetProcAddress(crt,"_mbscpy"); symbols[18]=(void*)GetProcAddress(crt,"_mbscat");
    symbols[19]=unexpected_exception; for(unsigned i=0;i<5;i++) symbols[20+i]=(void*)labels[i];
    sprintf_import=GetProcAddress(crt,"sprintf"); strncpy_import=GetProcAddress(crt,"strncpy");
    REQUIRE(symbols[17] && symbols[18] && sprintf_import && strncpy_import);
    symbols[25]=&sprintf_import; symbols[26]=&sas; symbols[27]=&strncpy_import;
    for(unsigned i=0;i<4;i++) symbols[28+i]=(void*)(prefix+4*i);
    symbols[32]=&begin_callback; symbols[33]=&end_callback;
    native_group_load(symbols,image,manifest,group_bridge,(void**)&group_hooked,
                      &group_resume,&group_fallback);
    group_original=symbols[0];
}

int main(int argc,char **argv)
{
    REQUIRE(argc==4 || argc==5);
    if(argc==5) { fault_mode=(unsigned)atoi(argv[4]); REQUIRE(fault_mode>=1 && fault_mode<=4); }
    load(argv[1],argv[2],argv[3]);
    const unsigned counts[]={0,1,2,3,7,25}, maxima[]={1,3,25};
    for(bulk_after=1;bulk_after<=2;bulk_after++) {
    for(unsigned n=0;n<6;n++) for(unsigned m=0;m<3;m++) for(unsigned p=0;p<3;p++)
    for(unsigned ok=0;ok<2;ok++) for(unsigned cb=0;cb<4;cb++) for(unsigned name=0;name<3;name++) {
        Scenario s={counts[n],maxima[m],p,ok,cb,0,name,0,0};
        run(s,0,&baseline); for(unsigned fast=1;fast<=3;fast++) run(s,fast,NULL);
    }
    for(unsigned split=1;split<=5;split++) for(unsigned cb=0;cb<4;cb++) for(unsigned ok=0;ok<2;ok++) {
        Scenario s={25,1,1,ok,cb,split,0,0,0};
        run(s,0,&baseline); for(unsigned fast=1;fast<=3;fast++) run(s,fast,NULL);
    }
    for(unsigned cb=0;cb<4;cb++) for(unsigned drop=0;drop<2;drop++) {
        Scenario s={25,1,1,1,cb,0,0,drop,1};
        run(s,0,&baseline); for(unsigned fast=1;fast<=3;fast++) run(s,fast,NULL);
    }
    }
    printf("Native group: %u cases passed; bulk after one/two members; logical order, passes, method chains/references, callback fallback, partial batches, SEH state, FS chain and ABI\n",cases);
    for(unsigned i=0;i<9;i++) REQUIRE(VirtualFree(symbols[i],0,MEM_RELEASE));
    REQUIRE(VirtualFree(group_hooked,0,MEM_RELEASE)); return 0;
}
