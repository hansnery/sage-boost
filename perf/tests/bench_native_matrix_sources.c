/* Offline source-computation screen. Original native bodies are loaded from
 * the user's image into private RAM; no game process or EA assets are saved.
 * A recording setter replaces D3DX parameter storage, so this measures only
 * source computation/dispatch plus a 64-byte recording sink. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "bench_guard.h"

#define REQUIRE(x) do { if(!(x)) { fprintf(stderr,"matrix source check failed at line %d: %s\n",__LINE__,#x); exit(2); } } while(0)
#define BODIES 9
#define SYMBOLS 15
#define GROUPS 1222
#define REPEATS 200
#define SAMPLES 21
static void *symbols[SYMBOLS];
static unsigned flags;
static unsigned char state[0x290];
static float projection[16],one=1;
static void *effect_vt[45],*effect=effect_vt;
static void *provider_vt[3][3],*providers[3];
static float captured[16];
static volatile unsigned calls;
static unsigned setter_kind,expected_parameter;
typedef void (__attribute__((thiscall)) *source_fn)(void *,void *,void *);
typedef struct {unsigned body,offset,kind,symbol,add;} Patch;

static unsigned fnv(const unsigned char *p,unsigned n)
{ unsigned h=2166136261u; while(n--)h=(h^*p++)*16777619u; return h; }

static void load_bodies(const char *image,const char *manifest)
{
    const unsigned sizes[BODIES]={52,87,87,404,300,404,22,34,3};
    FILE *f=fopen(image,"rb"); REQUIRE(f);
    REQUIRE(!fseek(f,0,SEEK_END)); long length=ftell(f); rewind(f);
    REQUIRE(length>4096 && length<128*1024*1024);
    unsigned char *raw=malloc(length); REQUIRE(raw);
    REQUIRE(fread(raw,1,length,f)==(size_t)length); fclose(f);
    REQUIRE(((IMAGE_DOS_HEADER*)raw)->e_magic==IMAGE_DOS_SIGNATURE);
    unsigned h=((IMAGE_DOS_HEADER*)raw)->e_lfanew;
    REQUIRE(h<=(unsigned)length-sizeof(IMAGE_NT_HEADERS32));
    IMAGE_NT_HEADERS32 *nt=(void*)(raw+h);
    REQUIRE(nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.Machine==IMAGE_FILE_MACHINE_I386 && nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC);
    IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);
    REQUIRE((unsigned char *)(sections+nt->FileHeader.NumberOfSections)<=raw+length);
    f=fopen(manifest,"r"); REQUIRE(f); Patch patches[128]; unsigned np=0;
    for(unsigned i=0;i<BODIES;i++) {
        unsigned rva,size,hash,n,offset=0,matches=0;
        REQUIRE(fscanf(f,"%u %u %u %u",&rva,&size,&hash,&n)==4 && size==sizes[i] && n<40);
        for(unsigned j=0;j<nt->FileHeader.NumberOfSections;j++) {
            IMAGE_SECTION_HEADER *s=&sections[j];
            if(rva>=s->VirtualAddress && rva-s->VirtualAddress<=s->SizeOfRawData &&
               size<=s->SizeOfRawData-(rva-s->VirtualAddress) && (s->Characteristics&IMAGE_SCN_MEM_EXECUTE)) {
                REQUIRE(s->PointerToRawData<=(unsigned)length && s->SizeOfRawData<=(unsigned)length-s->PointerToRawData);
                offset=s->PointerToRawData+rva-s->VirtualAddress; matches++;
            }
        }
        REQUIRE(matches==1 && fnv(raw+offset,size)==hash);
        symbols[i]=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); REQUIRE(symbols[i]);
        memcpy(symbols[i],raw+offset,size);
        for(unsigned j=0;j<n;j++) {
            REQUIRE(np<128); Patch *p=&patches[np++]; p->body=i;
            REQUIRE(fscanf(f,"%u %u %u %u",&p->offset,&p->kind,&p->symbol,&p->add)==4);
            REQUIRE(size>=4 && p->offset<=size-4 && p->kind<=1 && p->symbol<SYMBOLS && p->add<0x290);
        }
    }
    {char extra; REQUIRE(fscanf(f," %c",&extra)==EOF);} fclose(f); free(raw);
    for(unsigned i=0;i<np;i++) {
        Patch p=patches[i]; unsigned char *field=(unsigned char *)symbols[p.body]+p.offset;
        REQUIRE(symbols[p.symbol]);
        *(unsigned *)field=(unsigned)symbols[p.symbol]+p.add-(p.kind?0:(unsigned)(field+4));
    }
    for(unsigned i=0;i<BODIES;i++) {
        DWORD old; REQUIRE(VirtualProtect(symbols[i],4096,PAGE_EXECUTE_READ,&old));
        REQUIRE(FlushInstructionCache(GetCurrentProcess(),symbols[i],4096));
    }
}

static HRESULT WINAPI record_transpose(void *self,void *parameter,const float *matrix)
{
    REQUIRE(self==&effect && (unsigned)parameter==expected_parameter);
    memcpy(captured,matrix,sizeof captured); setter_kind=0; calls++; return S_OK;
}
static HRESULT WINAPI record_matrix(void *self,void *parameter,const float *matrix)
{
    HRESULT result=record_transpose(self,parameter,matrix); setter_kind=1; return result;
}
static void invoke(unsigned provider,unsigned callback)
{
    ((source_fn)symbols[callback])(&providers[provider],&effect,(void *)expected_parameter);
}
static void transpose(float *out,const float *in)
{ for(unsigned r=0;r<4;r++)for(unsigned c=0;c<4;c++)out[r*4+c]=in[c*4+r]; }
static void identity(float *m)
{ memset(m,0,64); for(unsigned i=0;i<4;i++)m[i*5]=1; }

/* Independent double-precision elimination; never calls D3DX for the oracle. */
static int inverse(float *out,const float *in)
{
    double a[4][8];
    for(unsigned r=0;r<4;r++)for(unsigned c=0;c<8;c++)a[r][c]=c<4?in[r*4+c]:(c-4==r);
    for(unsigned c=0;c<4;c++) {
        unsigned pivot=c;
        for(unsigned r=c+1;r<4;r++)if(fabs(a[r][c])>fabs(a[pivot][c]))pivot=r;
        if(fabs(a[pivot][c])<1e-12)return 0;
        for(unsigned k=0;k<8;k++) { double t=a[c][k]; a[c][k]=a[pivot][k]; a[pivot][k]=t; }
        double divisor=a[c][c]; for(unsigned k=0;k<8;k++)a[c][k]/=divisor;
        for(unsigned r=0;r<4;r++)if(r!=c) {
            double factor=a[r][c]; for(unsigned k=0;k<8;k++)a[r][k]-=factor*a[c][k];
        }
    }
    for(unsigned r=0;r<4;r++)for(unsigned c=0;c<4;c++)out[r*4+c]=(float)a[r][c+4];
    return 1;
}
static int equal(const float *a,const float *b)
{
    for(unsigned i=0;i<16;i++)if(!isfinite(a[i]) || !isfinite(b[i]) ||
        fabs((double)a[i]-b[i])>0.00002*(1+fabs(b[i])))return 0;
    return 1;
}
static float *input(unsigned provider)
{ return provider==0?(float *)(state+0x22c):provider==1?projection:(float *)(state+0x1ec); }
static void fill(unsigned variant)
{
    const float base[16]={2,1,0,3, 0,3,1,4, 1,0,4,5, 0,0,0,1};
    for(unsigned p=0;p<3;p++) {
        memcpy(input(p),base,64);
        input(p)[0]+=p+variant;
        input(p)[7]+=2*p+variant;
    }
}
static unsigned correctness(void)
{
    unsigned checks=0,transpose_rejected=0,changed=0;
    float previous[3][3][16];
    for(unsigned variant=0;variant<6;variant++) {
        fill(variant); flags=0;
        if(variant==2)flags=1u<<19;
        if(variant==3)flags=1u<<18;
        if(variant==4)flags=(1u<<18)|(1u<<19);
        if(variant==5)for(unsigned p=0;p<3;p++) {
            /* Asymmetric singular matrix exercises the actual fallback. */
            memset(input(p)+12,0,16);
        }
        for(unsigned p=0;p<3;p++)for(unsigned callback=0;callback<3;callback++) {
            float source[16],expected[16],wrong[16];
            if((p==0 && flags&(1u<<19)) || (p==2 && flags&(1u<<18)))identity(source);
            else transpose(source,input(p));
            if(!callback)memcpy(expected,source,64);
            else if(!inverse(expected,source))transpose(expected,source);
            expected_parameter=0x100+p*4+callback;
            unsigned old=calls; invoke(p,callback);
            REQUIRE(calls==old+1 && setter_kind==(callback==2));
            REQUIRE(equal(captured,expected));
            transpose(wrong,expected); transpose_rejected+=!equal(captured,wrong);
            if(variant==1) { REQUIRE(!equal(captured,previous[p][callback])); changed++; }
            memcpy(previous[p][callback],captured,64); checks++;
        }
    }
    REQUIRE(changed==9 && transpose_rejected>0);
    return checks;
}
static double sample(unsigned workload,LARGE_INTEGER frequency)
{
    LARGE_INTEGER start,end; unsigned before=calls;
    expected_parameter=0x1234;
    REQUIRE(QueryPerformanceCounter(&start));
    for(unsigned repeat=0;repeat<REPEATS;repeat++)for(unsigned g=0;g<GROUPS;g++) {
        if(workload==2)invoke(0,1);
        else {
            invoke(2,0); invoke(0,0); invoke(1,0);
            if(!workload)invoke(0,1);
        }
    }
    REQUIRE(QueryPerformanceCounter(&end));
    REQUIRE(calls-before==REPEATS*GROUPS*(workload==0?4:workload==1?3:1));
    return (end.QuadPart-start.QuadPart)*1000.0/frequency.QuadPart/REPEATS;
}

int main(int argc,char **argv)
{
    REQUIRE(argc==4 || (argc==5 && !strcmp(argv[4],"swap-view")));
    REQUIRE(bench_no_game());
    HMODULE d3dx=LoadLibraryA(argv[3]); REQUIRE(d3dx);
    symbols[9]=GetProcAddress(d3dx,"D3DXMatrixInverse");
    symbols[10]=GetProcAddress(d3dx,"D3DXMatrixTranspose");
    REQUIRE(symbols[9] && symbols[10]);
    symbols[11]=&flags; symbols[12]=state; symbols[13]=projection; symbols[14]=&one;
    load_bodies(argv[1],argv[2]);
    effect_vt[44]=record_transpose; effect_vt[38]=record_matrix;
    for(unsigned p=0;p<3;p++) { provider_vt[p][2]=symbols[p+3]; providers[p]=provider_vt[p]; }
    if(argc==5)provider_vt[0][2]=symbols[5];
    unsigned checks=correctness();
    fill(7); flags=0;
    LARGE_INTEGER frequency; REQUIRE(QueryPerformanceFrequency(&frequency));
    double samples[3][SAMPLES];
    for(unsigned w=0;w<3;w++)sample(w,frequency);
    for(unsigned s=0;s<SAMPLES;s++)for(unsigned k=0;k<3;k++) {
        unsigned w=(s+k)%3; samples[w][s]=sample(w,frequency);
    }
    REQUIRE(bench_no_game());
    printf("{\n  \"scope\": \"Native matrix computation/dispatch with recording setters; no real effect storage or rendering\",\n");
    printf("  \"checks\": %u,\n  \"groups\": %u,\n  \"repeats_per_sample\": %u,\n  \"samples_ms\": {\n",checks,GROUPS,REPEATS);
    const char *names[]={"world_view_projection_inverse","world_view_projection","view_inverse"};
    for(unsigned w=0;w<3;w++) {
        printf("    \"%s\": [",names[w]);
        for(unsigned s=0;s<SAMPLES;s++)printf("%s%.9f",s?", ":"",samples[w][s]);
        printf("]%s\n",w==2?"":",");
    }
    printf("  }\n}\n");
    for(unsigned i=0;i<BODIES;i++)REQUIRE(VirtualFree(symbols[i],0,MEM_RELEASE));
    FreeLibrary(d3dx); return 0;
}
