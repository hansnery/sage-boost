/* Four fully verified local event bodies only. No PE entry, scene, game
 * loop or disk image patch. Use tools/test_event_contract.py --execute. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(c) do { if(!(c)) { fprintf(stderr,"native event refused line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static void *bodies[4],*begin_hook,*end_hook;
static FARPROC copy_import;
static const char prefix[]="SceneAnalyst";
static unsigned begins,ends,color_seen,cases;
static unsigned short text_seen[256];
volatile unsigned event_abi_errors;
unsigned native_event_call(void *,void *,unsigned,unsigned,unsigned,unsigned);
__asm__(
".text\n.globl _native_event_call\n_native_event_call:\n"
"pushl %ebp\nmovl %esp,%ebp\npushl %ebx\npushl %esi\npushl %edi\nsubl $4,%esp\n"
"movl %esp,-16(%ebp)\nmovl 8(%ebp),%eax\nmovl 12(%ebp),%ecx\n"
"cmpl $3,28(%ebp)\njne 1f\npushl 24(%ebp)\npushl 20(%ebp)\npushl 16(%ebp)\n1:\n"
"movl $0x13579bdf,%ebx\nmovl $0x2468ace0,%esi\nmovl $0x76543210,%edi\ncall *%eax\n"
"cmpl -16(%ebp),%esp\nje 2f\nincl _event_abi_errors\n2:\n"
"cmpl $0x13579bdf,%ebx\nje 3f\nincl _event_abi_errors\n3:\n"
"cmpl $0x2468ace0,%esi\nje 4f\nincl _event_abi_errors\n4:\n"
"cmpl $0x76543210,%edi\nje 5f\nincl _event_abi_errors\n5:\n"
"leal -12(%ebp),%esp\npopl %edi\npopl %esi\npopl %ebx\npopl %ebp\nret\n");
static void __stdcall callback_begin(void *color,const unsigned short *text)
{
    begins++; color_seen=(unsigned)(UINT_PTR)color;
    for(unsigned i=0;i<256;i++) {
        text_seen[i]=text[i];
        if(!text[i]) break;
    }
}
static int callback_end(void) { ends++; return 0x12345678; }
static unsigned fnv(const unsigned char *p,unsigned n)
{
    unsigned h=2166136261u;
    while(n--) h=(h^*p++)*16777619u;
    return h;
}
static void check(const char *label,const char *group,unsigned flags)
{
    unsigned char guarded[336],expected[320];
    memset(guarded,0xA5,sizeof guarded); memset(expected,0xA5,sizeof expected);
    unsigned char *object=guarded+8;
    begins=ends=color_seen=0; memset(text_seen,0x5A,sizeof text_seen);
    begin_hook=flags&1?(void*)callback_begin:NULL;
    end_hook=flags&2?(void*)callback_end:NULL;
    if(label) {
        size_t n=strlen(label); if(n>255) n=255;
        memset(expected,0,256); memcpy(expected,label,n);
        if(group) {
            memcpy(expected+256,prefix,sizeof prefix);
            n=strlen(group); if(n>51) n=51;
            memset(expected+268,0,52); memcpy(expected+268,group,n);
        } else expected[256]=0;
    }
    REQUIRE(native_event_call(bodies[0],object,(unsigned)label,(unsigned)group,0x9ABCDE01,3)==(unsigned)object);
    REQUIRE(!memcmp(expected,object,320));
    REQUIRE(begins==((flags&1)&&label?1u:0u));
    if(begins) {
        REQUIRE(color_seen==0x9ABCDE01);
        unsigned i=0;
        do {
            REQUIRE(text_seen[i]==(unsigned short)(short)(signed char)object[i]);
        } while(object[i++] && i<256);
    }
    unsigned result=native_event_call(bodies[1],object,0,0,0,0);
    REQUIRE(!event_abi_errors);
    REQUIRE(ends==(flags&2?1u:0u));
    if(flags&2) REQUIRE(result==0x12345678);
    for(unsigned i=0;i<8;i++) REQUIRE(guarded[i]==0xA5 && guarded[328+i]==0xA5);
    REQUIRE(!memcmp(expected,object,320));
    cases++;
}

int main(int argc,char **argv)
{
    REQUIRE(argc==4);
    FILE *file=fopen(argv[1],"rb"); REQUIRE(file);
    REQUIRE(!fseek(file,0,SEEK_END)); long length=ftell(file); rewind(file);
    REQUIRE(length>4096 && length<128*1024*1024);
    unsigned char *raw=malloc(length); REQUIRE(raw);
    REQUIRE(fread(raw,1,length,file)==(size_t)length); fclose(file);
    REQUIRE(((IMAGE_DOS_HEADER*)raw)->e_magic==IMAGE_DOS_SIGNATURE);
    unsigned header=((IMAGE_DOS_HEADER*)raw)->e_lfanew;
    REQUIRE(header<=(unsigned)length-sizeof(IMAGE_NT_HEADERS32));
    IMAGE_NT_HEADERS32 *nt=(void*)(raw+header);
    REQUIRE(nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.Machine==IMAGE_FILE_MACHINE_I386 &&
            nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC);
    IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);
    REQUIRE((unsigned char*)(sections+nt->FileHeader.NumberOfSections)<=raw+length);
    FILE *manifest=fopen(argv[2],"r"); REQUIRE(manifest);
    const unsigned sizes[]={167,5,128,12};
    for(unsigned i=0;i<4;i++) {
        unsigned rva,size,hash,offset=0,matches=0;
        REQUIRE(fscanf(manifest,"%u %u %u",&rva,&size,&hash)==3 && size==sizes[i]);
        for(unsigned j=0;j<nt->FileHeader.NumberOfSections;j++) {
            IMAGE_SECTION_HEADER *s=&sections[j];
            if(rva>=s->VirtualAddress && rva-s->VirtualAddress<=s->SizeOfRawData &&
               size<=s->SizeOfRawData-(rva-s->VirtualAddress) && (s->Characteristics&IMAGE_SCN_MEM_EXECUTE)) {
                REQUIRE(s->PointerToRawData<=(unsigned)length && s->SizeOfRawData<=(unsigned)length-s->PointerToRawData);
                offset=s->PointerToRawData+(rva-s->VirtualAddress); matches++;
            }
        }
        REQUIRE(matches==1 && fnv(raw+offset,size)==hash);
        bodies[i]=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); REQUIRE(bodies[i]);
        memcpy(bodies[i],raw+offset,size);
    }
    { char extra; REQUIRE(fscanf(manifest," %c",&extra)==EOF); }
    fclose(manifest); free(raw);
    HMODULE crt=LoadLibraryA(argv[3]); REQUIRE(crt);
    copy_import=GetProcAddress(crt,"strncpy"); REQUIRE(copy_import);
    unsigned char *ctor=bodies[0],*dtor=bodies[1],*begin=bodies[2],*end=bodies[3];
    *(void**)(ctor+0x13)=&copy_import;
    const unsigned offsets[]={0x34,0x44,0x4D,0x56};
    for(unsigned i=0;i<4;i++) *(const char**)(ctor+offsets[i])=prefix+4*i;
    *(unsigned*)(ctor+0x98)=(unsigned)begin-(unsigned)(ctor+0x9C);
    *(unsigned*)(dtor+1)=(unsigned)end-(unsigned)(dtor+5);
    *(void**)(begin+9)=&begin_hook; *(void**)(end+1)=&end_hook;
    for(unsigned i=0;i<4;i++) {
        DWORD old;
        REQUIRE(VirtualProtect(bodies[i],4096,PAGE_EXECUTE_READ,&old));
        REQUIRE(FlushInstructionCache(GetCurrentProcess(),bodies[i],sizes[i]));
    }
    char long_label[600],long_group[180];
    memset(long_label,'X',599); long_label[599]=0;
    memset(long_group,'G',179); long_group[179]=0;
    const char *labels[]={NULL,"","Rendering mesh\tFXShader\tMODEL",long_label,"high-\x80\xFF"};
    const char *groups[]={NULL,"","MeshFXShader",long_group};
    for(unsigned f=0;f<4;f++) for(unsigned l=0;l<5;l++) for(unsigned g=0;g<4;g++) check(labels[l],groups[g],f);
    printf("Native events: %u callback combinations, labels, truncation, signed widening and buffer checks passed\n",cases);
    for(unsigned i=0;i<4;i++) REQUIRE(VirtualFree(bodies[i],0,MEM_RELEASE));
    FreeLibrary(crt);
    return 0;
}
