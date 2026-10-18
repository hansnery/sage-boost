/* Execute the installed entry in a private image, never initialize the game.
 * Reuse the independent record/reference/ABI oracle against an unhooked copy.
 * Only controlled allocation/free callbacks may leave the native closure. */
#define main insertion_screen_main
#include "bench_native_insert.c"
#undef main

static unsigned char *mapped;
static unsigned mapped_size, native_rvas[COUNT], heap_rvas[2], refusal_count;
static Patch mapped_patches[512];
static unsigned mapped_patch_count;

static void map_private(const char *path, const char *manifest)
{
    FILE *f=fopen(path,"rb");REQUIRE(f && !fseek(f,0,SEEK_END));
    long length=ftell(f);rewind(f);REQUIRE(length>4096 && length<128*1024*1024);
    unsigned char *raw=malloc(length);REQUIRE(raw && fread(raw,1,length,f)==(size_t)length);fclose(f);
    IMAGE_NT_HEADERS32 *nt=(void *)(raw+((IMAGE_DOS_HEADER *)raw)->e_lfanew);
    mapped_size=nt->OptionalHeader.SizeOfImage;
    mapped=VirtualAlloc(NULL,mapped_size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);REQUIRE(mapped);
    REQUIRE(nt->OptionalHeader.SizeOfHeaders<=4096);
    memcpy(mapped,raw,nt->OptionalHeader.SizeOfHeaders);
    IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER *s=sections+i;
        REQUIRE(s->VirtualAddress<=mapped_size && s->SizeOfRawData<=mapped_size-s->VirtualAddress);
        REQUIRE(s->PointerToRawData<=(unsigned)length && s->SizeOfRawData<=(unsigned)length-s->PointerToRawData);
        memcpy(mapped+s->VirtualAddress,raw+s->PointerToRawData,s->SizeOfRawData);
    }
    unsigned preferred=nt->OptionalHeader.ImageBase;free(raw);
    f=fopen(manifest,"r");REQUIRE(f);unsigned count;
    REQUIRE(fscanf(f,"%u",&count)==1 && count==COUNT);
    for(unsigned i=0;i<COUNT;i++) {
        unsigned size,hash,n;
        REQUIRE(fscanf(f,"%u %u %u %u",native_rvas+i,&size,&hash,&n)==4);
        REQUIRE(native_rvas[i]<mapped_size && size<=mapped_size-native_rvas[i]);
        REQUIRE(fnv(mapped+native_rvas[i],size)==hash);
        for(unsigned j=0;j<n;j++) {
            REQUIRE(mapped_patch_count<512);
            Patch *p=mapped_patches+mapped_patch_count++;p->body=i;
            REQUIRE(fscanf(f,"%u %u %u",&p->field,&p->kind,&p->symbol)==3 && p->field<=size-4);
            unsigned char *field=mapped+native_rvas[i]+p->field;
            if(p->kind) *(unsigned *)field += (unsigned)mapped-preferred;
            if(p->symbol==COUNT+1 || p->symbol==COUNT+2) {
                unsigned dest=*(unsigned *)field+(p->kind?-(unsigned)mapped:native_rvas[i]+p->field+4);
                unsigned slot=p->symbol-COUNT-1;
                REQUIRE(!heap_rvas[slot] || heap_rvas[slot]==dest);heap_rvas[slot]=dest;
            }
        }
    }
    fclose(f);
}

static void refused(pf_image *image, const char *expected)
{
    unsigned entry=123;
    unsigned before=fnv(mapped,mapped_size);
    const char *why=pf_queue_insert_find(image,&entry);
    if(!why || (expected && strcmp(why,expected))) {
        fprintf(stderr,"finder expected %s, got %s\n",expected?expected:"refusal",why?why:"success");
        REQUIRE(0);
    }
    REQUIRE(entry==0);
    REQUIRE(!pf_queue_insert_install(image,&why) && why);
    REQUIRE(before==fnv(mapped,mapped_size));refusal_count++;
}

static void finder_controls(pf_image *image)
{
    unsigned entry;
    const char *why=pf_queue_insert_find(image,&entry);
    if(why)fprintf(stderr,"unmodified finder: %s\n",why);
    REQUIRE(!why && entry==native_rvas[0]);
    /* Mutations outside the short anchor exercise complete-body hashes. */
    const unsigned offsets[][2]={{0,0x60},{10,0x25},{29,0x15},{31,8},{32,20}};
    for(unsigned i=0;i<sizeof offsets/sizeof offsets[0];i++) {
        unsigned off=offsets[i][1];
        for(unsigned j=0;j<mapped_patch_count;j++) {
            Patch p=mapped_patches[j];
            if(p.body==offsets[i][0] && off>=p.field && off<p.field+4)off=p.field+4;
        }
        unsigned char *p=mapped+native_rvas[offsets[i][0]]+off;
        *p^=1;refused(image,"complete body differs");*p^=1;
    }
    /* Every encoded dependency is tested outside its expected section role. */
    for(unsigned i=0;i<mapped_patch_count;i++) {
        Patch p=mapped_patches[i];unsigned *field=(void *)(mapped+native_rvas[p.body]+p.field),old=*field;
        *field=p.kind?(unsigned)mapped:-(native_rvas[p.body]+p.field+4);
        refused(image,"dependency section role");*field=old;
    }
    unsigned char *copy=mapped+image->text_rva+image->text_size-48, saved[48];
    memcpy(saved,copy,48);memcpy(copy,mapped+entry,48);
    refused(image,"ambiguous entry signature");memcpy(copy,saved,48);
    mapped[entry]^=1;refused(image,"entry signature missing");mapped[entry]^=1;
    IMAGE_NT_HEADERS32 *nt=(void *)(mapped+((IMAGE_DOS_HEADER *)mapped)->e_lfanew);
    IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER *s=sections+i;
        if(s->VirtualAddress==image->text_rva) {
            s->Characteristics^=IMAGE_SCN_MEM_EXECUTE;refused(image,"invalid text range");
            s->Characteristics^=IMAGE_SCN_MEM_EXECUTE;
        }
    }
    unsigned size=nt->OptionalHeader.SizeOfImage;
    nt->OptionalHeader.SizeOfImage=image->text_rva+image->text_size-1;
    refused(image,"invalid text range");nt->OptionalHeader.SizeOfImage=size;
    /* A body just beyond the section end must refuse before reading it. */
    Patch p=mapped_patches[1];
    unsigned *field=(void *)(mapped+native_rvas[p.body]+p.field),old=*field;
    unsigned end=image->text_rva+image->text_size-1;
    *field=p.kind?(unsigned)mapped+end:end-(native_rvas[p.body]+p.field+4);
    refused(image,"body outside executable section");*field=old;
    /* Repeated calls must resolve to one helper even inside valid code. */
    unsigned identity_checks=0;
    for(unsigned i=0;i<mapped_patch_count && !identity_checks;i++) {
        Patch later=mapped_patches[i];
        if(later.symbol>=COUNT || later.body==0)continue;
        for(unsigned j=0;j<i;j++) {
            Patch earlier=mapped_patches[j];
            if(earlier.symbol!=later.symbol || earlier.body>=later.body)continue;
            unsigned *changed=(void *)(mapped+native_rvas[later.body]+later.field),save=*changed;
            *changed=later.kind?(unsigned)mapped+native_rvas[0]:
                                native_rvas[0]-(native_rvas[later.body]+later.field+4);
            refused(image,"dependency identity differs");*changed=save;
            identity_checks++;break;
        }
    }
    REQUIRE(identity_checks==1);
    REQUIRE(!pf_queue_insert_find(image,&entry));
}

static void redirect_heap(unsigned index, void *callback)
{
    unsigned rva=heap_rvas[index];REQUIRE(rva && rva<mapped_size-5);
    mapped[rva]=0xe9;*(unsigned *)(mapped+rva+1)=(unsigned)callback-(unsigned)(mapped+rva+5);
}

int main(int argc,char **argv)
{
    REQUIRE(argc==3 && bench_no_game());
    load(argv[1],argv[2]);map_private(argv[1],argv[2]);
    pf_image image;REQUIRE(pf_image_open(&image,mapped));finder_controls(&image);
    redirect_heap(0,heap_allocate);redirect_heap(1,heap_release);
    DWORD old;REQUIRE(VirtualProtect(mapped,mapped_size,PAGE_EXECUTE_READ,&old));
    REQUIRE(FlushInstructionCache(GetCurrentProcess(),mapped,mapped_size));
    const char *why;REQUIRE(pf_queue_insert_install(&image,&why));
    REQUIRE(mapped[native_rvas[0]]==0xe9);
    candidate=(Add)(mapped+native_rvas[0]);
    const unsigned sizes[]={0,1,2,3,4,17,128,7680};
    for(unsigned n=0;n<sizeof sizes/sizeof sizes[0];n++)for(unsigned capacity=0;capacity<3;capacity++)
        for(unsigned count=0;count<=7;count++)for(unsigned pattern=0;pattern<3;pattern++)
            check(sizes[n],capacity==2?CAPACITY:capacity,count,pattern);
    edge_controls();
    REQUIRE(!pf_queue_insert_install(&image,&why)); /* Never double-hook. */
    printf("RUNTIME_INSERT,%u,REFUSALS,%u,ABI,%u\n",cases,refusal_count,insert_abi_errors);
    REQUIRE(VirtualFree(mapped,0,MEM_RELEASE) && bench_no_game());return 0;
}
