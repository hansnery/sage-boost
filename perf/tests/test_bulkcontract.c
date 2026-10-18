#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bulkcontract.h"
#include "bulkcontract_table.h"
#include "queueinsert.h"
#include "queuesort.h"
#include "bench_guard.h"
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"bulk contract refused at line %u: %s\n",__LINE__,#x); exit(1); } } while (0)
static unsigned fnv(const unsigned char *p,unsigned n)
{ unsigned h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h; }
#include "native_contract_image.h"

static unsigned refusals;

static void region_controls(void)
{
    unsigned char *base=VirtualAlloc(NULL,0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);REQUIRE(base);
    IMAGE_DOS_HEADER *dos=(void *)base;dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=128;
    IMAGE_NT_HEADERS32 *nt=(void *)(base+128);nt->Signature=IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine=IMAGE_FILE_MACHINE_I386;nt->FileHeader.NumberOfSections=2;
    nt->FileHeader.SizeOfOptionalHeader=sizeof nt->OptionalHeader;
    nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt->OptionalHeader.SizeOfHeaders=0x1000;nt->OptionalHeader.SizeOfImage=0x4000;
    IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);
    memcpy(sections[0].Name,".text",5);sections[0].VirtualAddress=0x1000;
    sections[0].Misc.VirtualSize=0x1000;sections[0].Characteristics=IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_EXECUTE;
    memcpy(sections[1].Name,".data",5);sections[1].VirtualAddress=0x3000;
    sections[1].Misc.VirtualSize=0x1000;sections[1].Characteristics=IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_WRITE;
    unsigned char normalized[]={0xb8,0,0,0,0,0xc3};memcpy(base+0x1000,normalized,sizeof normalized);
    *(unsigned *)(base+0x1001)=(unsigned)(base+0x3ff4);
    pf_contract_body body={6,0,1,fnv(normalized,6),pf_crc32(normalized,6)};
    pf_contract_patch patch={1,1,1,4};
    pf_contract_root root={0,"B8 ?? ?? ?? ?? C3"};
    unsigned spans[]={6,16};unsigned char code[]={1,0},writable[]={0,1};
    pf_native_contract contract={1,2,1,1,&body,&patch,code,&root,spans,writable};
    pf_image scan;REQUIRE(pf_image_open(&scan,base));unsigned found[2];
    REQUIRE(!pf_contract_find(&scan,&contract,found) && found[0]==0x1000 && found[1]==0x3ff0);
    /* Region base remains valid; only its final byte falls outside data. */
    sections[1].Misc.VirtualSize--;
    found[0]=found[1]=0xa5a5a5a5;REQUIRE(pf_contract_find(&scan,&contract,found));
    REQUIRE(found[0]==0xa5a5a5a5 && found[1]==0xa5a5a5a5);sections[1].Misc.VirtualSize++;
    sections[1].Characteristics&=~IMAGE_SCN_MEM_WRITE;REQUIRE(pf_contract_find(&scan,&contract,found));
    sections[1].Characteristics|=IMAGE_SCN_MEM_WRITE;
    patch.addend=16;REQUIRE(pf_contract_find(&scan,&contract,found));patch.addend=4;
    spans[1]=0;REQUIRE(pf_contract_find(&scan,&contract,found));spans[1]=16;
    REQUIRE(!pf_contract_find(&scan,&contract,found));REQUIRE(VirtualFree(base,0,MEM_RELEASE));
    printf("CONTRACT_REGIONS,valid_addend,1,refusals,4\n");
}
static void refused(ContractImage *image,const pf_image *scan)
{
    unsigned hash=fnv(image->base,image->size);
    pf_bulk_sites sites;memset(&sites,0xa5,sizeof sites);
    const char *why=pf_bulk_find(scan,&sites);
    if (!why)fprintf(stderr,"unexpected success on refusal %u\n",refusals);
    REQUIRE(why);
    const unsigned char *p=(const void *)&sites;
    for(unsigned i=0;i<sizeof sites;i++)REQUIRE(!p[i]);
    REQUIRE(hash==fnv(image->base,image->size));
    refusals++;
}

static void positive(const pf_image *scan,const unsigned *expected)
{
    unsigned found[BK_SYMBOL_COUNT];pf_bulk_sites sites;
    const char *why=pf_contract_find(scan,&bk_contract,found);
    if(why)fprintf(stderr,"unmodified graph: %s\n",why);
    REQUIRE(!why && !memcmp(found,expected,sizeof found));
    why=pf_bulk_find(scan,&sites);
    if(why)fprintf(stderr,"unmodified bulk finder: %s\n",why);
    REQUIRE(!why);
#define AT(field,key) REQUIRE((void *)sites.field==scan->base+expected[BK_##key])
    AT(group,GROUP);AT(mesh,MESH);AT(triangles,TRIANGLES);AT(draw,DRAW);
    AT(begin_callback,BEGIN_CALLBACK);AT(end_callback,END_CALLBACK);
    AT(fx_get_effect,FX_GET_EFFECT);AT(fx_begin,FX_BEGIN);AT(fx_begin_pass,FX_BEGIN_PASS);
    AT(fx_dynamic,FX_DYNAMIC);AT(fx_end_pass,FX_END_PASS);AT(fx_end,FX_END);
    AT(fx_bind,FX_BIND);AT(fx_apply_sources,FX_APPLY_SOURCES);
#undef AT
    REQUIRE(sites.hook==sites.group+0x14f && sites.fallback==sites.group+0x155 &&
            sites.resume==sites.group+0x2b7);
}

static void controls(ContractImage *image,ContractManifest *manifest,pf_image *scan,const unsigned *expected)
{
    unsigned bodies=0;
    for(unsigned i=0;i<manifest->count;i++) {
        unsigned size=manifest->size[i],off;
        for(off=0;off<size;off++) {
            unsigned patched=0;
            for(unsigned j=0;j<manifest->patch_count;j++) {
                ContractPatch *p=manifest->patches+j;
                if(p->body==i && off>=p->field && off<p->field+4)patched=1;
            }
            if(!patched)break;
        }
        if(off==size)continue; /* Every jump-table byte is a checked pointer. */
        image->base[manifest->rva[i]+off]^=1;refused(image,scan);
        image->base[manifest->rva[i]+off]^=1;bodies++;
    }
    for(unsigned i=0;i<manifest->patch_count;i++) {
        ContractPatch *p=manifest->patches+i;
        unsigned *field=(void *)(image->base+p->rva+p->field),saved=*field;
        *field=p->absolute?(unsigned)image->base:0u-(p->rva+p->field+4);
        refused(image,scan);*field=saved;
    }
    unsigned addends=0;
    for(unsigned i=0;i<bk_contract.patch_count;i++) {
        const pf_contract_patch *p=bk_patches+i;
        if(!p->addend)continue;
        ContractPatch *where=manifest->patches+i;
        unsigned *field=(void *)(image->base+where->rva+where->field),saved=*field;
        /* Wrong interior offset still targets the same readable/code section. */
        *field=saved+1;refused(image,scan);*field=saved;addends++;
    }
    for(unsigned i=0;i<bk_contract.root_count;i++) {
        unsigned rva=expected[bk_roots[i].symbol];
        pf_pattern pattern;REQUIRE(pf_parse_pattern(bk_roots[i].signature,&pattern));
        unsigned char *copy=image->base+scan->text_rva+scan->text_size-256,saved[256];
        memcpy(saved,copy,pattern.len);memcpy(copy,image->base+rva,pattern.len);
        refused(image,scan);memcpy(copy,saved,pattern.len);
    }
    IMAGE_NT_HEADERS32 *nt=(void *)(image->base+((IMAGE_DOS_HEADER *)image->base)->e_lfanew);
    IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);
    unsigned writable_sections=0;
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER *s=sections+i;unsigned used=0;
        for(unsigned j=0;j<BK_SYMBOL_COUNT;j++)if(bk_writable[j] && expected[j]>=s->VirtualAddress &&
            expected[j]<s->VirtualAddress+s->Misc.VirtualSize)used=1;
        if(!used)continue;
        DWORD old=s->Characteristics;s->Characteristics&=~IMAGE_SCN_MEM_WRITE;
        refused(image,scan);s->Characteristics=old;writable_sections++;
    }
    /* Loaded IAT identities: wrong live export and missing export both fail. */
    unsigned imports=0;
    for(unsigned i=0;i<sizeof bk_imports/sizeof bk_imports[0];i++) {
        unsigned *iat=(void *)(image->base+expected[bk_imports[i].symbol]),old=*iat;
        *iat=0;refused(image,scan);
        *iat=*(unsigned *)(image->base+expected[bk_imports[(i+1)%4].symbol]);
        refused(image,scan);*iat=old;imports++;
    }
    unsigned size=nt->OptionalHeader.SizeOfImage;
    nt->OptionalHeader.SizeOfImage=scan->text_rva+scan->text_size-1;
    refused(image,scan);nt->OptionalHeader.SizeOfImage=size;
    positive(scan,expected);
    printf("BULK_CONTRACT,regions,%u,symbols,%u,body_controls,%u,dependency_controls,%u,addends,%u,roots,%u,writable_sections,%u,imports,%u,refusals,%u\n",
           manifest->count,BK_SYMBOL_COUNT,bodies,manifest->patch_count,addends,bk_contract.root_count,writable_sections,imports,refusals);
}

int main(int argc,char **argv)
{
    REQUIRE(argc==7 && bench_no_game());region_controls();
    ContractImage image;ContractManifest manifest,insert,sort;pf_image scan;unsigned expected[BK_SYMBOL_COUNT];
    contract_map(&image,argv[1]);contract_rebase(&image,&manifest,argv[2]);
    contract_rebase(&image,&insert,argv[5]);contract_rebase(&image,&sort,argv[6]);
    REQUIRE(pf_image_open(&scan,image.base));
    FILE *f=fopen(argv[3],"r");REQUIRE(f);
    for(unsigned i=0;i<BK_SYMBOL_COUNT;i++)REQUIRE(fscanf(f,"%u",expected+i)==1);
    char extra;REQUIRE(fscanf(f," %c",&extra)==EOF);fclose(f);
    /* Model loader binding, never call the native executable entry. */
    HMODULE crt=LoadLibraryExA(argv[4],NULL,LOAD_WITH_ALTERED_SEARCH_PATH);REQUIRE(crt);
    for(unsigned i=0;i<sizeof bk_imports/sizeof bk_imports[0];i++) {
        FARPROC entry=GetProcAddress(crt,bk_imports[i].name);REQUIRE(entry);
        *(void **)(image.base+expected[bk_imports[i].symbol])=(void *)entry;
    }
    positive(&scan,expected);controls(&image,&manifest,&scan,expected);
    contract_execute(&image);positive(&scan,expected);
    const char *why;
    REQUIRE(pf_queue_insert_install(&scan,&why));positive(&scan,expected);
    REQUIRE(pf_queue_sort_install(&scan,&why));positive(&scan,expected);
    printf("BULK_QUEUE_COEXISTENCE,insertion,1,sorting,1\n");
    REQUIRE(VirtualFree(image.base,0,MEM_RELEASE));
    REQUIRE(FreeLibrary(crt));REQUIRE(bench_no_game());return 0;
}
