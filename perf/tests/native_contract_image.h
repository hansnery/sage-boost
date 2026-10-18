/* Full private mapping for runtime-hook tests. Never resolve imports or call
 * the executable entry. Manifests contain only numeric hashes/relocations.
 * Shared absolute fields are rebased once across overlapping contracts. */
typedef struct {
    unsigned char *base;
    unsigned size, preferred, relocated[512], relocated_count;
} ContractImage;
typedef struct { unsigned rva, field, absolute, symbol, body; } ContractPatch;
typedef struct {
    unsigned count, rva[64], size[64], patch_count;
    ContractPatch patches[512];
} ContractManifest;

static void contract_map(ContractImage *image,const char *path)
{
    memset(image,0,sizeof *image);
    FILE *f=fopen(path,"rb");REQUIRE(f && !fseek(f,0,SEEK_END));
    long length=ftell(f);rewind(f);REQUIRE(length>4096 && length<128*1024*1024);
    unsigned char *raw=malloc(length);REQUIRE(raw && fread(raw,1,length,f)==(size_t)length);fclose(f);
    unsigned pe=((IMAGE_DOS_HEADER *)raw)->e_lfanew;
    REQUIRE(((IMAGE_DOS_HEADER *)raw)->e_magic==IMAGE_DOS_SIGNATURE && pe<=(unsigned)length-sizeof(IMAGE_NT_HEADERS32));
    IMAGE_NT_HEADERS32 *nt=(void *)(raw+pe);
    REQUIRE(nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.Machine==IMAGE_FILE_MACHINE_I386 &&
            nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC);
    image->size=nt->OptionalHeader.SizeOfImage;image->preferred=nt->OptionalHeader.ImageBase;
    REQUIRE(image->size>=4096 && image->size<128*1024*1024 && nt->OptionalHeader.SizeOfHeaders<=4096);
    image->base=VirtualAlloc(NULL,image->size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);REQUIRE(image->base);
    REQUIRE((unsigned)image->base!=image->preferred);
    memcpy(image->base,raw,nt->OptionalHeader.SizeOfHeaders);
    IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);
    REQUIRE((unsigned char *)(sections+nt->FileHeader.NumberOfSections)<=raw+4096);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER *s=sections+i;
        REQUIRE(s->VirtualAddress<=image->size && s->SizeOfRawData<=image->size-s->VirtualAddress);
        REQUIRE(s->PointerToRawData<=(unsigned)length && s->SizeOfRawData<=(unsigned)length-s->PointerToRawData);
        memcpy(image->base+s->VirtualAddress,raw+s->PointerToRawData,s->SizeOfRawData);
    }
    free(raw);
}

static int contract_relocated(const ContractImage *image,unsigned field)
{
    for(unsigned i=0;i<image->relocated_count;i++)if(image->relocated[i]==field)return 1;
    return 0;
}

static void contract_rebase(ContractImage *image,ContractManifest *manifest,const char *path)
{
    memset(manifest,0,sizeof *manifest);
    FILE *f=fopen(path,"r");REQUIRE(f);
    REQUIRE(fscanf(f,"%u",&manifest->count)==1 && manifest->count && manifest->count<=64);
    unsigned delta=(unsigned)image->base-image->preferred;
    for(unsigned i=0;i<manifest->count;i++) {
        unsigned rva,size,hash,count;unsigned char raw[4096];
        REQUIRE(fscanf(f,"%u %u %u %u",&rva,&size,&hash,&count)==4 && size<=sizeof raw && size>=1);
        REQUIRE(rva<image->size && size<=image->size-rva && count<=512-manifest->patch_count);
        manifest->rva[i]=rva;manifest->size[i]=size;
        memcpy(raw,image->base+rva,size);
        unsigned first=manifest->patch_count;
        for(unsigned j=0;j<count;j++) {
            ContractPatch *p=manifest->patches+manifest->patch_count++;
            p->rva=rva;p->body=i;
            REQUIRE(fscanf(f,"%u %u %u",&p->field,&p->absolute,&p->symbol)==3 && size>=4 && p->field<=size-4 && p->absolute<=1);
            if(p->absolute && contract_relocated(image,rva+p->field))*(unsigned *)(raw+p->field)-=delta;
        }
        REQUIRE(fnv(raw,size)==hash);
        for(unsigned j=first;j<manifest->patch_count;j++) {
            ContractPatch p=manifest->patches[j];
            if(p.absolute && !contract_relocated(image,rva+p.field)) {
                REQUIRE(image->relocated_count<512);
                *(unsigned *)(image->base+rva+p.field)+=delta;
                image->relocated[image->relocated_count++]=rva+p.field;
            }
        }
    }
    char extra;REQUIRE(fscanf(f," %c",&extra)==EOF);fclose(f);
}

static void contract_execute(ContractImage *image)
{
    DWORD old;REQUIRE(VirtualProtect(image->base,image->size,PAGE_EXECUTE_READ,&old));
    REQUIRE(FlushInstructionCache(GetCurrentProcess(),image->base,image->size));
}
