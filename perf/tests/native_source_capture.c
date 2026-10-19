/* Reuse our verified private-body loader and independent matrix oracle.
 * No source/reference bytes are compiled into this translation unit. */
#define main unused_matrix_source_main
#include "bench_native_matrix_sources.c"
#undef main
#include "source_packet.h"

static DWORD producer_id;
static HMODULE capture_d3dx;
static source_matrix_command *capture_output;
static unsigned capture_count,baseline_calls;

static HRESULT WINAPI capture_matrix(void *self,void *parameter,const float *value)
{
    REQUIRE(GetCurrentThreadId()==producer_id && self==&effect && capture_output);
    REQUIRE((unsigned)parameter==expected_parameter && expected_parameter<3 && capture_count==expected_parameter);
    source_matrix_command *out=&capture_output[capture_count++];
    out->semantic=expected_parameter;out->transpose=1;
    memcpy(out->value,value,sizeof out->value);calls++;return S_OK;
}

void native_source_capture_init(const char *image,const char *manifest,const char *d3dx)
{
    producer_id=GetCurrentThreadId();capture_d3dx=LoadLibraryA(d3dx);REQUIRE(capture_d3dx);
    symbols[9]=GetProcAddress(capture_d3dx,"D3DXMatrixInverse");
    symbols[10]=GetProcAddress(capture_d3dx,"D3DXMatrixTranspose");REQUIRE(symbols[9] && symbols[10]);
    symbols[11]=&flags;symbols[12]=state;symbols[13]=projection;symbols[14]=&one;
    load_bodies(image,manifest);
    effect_vt[44]=record_transpose;effect_vt[38]=record_matrix;
    for(unsigned p=0;p<3;p++){provider_vt[p][2]=symbols[p+3];providers[p]=provider_vt[p];}
    unsigned n=correctness();REQUIRE(n==54);baseline_calls=calls;
    effect_vt[44]=capture_matrix;effect_vt[38]=NULL;
    printf("NATIVE SOURCES: %u independent matrix/identity/inverse/singular oracle cases passed; producer=%lu\n",n,(unsigned long)producer_id);
}

void native_source_capture(const float values[3][16],source_matrix_command output[3])
{
    REQUIRE(GetCurrentThreadId()==producer_id && !capture_output);
    memcpy(input(2),values[0],64);memcpy(input(0),values[1],64);memcpy(input(1),values[2],64);
    flags=0;capture_count=0;capture_output=output;
    static const unsigned provider[3]={2,0,1};
    for(unsigned i=0;i<3;i++){expected_parameter=i;invoke(provider[i],0);}
    REQUIRE(capture_count==3);capture_output=NULL;
    /* These bytes are borrowed only during the callback. Destroy them before
     * any packet can be published; a sink retaining a pointer cannot pass. */
    memset(state,0xA5,sizeof state);memset(projection,0xA5,sizeof projection);
}

unsigned native_source_capture_calls(void)
{ REQUIRE(GetCurrentThreadId()==producer_id);return calls-baseline_calls; }

void native_source_capture_close(void)
{
    REQUIRE(GetCurrentThreadId()==producer_id && !capture_output);
    for(unsigned i=0;i<BODIES;i++)REQUIRE(VirtualFree(symbols[i],0,MEM_RELEASE));
    FreeLibrary(capture_d3dx);
}
