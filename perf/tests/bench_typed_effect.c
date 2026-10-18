/* Offline typed-write screen. No production hook or native source bodies. */
#define PF_EFFECT_MAIN generic_effect_main
#include "bench_effect.c"

enum {TY_FORWARD,TY_INT,TY_FLOAT,TY_VECTOR,TY_TRANSPOSE,TY_COUNT};
typedef struct { source_write *source; unsigned kind,bytes; unsigned data[16]; } typed_input;
static typed_input typed_inputs[512];

static unsigned typed_kind(const D3DXPARAMETER_DESC *d)
{
    if(d->Elements || d->StructMembers)return TY_FORWARD;
    if(d->Class==D3DXPC_SCALAR && d->Type==D3DXPT_INT && d->Bytes==4)return TY_INT;
    if(d->Class==D3DXPC_SCALAR && d->Type==D3DXPT_FLOAT && d->Bytes==4)return TY_FLOAT;
    if(d->Class==D3DXPC_VECTOR && d->Type==D3DXPT_FLOAT &&
       d->Rows==1 && d->Columns>=2 && d->Columns<=4 && d->Bytes==d->Columns*4)return TY_VECTOR;
    if((d->Class==D3DXPC_MATRIX_ROWS || d->Class==D3DXPC_MATRIX_COLUMNS) &&
       d->Type==D3DXPT_FLOAT && d->Rows==4 && d->Columns==4 && d->Bytes==64)return TY_TRANSPOSE;
    return TY_FORWARD;
}

static HRESULT typed_get(ID3DXEffect *fx,typed_input *in,void *out)
{
    switch(in->kind) {
    case TY_INT:return FXCALL(fx,GetInt,in->source->handle,(int *)out);
    case TY_FLOAT:return FXCALL(fx,GetFloat,in->source->handle,(float *)out);
    case TY_VECTOR:return FXCALL(fx,GetVector,in->source->handle,(D3DXVECTOR4 *)out);
    case TY_TRANSPOSE:return FXCALL(fx,GetMatrixTranspose,in->source->handle,(D3DXMATRIX *)out);
    default:return E_NOTIMPL;
    }
}

static HRESULT typed_write(ID3DXEffect *fx,typed_input *in,int filter,int recording)
{
    source_write *w=in->source;unsigned current[16];
    if(filter && !recording && in->kind && SUCCEEDED(typed_get(fx,in,current)) &&
       !memcmp(current,in->data,in->bytes))return S_OK;
    switch(in->kind) {
    case TY_INT:return FXCALL(fx,SetInt,w->handle,*(int *)in->data);
    case TY_FLOAT:{float value;memcpy(&value,in->data,4);return FXCALL(fx,SetFloat,w->handle,value);}
    case TY_VECTOR:return FXCALL(fx,SetVector,w->handle,(const D3DXVECTOR4 *)in->data);
    case TY_TRANSPOSE:return FXCALL(fx,SetMatrixTranspose,w->handle,(const D3DXMATRIX *)in->data);
    default:return FXCALL(fx,SetValue,w->handle,w->value,w->bytes);
    }
}

static void typed_prepare(ID3DXEffect *fx,const char *technique)
{
    unsigned kinds[TY_COUNT]={0};
    for(unsigned i=0;i<source_write_count;i++) {
        source_write *w=source_writes+i;typed_input *in=typed_inputs+i;
        D3DXPARAMETER_DESC d;HR(FXCALL(fx,GetParameterDesc,w->handle,&d));
        *in=(typed_input){.source=w};
        if(w->raw)continue;
        in->kind=typed_kind(&d);
        in->bytes=d.Bytes;kinds[in->kind]++;
        if(in->kind)HR(typed_get(fx,in,in->data));
    }
    printf("TYPED_SHAPES,%s,%u,%u,%u,%u,%u\n",technique,kinds[0],kinds[1],kinds[2],kinds[3],kinds[4]);
}

static void typed_static(ID3DXEffect *fx,int filter,int recording)
{
    for(unsigned i=0;i<source_write_count;i++)if(!source_writes[i].raw)
        HR(typed_write(fx,typed_inputs+i,filter,recording));
}

static void typed_roundtrips(ID3DXEffect *fx)
{
    unsigned checked=0;
    for(unsigned i=0;i<source_write_count;i++) {
        typed_input *in=typed_inputs+i;if(!in->kind)continue;
        unsigned saved[16],expected[16],actual[16];memcpy(saved,in->data,sizeof saved);
        for(unsigned j=0;j<in->bytes/4;j++)in->data[j]=in->kind==TY_INT?3:0x3e800000u+j*0x8000u;
        memcpy(expected,in->data,sizeof expected);
        HR(typed_write(fx,in,0,0));HR(typed_get(fx,in,actual));REQUIRE(!memcmp(actual,expected,in->bytes));
        in->data[0]^=1;HR(typed_write(fx,in,0,0));
        HR(typed_get(fx,in,actual));REQUIRE(memcmp(actual,expected,in->bytes));
        memcpy(in->data,expected,sizeof expected);
        HR(typed_write(fx,in,1,0));HR(typed_get(fx,in,actual));REQUIRE(!memcmp(actual,expected,in->bytes));
        memcpy(in->data,saved,sizeof saved);checked++;
    }
    source_write_all(fx);
    printf("TYPED_ROUNDTRIPS,%u,nonzero scalars/vectors and asymmetric matrices; changed values recover through filter\n",checked);
}

static void typed_snapshot(ID3DXEffect *fx,unsigned *out)
{
    UINT passes=0;HR(FXCALL(fx,Begin,&passes,6));REQUIRE(passes==1);
    HR(FXCALL(fx,BeginPass,0));HR(FXCALL(fx,CommitChanges));
    HR(IDirect3DDevice9_GetVertexShaderConstantF(device,0,(float *)out,256));
    HR(IDirect3DDevice9_GetPixelShaderConstantF(device,0,(float *)(out+1024),32));
    IDirect3DVertexShader9 *vs=NULL;IDirect3DPixelShader9 *ps=NULL;
    HR(IDirect3DDevice9_GetVertexShader(device,&vs));HR(IDirect3DDevice9_GetPixelShader(device,&ps));
    out[1152]=shader_hash(vs,0);out[1153]=shader_hash(ps,1);
    if(vs)IDirect3DVertexShader9_Release(vs);
    if(ps)IDirect3DPixelShader9_Release(ps);
    HR(FXCALL(fx,EndPass));HR(FXCALL(fx,End));
}

static void typed_controls(ID3DXEffect *fx)
{
    unsigned expected[1154],actual[1154],first[1154],poison[1024];
    for(unsigned i=0;i<1024;i++)poison[i]=0x3e800000u;
    REQUIRE(typed_inputs[0].kind==TY_INT && typed_inputs[0].data[0]==1);
    for(unsigned pose=0;pose<3;pose++)for(unsigned mode=0;mode<3;mode++) {
        source_write_all(fx);cycle(fx,6);
        HR(IDirect3DDevice9_SetVertexShaderConstantF(device,0,(const float *)poison,256));
        HR(IDirect3DDevice9_SetPixelShaderConstantF(device,0,(const float *)poison,32));
        HR(FXCALL(fx,SetInt,source_writes[0].handle,0));
        if(mode<2)typed_static(fx,mode,0); /* Third mode deliberately omits all static writes. */
        source_write_pose(fx,pose*17);typed_snapshot(fx,mode?actual:expected);
        if(mode==1)REQUIRE(!memcmp(expected,actual,sizeof expected));
        if(mode==2)REQUIRE(memcmp(expected,actual,sizeof expected));
        if(!mode && !pose)memcpy(first,expected,sizeof expected);
        if(!mode && pose)REQUIRE(memcmp(first,expected,sizeof expected));
    }
    /* Capturing a currently identical value still changes the future block.
     * The fixture knows it is recording; a production hook has no such proof. */
    for(unsigned mode=0;mode<3;mode++) {
        source_write_all(fx);HR(FXCALL(fx,BeginParameterBlock));
        HR(typed_write(fx,typed_inputs,mode!=0,mode!=2));
        D3DXHANDLE block=FXCALL(fx,EndParameterBlock);REQUIRE(block);
        HR(FXCALL(fx,SetInt,source_writes[0].handle,0));
        HRESULT applied=FXCALL(fx,ApplyParameterBlock,block);
        printf("TYPED_BLOCK,%u,%08lX\n",mode,(unsigned long)applied);
        if(mode!=2)HR(applied);
        int current=-1;HR(FXCALL(fx,GetInt,source_writes[0].handle,&current));
        if(mode==2)REQUIRE(FAILED(applied) || current!=1);
        else REQUIRE(current==1);
        HR(FXCALL(fx,DeleteParameterBlock,block));
    }
    for(unsigned kind=TY_INT;kind<TY_COUNT;kind++) {
        typed_input invalid=typed_inputs[0];source_write missing=*invalid.source;
        missing.handle=NULL;invalid.source=&missing;invalid.kind=kind;
        invalid.bytes=kind==TY_TRANSPOSE?64:kind==TY_VECTOR?16:4;
        HRESULT a=typed_write(fx,&invalid,0,0),b=typed_write(fx,&invalid,1,0);
        REQUIRE(FAILED(a) && a==b);
    }
    source_write_all(fx);
    puts("TYPED_CONTROLS,3 changed poses; poisoned constants and external parameter restored; 3 stale skips rejected; recording bypass preserves blocks; unguarded block filter rejected; 4 invalid-handle HRESULTs preserved");
}

static void typed_conversion_control(create_effect_fn create)
{
    static const char code[]="float value=1.5; float4 pixel():COLOR0{return value;} technique T{pass P{PixelShader=compile ps_2_0 pixel();}}";
    ID3DXEffect *fx=NULL;HR(create(device,code,sizeof code-1,NULL,NULL,0,NULL,&fx,NULL));
    D3DXHANDLE h=FXCALL(fx,GetParameterByName,NULL,"value");REQUIRE(h);
    D3DXPARAMETER_DESC desc;HR(FXCALL(fx,GetParameterDesc,h,&desc));
    REQUIRE(typed_kind(&desc)==TY_FLOAT && typed_kind(&desc)!=TY_INT);
    int converted;float actual;
    HR(FXCALL(fx,GetInt,h,&converted));REQUIRE(converted==1);
    HR(FXCALL(fx,GetFloat,h,&actual));REQUIRE(actual==1.5f);
    /* A getter match alone would skip this setter and retain the wrong float. */
    HR(FXCALL(fx,SetInt,h,1));HR(FXCALL(fx,GetFloat,h,&actual));REQUIRE(actual==1.0f);
    FXCALL(fx,Release);
    puts("TYPED_CONVERSION_CONTROL,GetInt equality on fractional float is not setter equivalence; exact descriptor gate required");
}

static double typed_measure(ID3DXEffect *fx,int filter,double *complete)
{
    source_write_all(fx);drain();long long begin=ticks();
    for(unsigned repeat=0;repeat<8;repeat++)for(unsigned group=0;group<850;group++) {
        typed_static(fx,filter,0);source_write_pose(fx,group);cycle(fx,6);
    }
    long long submitted=ticks();drain();
    *complete=(ticks()-begin)*1000.0/frequency/8;
    return (submitted-begin)*1000.0/frequency/8;
}

static void typed_screen(ID3DXEffect *fx)
{
    const char *names[]={"Default","_CreateShadowMap"};
    for(unsigned t=0;t<2;t++) {
        D3DXHANDLE tech=FXCALL(fx,GetTechniqueByName,names[t]);REQUIRE(tech);
        HR(FXCALL(fx,SetTechnique,tech));
        D3DXHANDLE joints=FXCALL(fx,GetParameterByName,NULL,"NumJointsPerVertex");REQUIRE(joints);
        HR(FXCALL(fx,SetInt,joints,1));
        D3DXEFFECT_DESC desc;HR(FXCALL(fx,GetDesc,&desc));source_write_count=source_write_bytes=0;
        for(unsigned p=0;p<desc.Parameters;p++) {
            D3DXHANDLE h=FXCALL(fx,GetParameter,NULL,p);D3DXPARAMETER_DESC d;
            HR(FXCALL(fx,GetParameterDesc,h,&d));const char *address=source_annotation(fx,h);
            if((!address && !d.Semantic) || !FXCALL(fx,IsParameterUsed,h,tech))continue;
            source_collect_writes(fx,h,0,address && source_family(address)==7 && d.Elements && d.StructMembers);
        }
        typed_prepare(fx,names[t]);typed_roundtrips(fx);typed_controls(fx);
        double savings[21],completed[21];
        for(unsigned r=0;r<25;r++) {
            REQUIRE(bench_no_game());double cpu[2],gpu[2];
            for(unsigned j=0;j<2;j++){unsigned mode=j^(r&1);cpu[mode]=typed_measure(fx,mode,gpu+mode);}
            if(r>=4) {
                unsigned i=r-4;savings[i]=cpu[0]-cpu[1];completed[i]=gpu[0]-gpu[1];
                printf("TYPED_SAMPLE,%s,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",names[t],i,cpu[0],cpu[1],gpu[0],gpu[1],savings[i],completed[i]);
            }
        }
        qsort(savings,21,sizeof *savings,cmp);qsort(completed,21,sizeof *completed,cmp);
        printf("TYPED_SUMMARY,%s,%.6f,%.6f,%.6f,%.6f\n",names[t],savings[10],savings[2],savings[18],completed[10]);
        for(unsigned i=0;i<source_write_count;i++)free(source_writes[i].value);
    }
}

int main(int argc,char **argv)
{
    REQUIRE(argc==2 && bench_no_game());char path[MAX_PATH],backend[MAX_PATH];
    snprintf(path,sizeof path,"%s/Shaders.big",argv[1]);unsigned size;
    unsigned char *big=read_file(path,&size);REQUIRE(big && size>=16);
    snprintf(path,sizeof path,"%s/d3dx9_27.dll",argv[1]);HMODULE dll=LoadLibraryA(path),d9=LoadLibraryA("d3d9.dll");
    REQUIRE(dll && d9);create_effect_fn create=(create_effect_fn)(void *)GetProcAddress(dll,"D3DXCreateEffect");REQUIRE(create);
    REQUIRE(GetModuleFileNameA(d9,backend,sizeof backend));printf("backend: %s\n",backend);
    create9_fn make=(create9_fn)(void *)GetProcAddress(d9,"Direct3DCreate9");REQUIRE(make);
    IDirect3D9 *d3d=make(D3D_SDK_VERSION);REQUIRE(d3d);
    HWND wnd=CreateWindowExA(0,"STATIC","typed_effect_screen",WS_POPUP,0,0,64,64,NULL,NULL,NULL,NULL);REQUIRE(wnd);
    D3DPRESENT_PARAMETERS pp={0};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=wnd;pp.BackBufferWidth=pp.BackBufferHeight=64;
    HR(IDirect3D9_CreateDevice(d3d,0,D3DDEVTYPE_HAL,wnd,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device));
    HR(IDirect3DDevice9_CreateQuery(device,D3DQUERYTYPE_EVENT,&fence));LARGE_INTEGER f;QueryPerformanceFrequency(&f);frequency=(double)f.QuadPart;
    typed_conversion_control(create);unsigned pos=16,checked=0;
    for(unsigned i=0;i<be32(big+8);i++) {
        REQUIRE(pos+8<size);unsigned off=be32(big+pos),len=be32(big+pos+4);const char *name=(const char *)big+pos+8;
        REQUIRE(memchr(name,0,size-pos-8));pos+=8+(unsigned)strlen(name)+1;
        if(!ends_with(name,"defaultw3d.fxo"))continue;
        REQUIRE(off<=size && len<=size-off);ID3DXEffect *fx=NULL;
        HR(create(device,big+off,len,NULL,NULL,0,NULL,&fx,NULL));typed_screen(fx);FXCALL(fx,Release);checked++;
    }
    REQUIRE(checked==1);free(big);IDirect3DQuery9_Release(fence);IDirect3DDevice9_Release(device);IDirect3D9_Release(d3d);
    DestroyWindow(wnd);FreeLibrary(dll);FreeLibrary(d9);REQUIRE(bench_no_game());puts("TYPED_SCREEN_OK");return 0;
}
