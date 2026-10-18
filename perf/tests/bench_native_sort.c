/* Preserve the native sort and its exact permutation; screen replacing
 * temporary record ownership traffic with raw copies inside that sort.
 * Offline only. Exception handlers are fail-stop, not an unwind proof. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "bench_guard.h"
#define REQUIRE(c) do { if(!(c)) { fprintf(stderr,"sort screen refused line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
#define COUNT 38
#define CAPACITY 8192
typedef struct {unsigned word[8];} Record;
typedef struct {void **vtable;unsigned refs;unsigned pad[47],key,pad2[146],state;} Mesh;
typedef struct {void **vtable;unsigned refs;} Method;
_Static_assert(sizeof(Record)==32 && offsetof(Mesh,key)==0xc4 && offsetof(Mesh,state)==0x310,"native layout");
typedef unsigned (__cdecl *Compare)(const Record *,const Record *);
typedef void (__cdecl *Sort)(Record *,Record *,Compare);
static void *bodies[2][COUNT+1];
static unsigned broken;
#ifndef PF_NATIVE_SORT_EMBED
static unsigned cases;
#endif
volatile unsigned sort_abi_errors;

static void __cdecl unexpected(void) { REQUIRE(0 && "native exception/termination dependency reached"); }
#ifndef PF_NATIVE_SORT_EMBED
static void __attribute__((thiscall)) delete_unexpected(void *self) { (void)self;REQUIRE(0 && "unexpected object destruction"); }
#endif
static void * __attribute__((thiscall)) raw_copy(void *to,const void *from)
{
    unsigned *out=to;const unsigned *in=from;
    for(unsigned i=0;i<8;i++)out[i]=in[i];
    if(broken)out[7]^=1;
    return to;
}
static void __attribute__((thiscall)) raw_destroy(void *self) { (void)self; }

void sort_checked(void *,Record *,Record *,Compare,unsigned);
__asm__(
".text\n.globl _sort_checked\n_sort_checked:\n"
"pushl %ebp\nmovl %esp,%ebp\npushl %ebx\npushl %esi\npushl %edi\nsubl $8,%esp\n"
"movl %esp,-16(%ebp)\nmovl %fs:0,%eax\nmovl %eax,-20(%ebp)\n"
"pushl 20(%ebp)\ncmpl $0,24(%ebp)\nje 1f\npushl $0\npushl $0\n1:\n"
"pushl 16(%ebp)\npushl 12(%ebp)\nmovl 8(%ebp),%eax\n"
"movl $0x13579bdf,%ebx\nmovl $0x2468ace0,%esi\nmovl $0x76543210,%edi\ncall *%eax\n"
"addl $12,%esp\ncmpl $0,24(%ebp)\nje 2f\naddl $8,%esp\n2:\n"
"cmpl -16(%ebp),%esp\nje 3f\nincl _sort_abi_errors\n3:\n"
"cmpl $0x13579bdf,%ebx\nje 4f\nincl _sort_abi_errors\n4:\n"
"cmpl $0x2468ace0,%esi\nje 5f\nincl _sort_abi_errors\n5:\n"
"cmpl $0x76543210,%edi\nje 6f\nincl _sort_abi_errors\n6:\n"
"movl %fs:0,%eax\ncmpl -20(%ebp),%eax\nje 7f\nincl _sort_abi_errors\n7:\n"
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
    unsigned char *code[2];
    /* Preserve the target's relative function placement, instead of
     * exaggerating reference costs with one separate page per body. Only
     * the verified bodies are populated; no image is loaded or initialized. */
    for(unsigned mode=0;mode<2;mode++) {
        code[mode]=VirtualAlloc(NULL,image_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);REQUIRE(code[mode]);
    }
    IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);REQUIRE((unsigned char*)(sections+nt->FileHeader.NumberOfSections)<=raw+length);
    f=fopen(manifest,"r");REQUIRE(f);unsigned count,np=0;Patch patches[512];
    REQUIRE(fscanf(f,"%u",&count)==1 && count==COUNT);
    const unsigned sizes[]={47,16,20,71,56,47,86,135,29,47,85,29,46,75,172,23,92,27,83,45,101,118,25,23,57,58,74,100,27,144,67,89,59,17,94,96,31,102};
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
        for(unsigned mode=0;mode<2;mode++) {
            bodies[mode][i]=code[mode]+rva;
            memcpy(bodies[mode][i],raw+source,size);
        }
        for(unsigned j=0;j<n;j++) {
            REQUIRE(np<512);Patch *p=patches+np++;p->body=i;
            REQUIRE(fscanf(f,"%u %u %u",&p->field,&p->kind,&p->symbol)==3 && p->field<=size-4 && p->kind<=1 && p->symbol<=COUNT);
        }
    }
    {char extra;REQUIRE(fscanf(f," %c",&extra)==EOF);}fclose(f);free(raw);
    for(unsigned mode=0;mode<2;mode++) {
        bodies[mode][COUNT]=unexpected;
        for(unsigned j=0;j<np;j++) {
            Patch p=patches[j];unsigned char *field=(unsigned char*)bodies[mode][p.body]+p.field;
            void *target=bodies[mode][p.symbol];
            if(mode && (p.symbol==10 || p.symbol==4))target=raw_copy;
            if(mode && p.symbol==3)target=raw_destroy;
            *(unsigned*)field=(unsigned)target-(p.kind?0:(unsigned)(field+4));
        }
        DWORD old;REQUIRE(VirtualProtect(code[mode],image_size,PAGE_EXECUTE_READ,&old));
        REQUIRE(FlushInstructionCache(GetCurrentProcess(),code[mode],image_size));
    }
}

#ifdef PF_NATIVE_SORT_EMBED
/* The joined GPU fixture uses the same verified functions and general-
 * register candidate, with native ownership at queue entry and exit. */
void bench_sort_load(const char *image,const char *manifest)
{ load(image,manifest); }
void bench_sort_run(void *first,void *last,unsigned optimized)
{
    REQUIRE(optimized<2 && (unsigned)last>=(unsigned)first && !(((unsigned)last-(unsigned)first)%32));
    sort_checked(bodies[optimized][30],first,last,(Compare)bodies[optimized][8],0);
    REQUIRE(!sort_abi_errors);
}
void bench_sort_copy(void *to,const void *from)
{ ((void *(__attribute__((thiscall)) *)(void *,const void *))bodies[0][10])(to,from); }
void bench_sort_destroy(void *record)
{ ((void (__attribute__((thiscall)) *)(void *))bodies[0][3])(record); }
void bench_sort_corrupt(unsigned enabled) { broken=enabled; }
#else
static Mesh meshes[CAPACITY];
static Method methods[32];
static unsigned method_refs[32];
static void *vtable[]={delete_unexpected};
typedef struct {unsigned before[8];Record records[CAPACITY];unsigned after[8];} Guarded;
static Guarded seed,original,candidate;
static unsigned random_word(unsigned *s) { *s=*s*1664525u+1013904223u;return *s; }
static void setup(unsigned n,unsigned pattern,unsigned method_count)
{
    unsigned rng=0xdeadbeef;
    memset(&seed,0xa5,sizeof seed);memset(meshes,0,sizeof meshes);memset(methods,0,sizeof methods);memset(method_refs,0,sizeof method_refs);
    for(unsigned i=0;i<32;i++)methods[i].vtable=vtable;
    for(unsigned i=0;i<n;i++) {
        Mesh *m=meshes+i;m->vtable=vtable;m->refs=1;
        switch(pattern) {
        case 0:m->key=random_word(&rng)%128;break;
        case 1:m->key=i/6;break;
        case 2:m->key=(n-1-i)/6;break;
        case 3:m->key=0;break;
        case 4:m->key=(i<n/2?i:n-1-i)/2;break;
        default:m->key=random_word(&rng);break;
        }
        m->state=(i%7)*0x25357789u;
        Record *record=seed.records+i;record->word[0]=(unsigned)m;record->word[1]=method_count;
        for(unsigned j=0;j<6;j++) {
            unsigned index=(i+j)%32;
            record->word[2+j]=j<method_count?(unsigned)(methods+index):0;
            if(j<method_count)method_refs[index]++;
        }
    }
    for(unsigned i=0;i<32;i++)methods[i].refs=method_refs[i];
}
static void references(unsigned n)
{
    for(unsigned i=0;i<n;i++)REQUIRE(meshes[i].refs==1);
    for(unsigned i=0;i<32;i++)REQUIRE(methods[i].refs==method_refs[i]);
}
static void check(unsigned n,unsigned pattern,unsigned count,unsigned comparator,unsigned heap)
{
    setup(n,pattern,count);memcpy(&original,&seed,sizeof seed);memcpy(&candidate,&seed,sizeof seed);
    unsigned hash=fnv((unsigned char*)meshes,sizeof meshes);
    sort_checked(bodies[0][heap?29:30],original.records,original.records+n,(Compare)bodies[0][8+comparator],heap);
    /* The introsort loop leaves ranges of <=16 for the final insertion
     * pass, even at depth zero. Match the native root's second phase. */
    if(heap)sort_checked(bodies[0][26],original.records,original.records+n,(Compare)bodies[0][8+comparator],0);
    REQUIRE(!sort_abi_errors);references(n);REQUIRE(hash==fnv((unsigned char*)meshes,sizeof meshes));
    sort_checked(bodies[1][heap?29:30],candidate.records,candidate.records+n,(Compare)bodies[1][8+comparator],heap);
    if(heap)sort_checked(bodies[1][26],candidate.records,candidate.records+n,(Compare)bodies[1][8+comparator],0);
    REQUIRE(!sort_abi_errors);REQUIRE(!memcmp(&original,&candidate,sizeof original));references(n);
    REQUIRE(hash==fnv((unsigned char*)meshes,sizeof meshes));
    REQUIRE(!memcmp(original.before,seed.before,sizeof seed.before) && !memcmp(original.after,seed.after,sizeof seed.after));
    REQUIRE(!memcmp(original.records+n,seed.records+n,(CAPACITY-n)*sizeof(Record)));
    for(unsigned i=1;i<n;i++)if(((Compare)bodies[0][8+comparator])(original.records+i,original.records+i-1)) {
        fprintf(stderr,"case n=%u pattern=%u methods=%u comparator=%u heap=%u at=%u\n",n,pattern,count,comparator,heap,i);
        REQUIRE(0 && "native output is not sorted");
    }
    cases++;
}
static void check_split(unsigned pattern,unsigned count,unsigned comparator)
{
    const unsigned bounds[]={0,3072,4608,7680};
    setup(7680,pattern,count);memcpy(&original,&seed,sizeof seed);memcpy(&candidate,&seed,sizeof seed);
    unsigned hash=fnv((unsigned char*)meshes,sizeof meshes);
    for(unsigned queue=0;queue<3;queue++) {
        unsigned first=bounds[queue],last=bounds[queue+1];
        sort_checked(bodies[0][30],original.records+first,original.records+last,(Compare)bodies[0][8+comparator],0);
        REQUIRE(!sort_abi_errors);references(7680);
        sort_checked(bodies[1][30],candidate.records+first,candidate.records+last,(Compare)bodies[1][8+comparator],0);
        REQUIRE(!sort_abi_errors);references(7680);
        REQUIRE(!memcmp(&original,&candidate,sizeof original));
        REQUIRE(hash==fnv((unsigned char*)meshes,sizeof meshes));
        REQUIRE(!memcmp(original.before,seed.before,sizeof seed.before) && !memcmp(original.after,seed.after,sizeof seed.after));
        REQUIRE(!memcmp(original.records+last,seed.records+last,(CAPACITY-last)*sizeof(Record)));
        for(unsigned i=first+1;i<last;i++)REQUIRE(!((Compare)bodies[0][8+comparator])(original.records+i,original.records+i-1));
    }
    cases++;
}
static void controls(void)
{
    const unsigned sizes[]={0,1,2,3,15,16,17,31,32,33,128,1024,3072};
    const unsigned methods_count[]={0,1,6};
    for(unsigned s=0;s<sizeof sizes/sizeof sizes[0];s++)for(unsigned p=0;p<6;p++)
        for(unsigned m=0;m<3;m++)for(unsigned cmp=0;cmp<2;cmp++)for(unsigned heap=0;heap<2;heap++) {
            if(heap && !sizes[s])continue;
            check(sizes[s],p,methods_count[m],cmp,heap);
        }
    for(unsigned p=0;p<6;p++)for(unsigned m=0;m<3;m++)for(unsigned cmp=0;cmp<2;cmp++)
        check_split(p,methods_count[m],cmp);
    printf("SORT_CONTROLS,%u,ABI,%u\n",cases,sort_abi_errors);
}
static unsigned long long timed(unsigned mode,unsigned comparator)
{
    LARGE_INTEGER before,after;REQUIRE(QueryPerformanceCounter(&before));
    for(unsigned rep=0;rep<8;rep++) {
        memcpy(&candidate,&seed,sizeof seed);
        Sort sort=(Sort)bodies[mode][30];Compare compare=(Compare)bodies[mode][8+comparator];
        sort(candidate.records,candidate.records+3072,compare);
        sort(candidate.records+3072,candidate.records+4608,compare);
        sort(candidate.records+4608,candidate.records+7680,compare);
    }
    REQUIRE(QueryPerformanceCounter(&after));references(7680);
    return (unsigned long long)(after.QuadPart-before.QuadPart);
}
int main(int argc,char **argv)
{
    REQUIRE(argc==5);unsigned samples=(unsigned)atoi(argv[3]);broken=(unsigned)atoi(argv[4]);
    REQUIRE(samples<=101 && broken<=1 && bench_no_game());load(argv[1],argv[2]);controls();
    LARGE_INTEGER frequency;REQUIRE(QueryPerformanceFrequency(&frequency));printf("FREQUENCY,%llu\n",(unsigned long long)frequency.QuadPart);
    for(unsigned p=0;p<6 && samples;p++)for(unsigned count=0;count<=6;count+=(count?5:1)) {
        REQUIRE(bench_no_game());setup(7680,p,count);
        for(unsigned warm=0;warm<4;warm++) {timed(0,0);timed(1,0);}
        for(unsigned sample=0;sample<samples;sample++) {
            unsigned long long a,b;
            if(sample&1) {b=timed(1,0);a=timed(0,0);}else {a=timed(0,0);b=timed(1,0);}
            printf("SORT_TIMING,%u,%u,%u,%llu,%llu\n",p,count,sample,a,b);
        }
    }
    REQUIRE(bench_no_game());return 0;
}
#endif
