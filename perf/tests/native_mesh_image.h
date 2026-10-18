/* Shared offline loader: verified local bodies exist only in process memory.
 * Caller supplies REQUIRE and symbols 7..30. No native image bytes persist. */
#ifndef NATIVE_MESH_IMAGE_H
#define NATIVE_MESH_IMAGE_H
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned fnv(const unsigned char *p,unsigned n)
{
    unsigned h=2166136261u; while(n--) h=(h^*p++)*16777619u; return h;
}
typedef struct {unsigned body,offset,kind,symbol,add;} Patch;
static void native_mesh_load(void **symbols,const char *image,const char *manifest)
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
    f=fopen(manifest,"r"); REQUIRE(f); Patch patches[256]; unsigned np=0;
    const unsigned sizes[]={2482,33,452,1,30,18,16};
    for(unsigned i=0;i<7;i++) {
        unsigned rva,size,hash,n,offset=0,matches=0;
        REQUIRE(fscanf(f,"%u %u %u %u",&rva,&size,&hash,&n)==4 && size==sizes[i] && n<=160);
        for(unsigned j=0;j<nt->FileHeader.NumberOfSections;j++) {
            IMAGE_SECTION_HEADER *s=&sections[j];
            if(rva>=s->VirtualAddress && rva-s->VirtualAddress<=s->SizeOfRawData && size<=s->SizeOfRawData-(rva-s->VirtualAddress) && (s->Characteristics&IMAGE_SCN_MEM_EXECUTE)) {
                REQUIRE(s->PointerToRawData<=(unsigned)length && s->SizeOfRawData<=(unsigned)length-s->PointerToRawData);
                offset=s->PointerToRawData+rva-s->VirtualAddress; matches++;
            }
        }
        REQUIRE(matches==1 && fnv(raw+offset,size)==hash);
        symbols[i]=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); REQUIRE(symbols[i]);
        memcpy(symbols[i],raw+offset,size);
        for(unsigned j=0;j<n;j++) {
            REQUIRE(np<256); Patch *p=&patches[np++]; p->body=i;
            REQUIRE(fscanf(f,"%u %u %u %u",&p->offset,&p->kind,&p->symbol,&p->add)==4 && p->offset<=size-4 && p->kind<=1 && p->symbol<32 && p->add<0x290);
        }
    }
    symbols[31]=(char*)symbols[2]+452;
    for(unsigned i=0;i<8;i++) {
        unsigned offset; REQUIRE(fscanf(f,"%u",&offset)==1 && offset<452);
        ((void**)symbols[31])[i]=(char*)symbols[2]+offset;
    }
    {char extra; REQUIRE(fscanf(f," %c",&extra)==EOF);} fclose(f); free(raw);
    for(unsigned i=0;i<np;i++) {
        Patch p=patches[i]; unsigned char *field=(unsigned char*)symbols[p.body]+p.offset;
        *(unsigned*)field=(unsigned)symbols[p.symbol]+p.add-(p.kind?0:(unsigned)(field+4));
    }
    for(unsigned i=0;i<7;i++) {
        DWORD old; REQUIRE(VirtualProtect(symbols[i],4096,PAGE_EXECUTE_READ,&old));
        REQUIRE(FlushInstructionCache(GetCurrentProcess(),symbols[i],4096));
    }
}
#endif
