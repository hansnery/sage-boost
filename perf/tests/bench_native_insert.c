/* Offline native task insertion versus a spare-capacity direct append.
 * Native growth/erase run with controlled heap callbacks. No game startup,
 * installation or exception-recovery claim. Compile with general regs only. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "bench_guard.h"
#include "queueinsert.h"
#define REQUIRE(c) do { if(!(c)) { fprintf(stderr,"insert screen refused line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
#define COUNT 33
#define CAPACITY 7680
typedef struct {unsigned word[8];} Record;
typedef struct {Record *first,*last,*limit;} Queue;
typedef struct {void **first,**last,**limit;} Methods;
typedef struct {void **vtable;unsigned refs;} Object;
typedef struct {Object object;unsigned pad[47];unsigned char *model;} Mesh;
_Static_assert(sizeof(Record)==32 && sizeof(Queue)==12 && offsetof(Mesh,model)==0xc4,"native layout");
typedef void (__attribute__((thiscall)) *Add)(Queue *,Mesh *,const Methods *);
static void *bodies[COUNT+3];
static unsigned broken,fast_calls,fallback_calls,allocations,releases;
volatile unsigned insert_abi_errors;
static void __cdecl unexpected(void) { REQUIRE(0 && "native exception dependency reached"); }
static void *__cdecl heap_allocate(unsigned size,void *hint)
{
    REQUIRE(!hint && size && size<=CAPACITY*64);
    unsigned *p=malloc(size+32);REQUIRE(p);p[0]=size;
    for(unsigned i=1;i<4;i++)p[i]=0xabcd0123;
    memset(p+4,0xa5,size);
    for(unsigned i=0;i<4;i++)*(unsigned *)((char *)(p+4)+size+i*4)=0xabcd0123;
    allocations++;return p+4;
}
static void heap_check(void *ptr)
{
    if(!ptr)return;
    unsigned *p=(unsigned *)ptr-4,size=p[0];REQUIRE(size && size<=CAPACITY*64);
    for(unsigned i=1;i<4;i++)REQUIRE(p[i]==0xabcd0123);
    for(unsigned i=0;i<4;i++)REQUIRE(*(unsigned *)((char *)ptr+size+i*4)==0xabcd0123);
}
static void __cdecl heap_release(void *ptr)
{ if(ptr) {heap_check(ptr);releases++;free((unsigned *)ptr-4);} }

static void __attribute__((thiscall)) original_append(void *queues,void *mesh,const void *methods)
{ fallback_calls++;((Add)bodies[0])(queues,mesh,methods); }
static void __attribute__((thiscall)) direct_append(Queue *queues,Mesh *mesh,const Methods *methods)
{
    unsigned before=fallback_calls;
    pf_queue_insert_add(queues,mesh,methods);
    if(before==fallback_calls) {
        fast_calls++;
        if(broken) {
            unsigned which=(mesh->model[0x19]&4)?(**(unsigned char **)(mesh->model+0xbc)?1:2):0;
            queues[which].last[-1].word[7]^=1;
        }
    }
}
static Add candidate = direct_append;

void insert_checked(void *,Queue *,Mesh *,const Methods *);
__asm__(
".text\n.globl _insert_checked\n_insert_checked:\n"
"pushl %ebp\nmovl %esp,%ebp\npushl %ebx\npushl %esi\npushl %edi\nsubl $8,%esp\n"
"movl %esp,-16(%ebp)\nmovl %fs:0,%eax\nmovl %eax,-20(%ebp)\n"
"pushl 20(%ebp)\npushl 16(%ebp)\nmovl 12(%ebp),%ecx\nmovl 8(%ebp),%eax\n"
"movl $0x13579bdf,%ebx\nmovl $0x2468ace0,%esi\nmovl $0x76543210,%edi\ncall *%eax\n"
"cmpl -16(%ebp),%esp\nje 1f\nincl _insert_abi_errors\n1:\n"
"cmpl $0x13579bdf,%ebx\nje 2f\nincl _insert_abi_errors\n2:\n"
"cmpl $0x2468ace0,%esi\nje 3f\nincl _insert_abi_errors\n3:\n"
"cmpl $0x76543210,%edi\nje 4f\nincl _insert_abi_errors\n4:\n"
"movl %fs:0,%eax\ncmpl -20(%ebp),%eax\nje 5f\nincl _insert_abi_errors\n5:\n"
"leal -12(%ebp),%esp\npopl %edi\npopl %esi\npopl %ebx\npopl %ebp\nret\n");

static unsigned fnv(const unsigned char *p,unsigned n)
{ unsigned h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h; }
typedef struct {unsigned body,field,kind,symbol;} Patch;
static void load(const char *path,const char *manifest)
{
    FILE *f=fopen(path,"rb");REQUIRE(f);REQUIRE(!fseek(f,0,SEEK_END));long length=ftell(f);rewind(f);
    REQUIRE(length>4096 && length<128*1024*1024);unsigned char *raw=malloc(length);REQUIRE(raw);
    REQUIRE(fread(raw,1,length,f)==(size_t)length);fclose(f);
    REQUIRE(((IMAGE_DOS_HEADER*)raw)->e_magic==IMAGE_DOS_SIGNATURE);
    unsigned offset=((IMAGE_DOS_HEADER*)raw)->e_lfanew;REQUIRE(offset<=(unsigned)length-sizeof(IMAGE_NT_HEADERS32));
    IMAGE_NT_HEADERS32 *nt=(void*)(raw+offset);
    REQUIRE(nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.Machine==IMAGE_FILE_MACHINE_I386 && nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC);
    unsigned image_size=nt->OptionalHeader.SizeOfImage;
    REQUIRE(image_size>=4096 && image_size<128*1024*1024);
    unsigned char *code[1];
    /* Preserve the target's relative function placement, instead of
     * distorting native costs with one separate page per body. Only
     * the verified bodies are populated; no image is loaded or initialized. */
    for(unsigned mode=0;mode<1;mode++) {
        code[mode]=VirtualAlloc(NULL,image_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);REQUIRE(code[mode]);
    }
    IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);REQUIRE((unsigned char*)(sections+nt->FileHeader.NumberOfSections)<=raw+length);
    f=fopen(manifest,"r");REQUIRE(f);unsigned count,np=0;Patch patches[512];
    REQUIRE(fscanf(f,"%u",&count)==1 && count==COUNT);
    const unsigned sizes[]={249,31,35,47,62,100,16,4,98,51,264,71,59,17,94,29,25,85,38,29,29,27,180,96,47,20,102,45,47,56,37,28,30};
    for(unsigned i=0;i<COUNT;i++) {
        unsigned rva,size,hash,n,source=0,matches=0;
        REQUIRE(fscanf(f,"%u %u %u %u",&rva,&size,&hash,&n)==4 && size==sizes[i] && n<64);
        REQUIRE(rva<image_size && size<=image_size-rva);
        for(unsigned j=0;j<nt->FileHeader.NumberOfSections;j++) {
            IMAGE_SECTION_HEADER *s=sections+j;
            if(rva>=s->VirtualAddress && rva-s->VirtualAddress<=s->SizeOfRawData && size<=s->SizeOfRawData-(rva-s->VirtualAddress) && (s->Characteristics&IMAGE_SCN_MEM_EXECUTE)) {
                REQUIRE(s->PointerToRawData<=(unsigned)length && s->SizeOfRawData<=(unsigned)length-s->PointerToRawData);
                source=s->PointerToRawData+rva-s->VirtualAddress;matches++;
            }
        }
        REQUIRE(matches==1 && fnv(raw+source,size)==hash);
        for(unsigned mode=0;mode<1;mode++) {
            bodies[i]=code[mode]+rva;
            memcpy(bodies[i],raw+source,size);
        }
        for(unsigned j=0;j<n;j++) {
            REQUIRE(np<512);Patch *p=patches+np++;p->body=i;
            REQUIRE(fscanf(f,"%u %u %u",&p->field,&p->kind,&p->symbol)==3 && p->field<=size-4 && p->kind<=1 && p->symbol<=COUNT+2);
        }
    }
    {char extra;REQUIRE(fscanf(f," %c",&extra)==EOF);}fclose(f);free(raw);
    for(unsigned mode=0;mode<1;mode++) {
        bodies[COUNT]=unexpected;
        bodies[COUNT+1]=heap_allocate;bodies[COUNT+2]=heap_release;
        for(unsigned j=0;j<np;j++) {
            Patch p=patches[j];unsigned char *field=(unsigned char*)bodies[p.body]+p.field;
            void *target=bodies[p.symbol];
            *(unsigned*)field=(unsigned)target-(p.kind?0:(unsigned)(field+4));
        }
        DWORD old;REQUIRE(VirtualProtect(code[mode],image_size,PAGE_EXECUTE_READ,&old));
        REQUIRE(FlushInstructionCache(GetCurrentProcess(),code[mode],image_size));
    }
    pf_queue_insert_original(original_append);
}


#ifdef PF_NATIVE_INSERT_EMBED
void bench_insert_load(const char *image,const char *manifest) {load(image,manifest);}
void bench_insert_add(void *queues,void *mesh,const void *methods,unsigned optimized)
{ ((Add)(optimized?(void *)candidate:bodies[0]))(queues,mesh,methods); }
void bench_insert_corrupt(unsigned enabled) {broken=enabled;}
void bench_insert_clear(void *queue)
{
    Queue *q=queue;
    ((Record *(__attribute__((thiscall)) *)(Queue *,Record *,Record *))bodies[9])(q,q->first,q->last);
    REQUIRE(q->first==q->last);
}
#else
static Mesh meshes[CAPACITY];
static Object method_objects[8];
static unsigned char models[3][0xc0],geometry[3];
static void *method_words[7];
static unsigned deleted,cases;
static void __attribute__((thiscall)) object_delete(Object *object)
{ REQUIRE(!object->refs);deleted++; }
static void *vtable[]={object_delete};
static Record expected[3][CAPACITY];
static unsigned expected_size[3],expected_capacity[3],expected_refs[8],expected_deletes,expected_allocations,expected_mesh_hash;

static void init_objects(unsigned count,unsigned pattern)
{
    deleted=0;allocations=0;releases=0;fast_calls=0;fallback_calls=0;
    memset(meshes,0,sizeof meshes);memset(models,0,sizeof models);
    for(unsigned q=0;q<3;q++) {
        models[q][0x19]=q?4:0;geometry[q]=q==1;
        *(void **)(models[q]+0xbc)=geometry+q;
    }
    for(unsigned i=0;i<CAPACITY;i++) {
        meshes[i].object.vtable=vtable;meshes[i].object.refs=1;meshes[i].model=models[i%3];
    }
    for(unsigned i=0;i<8;i++) {method_objects[i].vtable=vtable;method_objects[i].refs=3;}
    for(unsigned i=0;i<7;i++)method_words[i]=pattern==1?method_objects:pattern==2 && (i%2)?NULL:method_objects+i;
    (void)count;
}
static void init_queues(Queue *queues,unsigned capacity)
{
    memset(queues,0,3*sizeof *queues);
    for(unsigned i=0;i<3;i++)if(capacity) {
        queues[i].first=heap_allocate(capacity*32,NULL);
        queues[i].last=queues[i].first;queues[i].limit=queues[i].first+capacity;
    }
}
static void destroy_queues(Queue *queues)
{
    for(unsigned q=0;q<3;q++) {
        ((void (__attribute__((thiscall)) *)(Queue *))bodies[32])(queues+q);
        memset(queues+q,0,sizeof *queues);
    }
}
static void oracle(int condition)
{ if(!condition) {fprintf(stderr,"insert oracle mismatch\n");exit(1);} }
static void check(unsigned n,unsigned capacity,unsigned count,unsigned pattern)
{
    for(unsigned mode=0;mode<2;mode++) {
        Queue queues[3];init_objects(count,pattern);init_queues(queues,capacity);
        Methods methods={method_words,method_words+count,method_words+7};
        unsigned source_hash=fnv((const unsigned char *)models,sizeof models)^fnv((const unsigned char *)method_words,sizeof method_words);
        unsigned hash=0;
        for(unsigned i=0;i<n;i++) {
            meshes[i].object.refs++; /* Caller creates its by-value holder. */
            insert_checked(mode?(void *)candidate:bodies[0],queues,meshes+i,&methods);
            REQUIRE(!insert_abi_errors);
            for(unsigned q=0;q<3;q++)heap_check(queues[q].first);
            hash+=meshes[i].object.refs;
        }
        REQUIRE(hash==n*(count<=6?2:1));
        REQUIRE(source_hash==(fnv((const unsigned char *)models,sizeof models)^fnv((const unsigned char *)method_words,sizeof method_words)));
        for(unsigned q=0;q<3;q++) {
            unsigned size=((unsigned)queues[q].last-(unsigned)queues[q].first)/32;
            unsigned cap=((unsigned)queues[q].limit-(unsigned)queues[q].first)/32;
            if(!mode) {expected_size[q]=size;expected_capacity[q]=cap;memcpy(expected[q],queues[q].first,size*32);}
            else {oracle(expected_size[q]==size && expected_capacity[q]==cap);oracle(!memcmp(expected[q],queues[q].first,size*32));}
            for(unsigned i=size*32;i<cap*32;i++)REQUIRE(((unsigned char *)queues[q].first)[i]==0xa5);
        }
        if(!mode) {
            for(unsigned i=0;i<8;i++)expected_refs[i]=method_objects[i].refs;
            expected_deletes=deleted;expected_allocations=allocations;
            expected_mesh_hash=fnv((const unsigned char *)meshes,sizeof meshes);
        } else {
            for(unsigned i=0;i<8;i++)oracle(expected_refs[i]==method_objects[i].refs);
            oracle(expected_deletes==deleted && expected_allocations==allocations);
            oracle(expected_mesh_hash==fnv((const unsigned char *)meshes,sizeof meshes));
            if(candidate==direct_append) {
                REQUIRE(fast_calls+fallback_calls==n);
                if(capacity>=n && count<=6)REQUIRE(fast_calls==n);
                if(count>6)REQUIRE(fallback_calls==n);
            }
        }
        destroy_queues(queues);REQUIRE(allocations==releases);
        for(unsigned i=0;i<n;i++)REQUIRE(meshes[i].object.refs==1);
        for(unsigned i=0;i<8;i++)REQUIRE(method_objects[i].refs==3);
    }
    cases++;
}
static void edge_controls(void)
{
    for(unsigned mode=0;mode<2;mode++) {
        Queue queues[3];init_objects(7,0);init_queues(queues,2);
        Methods too_many={method_words,method_words+7,method_words+7};
        insert_checked(mode?(void *)candidate:bodies[0],queues,meshes,&too_many);
        REQUIRE(deleted==1 && !meshes[0].object.refs);
        insert_checked(mode?(void *)candidate:bodies[0],queues,NULL,&too_many);
        REQUIRE(deleted==1);
        /* Source methods alias unused destination: native zero construction
         * makes the method NULL, so the candidate must delegate this case. */
        Methods alias={(void **)&queues[0].last->word[2],(void **)&queues[0].last->word[3],(void **)&queues[0].last->word[3]};
        queues[0].last->word[2]=(unsigned)method_objects;
        meshes[0].object.refs=2;
        insert_checked(mode?(void *)candidate:bodies[0],queues,meshes,&alias);
        REQUIRE(queues[0].last==queues[0].first+1 && !queues[0].first->word[2]);
        REQUIRE(meshes[0].object.refs==2 && method_objects[0].refs==3);
        if(mode && candidate==direct_append)REQUIRE(fallback_calls==3 && !fast_calls);
        destroy_queues(queues);REQUIRE(allocations==releases && !insert_abi_errors);
    }
}
static unsigned long long timed(unsigned mode,unsigned count,unsigned grow)
{
    Queue queues[3];init_objects(count,0);init_queues(queues,grow?0:CAPACITY/3);
    Methods methods={method_words,method_words+count,method_words+7};
    Add add=mode?candidate:(Add)bodies[0];
    LARGE_INTEGER before,after;REQUIRE(QueryPerformanceCounter(&before));
    for(unsigned rep=0;rep<8;rep++) {
        for(unsigned i=0;i<CAPACITY;i++) {meshes[i].object.refs++;add(queues,meshes+i,&methods);}
        for(unsigned q=0;q<3;q++) {
            ((Record *(__attribute__((thiscall)) *)(Queue *,Record *,Record *))bodies[9])(queues+q,queues[q].first,queues[q].last);
            REQUIRE(queues[q].first==queues[q].last);
        }
    }
    REQUIRE(QueryPerformanceCounter(&after));
    destroy_queues(queues);REQUIRE(allocations==releases && !deleted);
    for(unsigned i=0;i<CAPACITY;i++)REQUIRE(meshes[i].object.refs==1);
    for(unsigned i=0;i<8;i++)REQUIRE(method_objects[i].refs==3);
    return (unsigned long long)(after.QuadPart-before.QuadPart);
}
int main(int argc,char **argv)
{
    REQUIRE(argc==5);unsigned samples=(unsigned)atoi(argv[3]);broken=(unsigned)atoi(argv[4]);
    REQUIRE(samples<=101 && broken<=1 && bench_no_game());load(argv[1],argv[2]);
    const unsigned sizes[]={0,1,2,3,4,17,128,7680};
    for(unsigned n=0;n<sizeof sizes/sizeof sizes[0];n++)for(unsigned capacity=0;capacity<3;capacity++)
        for(unsigned count=0;count<=7;count++)for(unsigned pattern=0;pattern<3;pattern++)
            check(sizes[n],capacity==2?CAPACITY:capacity,count,pattern);
    edge_controls();printf("INSERT_CONTROLS,%u,ABI,%u\n",cases,insert_abi_errors);
    LARGE_INTEGER frequency;REQUIRE(QueryPerformanceFrequency(&frequency));printf("FREQUENCY,%llu\n",(unsigned long long)frequency.QuadPart);
    for(unsigned count=0;count<=6 && samples;count+=(count?5:1))for(unsigned grow=0;grow<2;grow++) {
        REQUIRE(bench_no_game());
        for(unsigned warm=0;warm<4;warm++) {timed(0,count,grow);timed(1,count,grow);}
        for(unsigned sample=0;sample<samples;sample++) {
            unsigned long long a,b;
            if(sample&1) {b=timed(1,count,grow);a=timed(0,count,grow);}else {a=timed(0,count,grow);b=timed(1,count,grow);}
            printf("INSERT_TIMING,%u,%u,%u,%llu,%llu\n",count,grow,sample,a,b);
        }
    }
    REQUIRE(bench_no_game());return 0;
}
#endif
