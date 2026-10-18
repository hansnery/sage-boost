/* Local effect metadata only. No shader/source/parameter payload persists.
 * Callers supply the existing bench_effect recorder and D3DX definitions. */
#ifndef EFFECT_SOURCE_AUDIT_H
#define EFFECT_SOURCE_AUDIT_H

static const char *const source_semantics[]={
    "World","View","Projection","WorldView","WorldViewProjection","Time","Ambient"
};
static const char *const source_addresses[]={
    "Sas.Skeleton.MeshToJointToWorld[0]","Sas.Camera.WorldToView",
    "Sas.Camera.Projection","Sas.Skeleton.MeshToJointToView[0]",
    "Sas.Skeleton.MeshToJointToProjection[0]","Sas.Time.Now","Sas.AmbientLight[0].Color"
};
static const char *const source_suffixes[]={"","Inverse","InverseTranspose"};

static unsigned source_hash(const char *text)
{return text?fnv1a((const unsigned char *)text,strlen(text)):0;}

static const char *source_annotation(ID3DXEffect *fx,D3DXHANDLE parameter)
{
    D3DXHANDLE a=FXCALL(fx,GetAnnotationByName,parameter,"SasBindAddress");
    const char *value=NULL;
    if(a && FAILED(FXCALL(fx,GetString,a,&value)))value=NULL;
    return value;
}

static unsigned source_family(const char *address)
{
    static const char *const prefixes[]={"Sas.Camera","Sas.Time","Sas.AmbientLight",
        "Sas.DirectionalLight","Sas.PointLight","Sas.Shadow","Sas.Skeleton"};
    for(unsigned i=0;i<sizeof prefixes/sizeof *prefixes;i++) {
        size_t n=strlen(prefixes[i]);
        if(!_strnicmp(address,prefixes[i],n) &&
           (!address[n] || address[n]=='.' || address[n]=='['))return i+1;
    }
    return 0;
}

static int source_dynamic_annotation(ID3DXEffect *fx,D3DXHANDLE parameter)
{
    D3DXHANDLE a=FXCALL(fx,GetAnnotationByName,parameter,"WW3DDynamicSet");
    int value=0;
    if(a && SUCCEEDED(FXCALL(fx,GetInt,a,&value)) && value>=0 && value<6)return value;
    return -1; /* Absent/invalid annotation; native namespace rules still apply. */
}

/* These are source requests, not reconstructed native callback lists.
 * Native nested resolution can expand roots into multiple callbacks. */
static unsigned source_semantic_requests(ID3DXEffect *fx,D3DXHANDLE technique,
                                         unsigned *duplicates,int emit,
                                         unsigned runtime,unsigned effect,unsigned tech)
{
    unsigned count=0;*duplicates=0;
    for(unsigned i=0;i<7;i++)for(unsigned j=0;j<(i<5?3u:1u);j++) {
        char semantic[80],address[160];
        snprintf(semantic,sizeof semantic,"%s%s",source_semantics[i],source_suffixes[j]);
        D3DXHANDLE parameter=FXCALL(fx,GetParameterBySemantic,NULL,semantic);
        if(!parameter || !FXCALL(fx,IsParameterUsed,parameter,technique))continue;
        snprintf(address,sizeof address,"%s%s%s",source_addresses[i],j?".":"",source_suffixes[j]);
        const char *annotation=source_annotation(fx,parameter);
        unsigned duplicate=annotation && !_stricmp(annotation,address);
        D3DXPARAMETER_DESC d;HR(FXCALL(fx,GetParameterDesc,parameter,&d));
        count++;*duplicates+=duplicate;
        if(emit)printf("SOURCE_SEMANTIC,%u,%08X,%08X,%08X,%u,%u,%u,%d\n",
            runtime,effect,tech,source_hash(d.Name),i,j,duplicate,source_dynamic_annotation(fx,parameter));
    }
    return count;
}

static void source_control(create_effect_fn create,unsigned runtime)
{
    static const char code[]=
        "float4x4 wvp : WorldViewProjection < string SasBindAddress = "
        "\"Sas.Skeleton.MeshToJointToProjection[0]\"; >;"
        "float4x4 view : View;"
        "float4 pixel() : COLOR0 { return wvp[0]+view[0]; }"
        "technique T { pass P { PixelShader = compile ps_2_0 pixel(); } }";
    ID3DXEffect *fx=NULL;ID3DXBuffer *errors=NULL;
    HRESULT result=create(device,code,sizeof code-1,NULL,NULL,0,NULL,&fx,&errors);
    if(FAILED(result) && errors)fprintf(stderr,"source control: %s\n",(char *)errors->lpVtbl->GetBufferPointer(errors));
    if(errors)errors->lpVtbl->Release(errors);
    HR(result);
    D3DXHANDLE technique=FXCALL(fx,GetTechniqueByName,"T");REQUIRE(technique);
    unsigned duplicates=0,requests=source_semantic_requests(fx,technique,&duplicates,0,0,0,0);
    REQUIRE(requests==2 && duplicates==1);
    printf("SOURCE_CONTROL,%u,%u,%u\n",runtime,requests,duplicates);
    FXCALL(fx,Release);
}

static void audit_sources(ID3DXEffect *fx,unsigned runtime,const char *name)
{
    D3DXEFFECT_DESC desc;HR(FXCALL(fx,GetDesc,&desc));
    for(unsigned t=0;t<desc.Techniques;t++) {
        D3DXHANDLE technique=FXCALL(fx,GetTechnique,t);D3DXTECHNIQUE_DESC td;
        HR(FXCALL(fx,GetTechniqueDesc,technique,&td));
        unsigned effect=source_hash(name),tech=source_hash(td.Name),roots=0;
        for(unsigned p=0;p<desc.Parameters;p++) {
            D3DXHANDLE parameter=FXCALL(fx,GetParameter,NULL,p);
            const char *address=source_annotation(fx,parameter);
            if(!address || !FXCALL(fx,IsParameterUsed,parameter,technique))continue;
            D3DXPARAMETER_DESC d;HR(FXCALL(fx,GetParameterDesc,parameter,&d));roots++;
            printf("SOURCE_ROOT,%u,%08X,%08X,%08X,%08X,%u,%u,%u,%u,%d\n",runtime,
                effect,tech,source_hash(d.Name),source_hash(address),source_family(address),
                (unsigned)d.Class,d.Elements,d.StructMembers,source_dynamic_annotation(fx,parameter));
        }
        unsigned duplicates=0;
        unsigned semantics=source_semantic_requests(fx,technique,&duplicates,1,runtime,effect,tech);
        printf("SOURCE_DEMAND,%u,%08X,%08X,%u,%u,%u\n",runtime,effect,tech,roots,semantics,duplicates);
        if(ends_with(name,"defaultw3d.fxo"))
            printf("SOURCE_UNIT,%u,%s,%08X,%08X,%u,%u,%u\n",runtime,td.Name,effect,tech,roots,semantics,duplicates);
    }
}

typedef struct {D3DXHANDLE handle;unsigned bytes,storage,raw;unsigned char *value;} source_write;
static source_write source_writes[512];
static unsigned source_write_count,source_write_bytes;

static void source_collect_writes(ID3DXEffect *fx,D3DXHANDLE handle,unsigned depth,int palette)
{
    D3DXPARAMETER_DESC d;HR(FXCALL(fx,GetParameterDesc,handle,&d));REQUIRE(depth<16);
    if(d.Class==D3DXPC_OBJECT)return; /* Texture/reference work is outside this screen. */
    if(!palette && d.Elements) {
        REQUIRE(d.Elements<=4096);
        for(unsigned i=0;i<d.Elements;i++)source_collect_writes(fx,FXCALL(fx,GetParameterElement,handle,i),depth+1,0);
    } else if(!palette && d.StructMembers) {
        REQUIRE(d.StructMembers<=256);
        for(unsigned i=0;i<d.StructMembers;i++)source_collect_writes(fx,FXCALL(fx,GetParameter,handle,i),depth+1,0);
    } else if(d.Bytes) {
        REQUIRE(source_write_count<512 && d.Bytes<=65536);
        unsigned bytes=d.Bytes;
        /* A 20-bone palette is one native upload, not 90 independent fields.
         * This is a shape-based cost fixture, not callback reconstruction. */
        if(palette) {REQUIRE(d.Elements>=20 && !(bytes%d.Elements));bytes=20*(bytes/d.Elements);}
        source_write *w=source_writes+source_write_count++;
        *w=(source_write){handle,bytes,d.Bytes,palette!=0,calloc(1,d.Bytes)};REQUIRE(w->value);
        HR(FXCALL(fx,GetValue,handle,w->value,w->storage));source_write_bytes+=bytes;
    }
}

static void source_write_all(ID3DXEffect *fx)
{
    for(unsigned i=0;i<source_write_count;i++) {
        source_write *w=source_writes+i;
        if(w->raw)HR(FXCALL(fx,SetRawValue,w->handle,w->value,0,w->bytes));
        else HR(FXCALL(fx,SetValue,w->handle,w->value,w->bytes));
    }
}

static void source_write_static(ID3DXEffect *fx)
{
    for(unsigned i=0;i<source_write_count;i++) {
        source_write *w=source_writes+i;
        if(!w->raw)HR(FXCALL(fx,SetValue,w->handle,w->value,w->bytes));
    }
}

static void source_write_filtered(ID3DXEffect *fx,int broken)
{
    for(unsigned i=0;i<source_write_count;i++) {
        source_write *w=source_writes+i;if(w->raw)continue;
        unsigned char current[64];
        if(broken)continue;
        if(w->bytes<=sizeof current && SUCCEEDED(FXCALL(fx,GetValue,w->handle,current,w->bytes)) &&
           !memcmp(current,w->value,w->bytes))continue;
        HR(FXCALL(fx,SetValue,w->handle,w->value,w->bytes));
    }
}

static void source_write_pose(ID3DXEffect *fx,unsigned group)
{
    for(unsigned i=0;i<source_write_count;i++) {
        source_write *w=source_writes+i;if(!w->raw)continue;
        unsigned data[16384];memcpy(data,w->value,w->bytes);
        data[0]=0x3c000000u+((group&63)<<17);
        HR(FXCALL(fx,SetRawValue,w->handle,data,0,w->bytes));
    }
}

static void source_static_controls(ID3DXEffect *fx)
{
    unsigned poisoned[1024],expected[1154],actual[1154],first_pose[1154];
    for(unsigned i=0;i<1024;i++)poisoned[i]=0x3e800000u;
    for(unsigned pose=0;pose<3;pose++)for(unsigned mode=0;mode<4;mode++) {
        source_write_all(fx);cycle(fx,6); /* Clear effect dirty state before the hostile write. */
        HR(IDirect3DDevice9_SetVertexShaderConstantF(device,0,(const float *)poisoned,256));
        HR(IDirect3DDevice9_SetPixelShaderConstantF(device,0,(const float *)poisoned,32));
        if(mode>=2) {
            /* An independent parameter writer must defeat unconditional
             * static reuse and be noticed by the read-before-write filter. */
            source_write *w=source_writes;unsigned char changed[65536];
            memcpy(changed,w->value,w->bytes);changed[0]^=1;
            HR(FXCALL(fx,SetValue,w->handle,changed,w->bytes));
            source_write_filtered(fx,mode==3);
        } else if(!mode)source_write_static(fx);
        source_write_pose(fx,pose*17);
        UINT passes=0;HR(FXCALL(fx,Begin,&passes,6));REQUIRE(passes==1);
        HR(FXCALL(fx,BeginPass,0));HR(FXCALL(fx,CommitChanges));
        unsigned *out=mode?actual:expected;
        HR(IDirect3DDevice9_GetVertexShaderConstantF(device,0,(float *)out,256));
        HR(IDirect3DDevice9_GetPixelShaderConstantF(device,0,(float *)(out+1024),32));
        IDirect3DVertexShader9 *vs=NULL;IDirect3DPixelShader9 *ps=NULL;
        HR(IDirect3DDevice9_GetVertexShader(device,&vs));HR(IDirect3DDevice9_GetPixelShader(device,&ps));
        out[1152]=shader_hash(vs,0);out[1153]=shader_hash(ps,1);
        if(vs)IDirect3DVertexShader9_Release(vs);
        if(ps)IDirect3DPixelShader9_Release(ps);
        HR(FXCALL(fx,EndPass));HR(FXCALL(fx,End));
        if(mode==3)REQUIRE(memcmp(expected,actual,sizeof expected));
        else if(mode)REQUIRE(!memcmp(expected,actual,sizeof expected));
        else if(!pose)memcpy(first_pose,expected,sizeof expected);
        else REQUIRE(memcmp(first_pose,expected,sizeof expected));
    }
    source_write_all(fx);
    puts("SOURCE_STATIC_CONTROLS,3 changed poses; static reuse and read-before-write preserve in-pass constants/shader hashes after external constant poisoning; filter recovers external parameter writes; 3 unconditional-skip controls rejected");
}

static double source_measure(ID3DXEffect *fx,unsigned mode,double *complete)
{
    unsigned repeats=mode?8:32;
    source_write_all(fx);drain();long long before=ticks();
    for(unsigned repeat=0;repeat<repeats;repeat++)for(unsigned group=0;group<850;group++) {
        if(!mode)source_write_all(fx);
        else if(mode!=2) {
            if(mode==1)source_write_static(fx);
            if(mode==4)source_write_filtered(fx,0);
            source_write_pose(fx,group);
        }
        if(mode)cycle(fx,6);
    }
    long long submitted=ticks();drain();
    *complete=(ticks()-before)*1000.0/frequency/repeats;
    return (submitted-before)*1000.0/frequency/repeats;
}

static void source_write_cost(ID3DXEffect *fx,const char *name)
{
    if(!ends_with(name,"defaultw3d.fxo"))return;
    const char *techniques[]={"Default","_CreateShadowMap"};
    for(unsigned t=0;t<2;t++) {
        D3DXHANDLE technique=FXCALL(fx,GetTechniqueByName,techniques[t]);REQUIRE(technique);
        HR(FXCALL(fx,SetTechnique,technique));
        D3DXHANDLE joints=FXCALL(fx,GetParameterByName,NULL,"NumJointsPerVertex");REQUIRE(joints);
        HR(FXCALL(fx,SetInt,joints,1));
        D3DXEFFECT_DESC desc;HR(FXCALL(fx,GetDesc,&desc));source_write_count=source_write_bytes=0;
        for(unsigned p=0;p<desc.Parameters;p++) {
            D3DXHANDLE handle=FXCALL(fx,GetParameter,NULL,p);D3DXPARAMETER_DESC d;
            HR(FXCALL(fx,GetParameterDesc,handle,&d));
            const char *address=source_annotation(fx,handle);
            if((!address && !d.Semantic) || !FXCALL(fx,IsParameterUsed,handle,technique))continue;
            int palette=address && source_family(address)==7 && d.Elements && d.StructMembers;
            source_collect_writes(fx,handle,0,palette);
        }
        REQUIRE(source_write_count && source_write_bytes);
        /* Verify unchanged updates and that a changed input is observable.
         * Generic SetValue timing does not substitute for native setters. */
        source_write_all(fx);
        for(unsigned i=0;i<source_write_count;i++) {
            source_write *w=source_writes+i;unsigned char data[65536];
            HR(FXCALL(fx,GetValue,w->handle,data,w->storage));REQUIRE(!memcmp(data,w->value,w->storage));
        }
        source_write *first=source_writes;unsigned char changed[65536];
        memcpy(changed,first->value,first->bytes);changed[0]^=1;
        HR(FXCALL(fx,SetValue,first->handle,changed,first->bytes));
        HR(FXCALL(fx,GetValue,first->handle,changed,first->bytes));
        REQUIRE(memcmp(changed,first->value,first->bytes));source_write_all(fx);
        source_static_controls(fx);
        printf("SOURCE_WRITES,%s,%u,%u,850 groups per workload; generic numeric setters only; no cache or FPS claim\n",
            techniques[t],source_write_count,source_write_bytes);
        double values[21],savings[21],completed_savings[21],static_savings[21],static_complete[21],filter_savings[21],filter_complete[21];
        for(unsigned round=0;round<25;round++) {
            REQUIRE(bench_no_game());double elapsed[5],complete[5];
            for(unsigned j=0;j<5;j++) {
                unsigned mode=round&1?4-j:j;
                elapsed[mode]=source_measure(fx,mode,&complete[mode]);
            }
            if(round>=4) {
                unsigned r=round-4;values[r]=elapsed[0];savings[r]=elapsed[1]-elapsed[2];
                completed_savings[r]=complete[1]-complete[2];
                static_savings[r]=elapsed[1]-elapsed[3];static_complete[r]=complete[1]-complete[3];
                filter_savings[r]=elapsed[1]-elapsed[4];filter_complete[r]=complete[1]-complete[4];
                printf("SOURCE_WRITE_TIMING,%s,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",techniques[t],r,
                    elapsed[0],elapsed[1],elapsed[2],savings[r],complete[1],complete[2],completed_savings[r],
                    elapsed[3],complete[3],static_savings[r],static_complete[r],
                    elapsed[4],complete[4],filter_savings[r],filter_complete[r]);
            }
        }
        qsort(values,21,sizeof *values,cmp);
        qsort(savings,21,sizeof *savings,cmp);qsort(completed_savings,21,sizeof *completed_savings,cmp);
        qsort(static_savings,21,sizeof *static_savings,cmp);qsort(static_complete,21,sizeof *static_complete,cmp);
        qsort(filter_savings,21,sizeof *filter_savings,cmp);qsort(filter_complete,21,sizeof *filter_complete,cmp);
        printf("SOURCE_WRITE_SUMMARY,%s,%.6f,%.6f,%.6f\n",techniques[t],values[10],values[2],values[18]);
        printf("SOURCE_LIFECYCLE_DELTA,%s,%.6f,%.6f,%.6f,completed median %.6f; zero-write control is not a runtime candidate\n",
            techniques[t],savings[10],savings[2],savings[18],completed_savings[10]);
        printf("SOURCE_STATIC_DELTA,%s,%.6f,%.6f,%.6f,completed median %.6f; changed pose retained, static invalidation/lookup unmodeled\n",
            techniques[t],static_savings[10],static_savings[2],static_savings[18],static_complete[10]);
        printf("SOURCE_FILTER_DELTA,%s,%.6f,%.6f,%.6f,completed median %.6f; live GetValue/compare cost included; generic numeric fields only\n",
            techniques[t],filter_savings[10],filter_savings[2],filter_savings[18],filter_complete[10]);
        for(unsigned i=0;i<source_write_count;i++)free(source_writes[i].value);
    }
}
#endif
