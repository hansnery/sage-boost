/* Native, warm-cache animation decoder cost screen. Synthetic inputs only.
 * No game entry point, assets, production hooks or FP control changes. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "bench_guard.h"
#define REQUIRE(c) do { if(!(c)) { fprintf(stderr,"decoder screen refused line %d: %s\n",__LINE__,#c); exit(1); } } while(0)

typedef struct {
    unsigned vtable,type,pivot,count,components,scale,initial[4];
    unsigned char *data;
} Channel;
_Static_assert(sizeof(Channel)==44 && offsetof(Channel,data)==40,"native channel layout");
typedef void (__attribute__((thiscall)) *Decoder)(Channel *,unsigned *,unsigned,unsigned,unsigned *,unsigned *);
static Decoder native[3];
static unsigned filter[256],broken,cases;
static unsigned long long hits,misses;
volatile unsigned decoder_abi_errors;
void decoder_checked(void *,Channel *,unsigned *,unsigned,unsigned,unsigned *,unsigned *);
__asm__(
".text\n.globl _decoder_checked\n_decoder_checked:\n"
"pushl %ebp\nmovl %esp,%ebp\npushl %ebx\npushl %esi\npushl %edi\nsubl $4,%esp\n"
"movl %esp,-16(%ebp)\nmovl 8(%ebp),%eax\nmovl 12(%ebp),%ecx\n"
"pushl 32(%ebp)\npushl 28(%ebp)\npushl 24(%ebp)\npushl 20(%ebp)\npushl 16(%ebp)\n"
"movl $0x13579bdf,%ebx\nmovl $0x2468ace0,%esi\nmovl $0x76543210,%edi\ncall *%eax\n"
"cmpl -16(%ebp),%esp\nje 1f\nincl _decoder_abi_errors\n1:\n"
"cmpl $0x13579bdf,%ebx\nje 2f\nincl _decoder_abi_errors\n2:\n"
"cmpl $0x2468ace0,%esi\nje 3f\nincl _decoder_abi_errors\n3:\n"
"cmpl $0x76543210,%edi\nje 4f\nincl _decoder_abi_errors\n4:\n"
"leal -12(%ebp),%esp\npopl %edi\npopl %esi\npopl %ebx\npopl %ebp\nret\n");

static unsigned fnv(const unsigned char *p,unsigned n)
{ unsigned h=2166136261u; while(n--) h=(h^*p++)*16777619u; return h; }

static void load_native(const char *path,const char *manifest)
{
    FILE *f=fopen(path,"rb"); REQUIRE(f); REQUIRE(!fseek(f,0,SEEK_END));
    long length=ftell(f); rewind(f); REQUIRE(length>4096 && length<128*1024*1024);
    unsigned char *raw=malloc(length); REQUIRE(raw);
    REQUIRE(fread(raw,1,length,f)==(size_t)length); fclose(f);
    REQUIRE(((IMAGE_DOS_HEADER*)raw)->e_magic==IMAGE_DOS_SIGNATURE);
    unsigned offset=((IMAGE_DOS_HEADER*)raw)->e_lfanew;
    REQUIRE(offset<=(unsigned)length-sizeof(IMAGE_NT_HEADERS32));
    IMAGE_NT_HEADERS32 *nt=(void*)(raw+offset);
    REQUIRE(nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.Machine==IMAGE_FILE_MACHINE_I386 && nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC);
    IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);
    REQUIRE((unsigned char*)(sections+nt->FileHeader.NumberOfSections)<=raw+length);
    f=fopen(manifest,"r"); REQUIRE(f);
    const unsigned sizes[]={250,326,329},dimensions[]={1,3,4};
    for(unsigned i=0;i<3;i++) {
        unsigned rva,size,hash,field,n,matches=0,source=0;
        REQUIRE(fscanf(f,"%u %u %u %u %u",&rva,&size,&hash,&field,&n)==5);
        REQUIRE(size==sizes[i] && n==dimensions[i] && field<=size-4);
        for(unsigned j=0;j<nt->FileHeader.NumberOfSections;j++) {
            IMAGE_SECTION_HEADER *s=sections+j;
            if(rva>=s->VirtualAddress && rva-s->VirtualAddress<=s->SizeOfRawData && size<=s->SizeOfRawData-(rva-s->VirtualAddress) && (s->Characteristics&IMAGE_SCN_MEM_EXECUTE)) {
                REQUIRE(s->PointerToRawData<=(unsigned)length && s->SizeOfRawData<=(unsigned)length-s->PointerToRawData);
                source=s->PointerToRawData+rva-s->VirtualAddress;matches++;
            }
        }
        REQUIRE(matches==1 && fnv(raw+source,size)==hash);
        unsigned char *code=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); REQUIRE(code);
        memcpy(code,raw+source,size); *(void**)(code+field)=filter;
        DWORD old; REQUIRE(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));
        REQUIRE(FlushInstructionCache(GetCurrentProcess(),code,4096));native[i]=(Decoder)code;
    }
    {char c;REQUIRE(fscanf(f," %c",&c)==EOF);} fclose(f);free(raw);
    /* Exact powers of two generated here, not copied game table data.
     * All 256 entries are valid; this screen tests decoder reuse, not the
     * game's runtime sine-table initializer or asset-loader behavior. */
    for(unsigned i=0;i<256;i++) filter[i]=(120u+i%12u)<<23;
}

typedef struct { Channel *channel; unsigned from,frame,state[4],result[8]; } Entry;
static Entry cache[1024];
static void reset(void) { memset(cache,0,sizeof cache);hits=misses=0; }

/* The fixture owns immutable channels/streams for a whole epoch. A future
 * render hook would need a separately proved lifetime/invalidation boundary.
 * Include input state: equal channel/frame alone is not an exact key. */
static void reuse(unsigned kind,Channel *channel,unsigned *state,unsigned from,unsigned frame,unsigned *out0,unsigned *out1)
{
    unsigned n=channel->components;
    unsigned key_frame=broken==2?0:frame;
    unsigned h=((unsigned)channel>>4)^(from*0x9e3779b9u)^(key_frame*0x85ebca6bu);
    unsigned input[4]={0};
    for(unsigned i=0;i<n;i++) {input[i]=broken==1?0:state[i];h=(h^input[i])*16777619u;}
    Entry *entry=cache+(h&1023);
    unsigned equal=1;
    for(unsigned i=0;i<n;i++)if(entry->state[i]!=input[i])equal=0;
    if(entry->channel==channel && entry->from==from && entry->frame==key_frame && equal) {
        hits++;
        for(unsigned i=0;i<n;i++) {out0[i]=entry->result[i];out1[i]=entry->result[n+i];}
        if(broken==3)out0[0]^=1;
        return;
    }
    misses++;
    /* Snapshot the key before calling: the game's warm path aliases state
     * with output zero. Native decode writes only the two output vectors. */
    native[kind](channel,state,from,frame,out0,out1);
    entry->channel=channel;entry->from=from;entry->frame=key_frame;
    for(unsigned i=0;i<n;i++) {entry->state[i]=input[i];entry->result[i]=out0[i];entry->result[n+i]=out1[i];}
}

static unsigned char streams[48][36*8];
static Channel channels[48];
static void setup(void)
{
    for(unsigned j=0;j<48;j++) {
        Channel *c=channels+j;
        c->count=128;c->components=(j%3==0?1:j%3==1?3:4);c->scale=0x3c800000;
        for(unsigned k=0;k<4;k++)c->initial[k]=0x3f000000+j*8192+k*65536;
        c->data=streams[j];
        for(unsigned b=0;b<8;b++) for(unsigned k=0;k<c->components;k++) {
            unsigned char *p=c->data+(b*c->components+k)*9;
            p[0]=(unsigned char)(b*17+k+j);
            for(unsigned v=1;v<9;v++)p[v]=(unsigned char)(b*31+k*43+v*17+j);
        }
    }
}

static void one(unsigned kind,unsigned from,unsigned frame,unsigned variant,int alias)
{
    Channel *c=channels+kind;
    unsigned n=c->components,source[4],source_copy[4],a[12],b[12];
    Channel before=*c;
    memcpy(source,c->initial,16);source[0]+=variant;
    memcpy(source_copy,source,16);
    for(unsigned i=0;i<12;i++)a[i]=b[i]=0xa5a5a5a5;
    if(alias) {memcpy(a+1,source,n*4);memcpy(b+1,source,n*4);}
    decoder_checked((void*)native[kind],c,alias?a+1:source,from,frame,a+1,a+1+n);
    reuse(kind,c,alias?b+1:source,from,frame,b+1,b+1+n);
    REQUIRE(!memcmp(a,b,sizeof a));REQUIRE(!memcmp(&before,c,sizeof before));
    REQUIRE(!memcmp(source,source_copy,16));REQUIRE(!decoder_abi_errors);cases++;
}

static void controls(void)
{
    unsigned streams_before=fnv((unsigned char*)streams,sizeof streams);
    /* Hit, changed state, changed frame: each broken-key control must fail
     * independently, including aliasing with the caller's cursor record. */
    for(unsigned kind=0;kind<3;kind++) {
        reset();one(kind,0,3,0,0);one(kind,0,3,0,0);
        one(kind,0,3,65536,0);
        reset();one(kind,0,3,0,0);one(kind,0,4,0,0);
        for(unsigned alias=0;alias<2;alias++) for(unsigned from=0;from<130;from+=7)
            for(unsigned frame=0;frame<130;frame+=5) {
                reset();one(kind,from,frame,0,alias);one(kind,from,frame,0,alias);
                one(kind,from,frame,0x80000000u,alias);
            }
    }
    /* Lifetime boundary: a changed stream is allowed only after reset. */
    reset();one(0,0,20,0,0);streams[0][1]^=0x71;reset();one(0,0,20,0,0);streams[0][1]^=0x71;
    REQUIRE(streams_before==fnv((unsigned char*)streams,sizeof streams));
    printf("CONTROLS,%u,ABI,%u\n",cases,decoder_abi_errors);
}

typedef struct {unsigned state[4],from,frame;} Input;
static Input inputs[48][64];
static volatile unsigned checksum;
static void prepare(unsigned distance,unsigned cold)
{
    for(unsigned j=0;j<48;j++) for(unsigned phase=0;phase<64;phase++) {
        Input *in=&inputs[j][phase];Channel *c=channels+j;unsigned next[4];
        in->from=cold?0:16+phase;in->frame=in->from+distance+(cold?phase:0);
        memcpy(in->state,c->initial,16);
        if(in->from) native[j%3](c,c->initial,0,in->from,in->state,next);
    }
}

static void verify_workload(unsigned distance,unsigned phases)
{
    reset();
    unsigned before=fnv((unsigned char*)inputs,sizeof inputs);
    unsigned channels_before=fnv((unsigned char*)channels,sizeof channels);
    unsigned streams_before=fnv((unsigned char*)streams,sizeof streams);
    for(unsigned unit=0;unit<512;unit++)for(unsigned j=0;j<48;j++) {
        Channel *c=channels+j;Input *in=&inputs[j][unit%phases];unsigned a[12],b[12];
        for(unsigned k=0;k<12;k++)a[k]=b[k]=0xa5a5a5a5;
        for(unsigned k=0;k<c->components;k++)a[k+1]=b[k+1]=in->state[k];
        native[j%3](c,a+1,in->from,in->frame,a+1,a+1+c->components);
        reuse(j%3,c,b+1,in->from,in->frame,b+1,b+1+c->components);
        REQUIRE(!memcmp(a,b,sizeof a));
    }
    REQUIRE(before==fnv((unsigned char*)inputs,sizeof inputs));
    REQUIRE(channels_before==fnv((unsigned char*)channels,sizeof channels));
    REQUIRE(streams_before==fnv((unsigned char*)streams,sizeof streams));
    printf("WORKLOAD,%u,%u,24576,%llu,%llu\n",distance,phases,hits,misses);
}

static unsigned long long timed(unsigned optimized,unsigned phases,unsigned repetitions)
{
    LARGE_INTEGER before,after;reset();REQUIRE(QueryPerformanceCounter(&before));
    for(unsigned repeat=0;repeat<repetitions;repeat++) {
        /* Epoch reset is charged to the candidate on every synthetic frame. */
        if(optimized) memset(cache,0,sizeof cache);
        for(unsigned unit=0;unit<512;unit++)for(unsigned j=0;j<48;j++) {
            Input *in=&inputs[j][unit%phases];unsigned output[8];
            unsigned n=channels[j].components;
            for(unsigned k=0;k<n;k++)output[k]=in->state[k];
            if(optimized)reuse(j%3,channels+j,output,in->from,in->frame,output,output+n);
            else native[j%3](channels+j,output,in->from,in->frame,output,output+n);
            checksum^=output[0];
        }
    }
    REQUIRE(QueryPerformanceCounter(&after));return (unsigned long long)(after.QuadPart-before.QuadPart);
}

int main(int argc,char **argv)
{
    REQUIRE(argc==5);unsigned samples=(unsigned)atoi(argv[3]);broken=(unsigned)atoi(argv[4]);
    REQUIRE(samples<=101 && broken<=3 && bench_no_game());
    load_native(argv[1],argv[2]);setup();controls();
    LARGE_INTEGER frequency;REQUIRE(QueryPerformanceFrequency(&frequency));
    printf("FREQUENCY,%llu\n",(unsigned long long)frequency.QuadPart);
    const unsigned phases[]={1,8,64};
    const unsigned distances[]={0,1,8,64};
    for(unsigned d=0;d<4;d++)for(unsigned p=0;p<3;p++) {
        REQUIRE(bench_no_game());prepare(distances[d],d==3);
        verify_workload(distances[d],phases[p]);
        if(!samples)continue;
        for(unsigned w=0;w<4;w++) {timed(0,phases[p],2);timed(1,phases[p],2);}
        for(unsigned s=0;s<samples;s++) {
            unsigned long long baseline,candidate,h,m;
            if(s&1) {candidate=timed(1,phases[p],4);h=hits;m=misses;baseline=timed(0,phases[p],4);}
            else {baseline=timed(0,phases[p],4);candidate=timed(1,phases[p],4);h=hits;m=misses;}
            printf("TIMING,%u,%u,%u,%llu,%llu,%llu,%llu\n",distances[d],phases[p],s,baseline,candidate,h,m);
        }
    }
    REQUIRE(bench_no_game());return 0;
}
