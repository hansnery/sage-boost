/* Offline verified native group loader. Caller supplies REQUIRE and
 * symbols 9..33; no EA executable bytes are saved. */
#ifndef NATIVE_GROUP_IMAGE_H
#define NATIVE_GROUP_IMAGE_H
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef PF_NATIVE_BODY_ALLOC
#define PF_NATIVE_BODY_ALLOC(image_size,rva,size) VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)
#endif
static unsigned ng_fnv(const unsigned char *p,unsigned n)
{
    unsigned h=2166136261u; while(n--) h=(h^*p++)*16777619u; return h;
}
typedef struct { unsigned body,offset,kind,symbol; } NativeGroupPatch;
static void native_group_load(void **symbols,const char *image,const char *manifest,
                              void *bridge,void **hooked_out,void **resume_out,void **fallback_out)
{
    FILE *f=fopen(image,"rb"); REQUIRE(f); REQUIRE(!fseek(f,0,SEEK_END)); long length=ftell(f); rewind(f);
    REQUIRE(length>4096 && length<128*1024*1024); unsigned char *raw=malloc(length); REQUIRE(raw);
    REQUIRE(fread(raw,1,length,f)==(size_t)length); fclose(f);
    REQUIRE(((IMAGE_DOS_HEADER*)raw)->e_magic==IMAGE_DOS_SIGNATURE);
    unsigned h=((IMAGE_DOS_HEADER*)raw)->e_lfanew; REQUIRE(h<=(unsigned)length-sizeof(IMAGE_NT_HEADERS32));
    IMAGE_NT_HEADERS32 *nt=(void*)(raw+h);
    REQUIRE(nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.Machine==IMAGE_FILE_MACHINE_I386 && nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC);
    IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);
    REQUIRE((unsigned char*)(sections+nt->FileHeader.NumberOfSections)<=raw+length);
    f=fopen(manifest,"r"); REQUIRE(f); NativeGroupPatch patches[100]; unsigned np=0;
    const unsigned sizes[]={819,31,14,83,41,167,5,128,12};
    for(unsigned i=0;i<9;i++) {
        unsigned rva,size,hash,n,offset=0,matches=0;
        REQUIRE(fscanf(f,"%u %u %u %u",&rva,&size,&hash,&n)==4 && size==sizes[i] && n<=40);
        for(unsigned j=0;j<nt->FileHeader.NumberOfSections;j++) {
            IMAGE_SECTION_HEADER *s=&sections[j];
            if(rva>=s->VirtualAddress && rva-s->VirtualAddress<=s->SizeOfRawData && size<=s->SizeOfRawData-(rva-s->VirtualAddress) && (s->Characteristics&IMAGE_SCN_MEM_EXECUTE)) {
                REQUIRE(s->PointerToRawData<=(unsigned)length && s->SizeOfRawData<=(unsigned)length-s->PointerToRawData);
                offset=s->PointerToRawData+rva-s->VirtualAddress; matches++;
            }
        }
        REQUIRE(matches==1 && ng_fnv(raw+offset,size)==hash);
        symbols[i]=PF_NATIVE_BODY_ALLOC(nt->OptionalHeader.SizeOfImage,rva,size); REQUIRE(symbols[i]);
        memcpy(symbols[i],raw+offset,size);
        for(unsigned j=0;j<n;j++) {
            REQUIRE(np<100); NativeGroupPatch *p=&patches[np++]; p->body=i;
            REQUIRE(fscanf(f,"%u %u %u",&p->offset,&p->kind,&p->symbol)==3 && p->offset<=size-4 && p->kind<=1 && p->symbol<34);
        }
    }
    { char extra; REQUIRE(fscanf(f," %c",&extra)==EOF); } fclose(f); free(raw);
    for(unsigned i=0;i<np;i++) {
        NativeGroupPatch p=patches[i]; unsigned char *field=(unsigned char*)symbols[p.body]+p.offset;
        *(unsigned*)field=(unsigned)symbols[p.symbol]-(p.kind?0:(unsigned)(field+4));
    }
    unsigned char *group_original=symbols[0];
    unsigned char *group_hooked=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); REQUIRE(group_hooked);
    memcpy(group_hooked,group_original,819);
    for(unsigned i=0;i<np;i++) if(!patches[i].body && !patches[i].kind) {
        NativeGroupPatch p=patches[i]; unsigned char *field=group_hooked+p.offset;
        *(unsigned*)field=(unsigned)symbols[p.symbol]-(unsigned)(field+4);
    }
    group_hooked[0x14F]=0xE9; *(unsigned*)(group_hooked+0x150)=(unsigned)bridge-(unsigned)(group_hooked+0x154);
    group_hooked[0x154]=0xCC;
    *hooked_out=group_hooked; *resume_out=group_hooked+0x2B7; *fallback_out=group_hooked+0x155;
    for(unsigned i=0;i<10;i++) {
        void *body=i==9?group_hooked:symbols[i]; DWORD old;
        REQUIRE(VirtualProtect(body,4096,PAGE_EXECUTE_READ,&old));
        REQUIRE(FlushInstructionCache(GetCurrentProcess(),body,4096));
    }
}
#endif
