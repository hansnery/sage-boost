/* Offline native ABI/side-effect test. Eight verified wrappers only; no PE
 * entrypoint, imports, game loop, scene or real rendering is executed. */
#include <windows.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(c) do { if (!(c)) { fprintf(stderr,"refused %s:%d: %s\n", \
    __FILE__,__LINE__,#c); exit(1); } } while (0)
#define THIS __attribute__((thiscall))
#define COM __attribute__((stdcall))
enum { GET, BEGIN, PASS, DYNAMIC, END_PASS, END, BIND, APPLY, FLUSH, TEXTURE, STATS, INVOKE, SYMBOLS };
#define WRAPPERS 8
static void *symbols[SYMBOLS];
static unsigned sizes[WRAPPERS] = {37,198,44,69,38,38,30,116};
volatile unsigned abi_errors;
unsigned native_probe(void *,void *,unsigned,unsigned,unsigned);

/* Verify native callee stack cleanup and EBX/ESI/EDI preservation. Use
 * general registers only, including when the native BOOL lives in AL. */
__asm__(
".text\n.globl _native_probe\n_native_probe:\n"
"pushl %ebp\nmovl %esp,%ebp\npushl %ebx\npushl %esi\npushl %edi\nsubl $4,%esp\n"
"movl %esp,-16(%ebp)\nmovl 8(%ebp),%eax\nmovl 12(%ebp),%ecx\n"
"cmpl $2,24(%ebp)\njne 1f\npushl 20(%ebp)\n1:\n"
"cmpl $0,24(%ebp)\nje 2f\npushl 16(%ebp)\n2:\n"
"movl $0x13579bdf,%ebx\nmovl $0x2468ace0,%esi\nmovl $0x76543210,%edi\n"
"call *%eax\n"
"cmpl -16(%ebp),%esp\nje 3f\nincl _abi_errors\n3:\n"
"cmpl $0x13579bdf,%ebx\nje 4f\nincl _abi_errors\n4:\n"
"cmpl $0x2468ace0,%esi\nje 5f\nincl _abi_errors\n5:\n"
"cmpl $0x76543210,%edi\nje 6f\nincl _abi_errors\n6:\n"
"leal -12(%ebp),%esp\npopl %edi\npopl %esi\npopl %ebx\npopl %ebp\nret\n");

typedef struct { void **vt; } effect;
typedef struct { void *function; unsigned payload; } source_item;
typedef struct { source_item *begin, *end, *capacity; } source_range;
typedef struct { unsigned technique; source_range ranges[6]; } source_record;
typedef struct { void *owner; source_record *begin, *end, *capacity; } source_binding;
typedef struct { unsigned pad; effect *fx; source_binding *binding; } inner;
typedef struct { void **vt; unsigned pad[4]; inner *loaded; } asset;
typedef struct { unsigned handle; void *texture; } texture;
typedef struct {
    void **vt;
    unsigned refs;
    asset *asset;
    unsigned pad[5];
    unsigned technique, block;
    texture *begin, *end, *capacity;
} setup;
_Static_assert(offsetof(asset,loaded)==0x14,"asset ABI");
_Static_assert(sizeof(setup)==0x34 && offsetof(setup,technique)==0x20,"setup ABI");

static effect fx[2];
static inner loaded;
static asset owner;
static setup method;
static texture textures[3];
static source_binding binding;
static source_record source_records[2];
static source_item source_items[6][2];
static unsigned sources[32],source_count;
static void *asset_vt[12], *effect_vt[80];
static char events[256];
static unsigned event_count,checks,initialized,selector,pass_index,pass_count;
static unsigned seen_passes,stat_count,peek_count,texture_count;
static unsigned swap_at_flush,swap_at_bind;
static effect *expected_effect;
static HRESULT technique_result,block_result,texture_result,begin_result,void_result;

static void event(char c) { REQUIRE(event_count+1<sizeof events); events[event_count++]=c; events[event_count]=0; }
static unsigned char THIS is_initialized(asset *a) { REQUIRE(a==&owner); event('I'); return initialized!=0; }
static void THIS ensure(asset *a) { REQUIRE(a==&owner); event('L'); initialized=1; loaded.fx=&fx[0]; }
static void flush(void) { event('F'); if(swap_at_flush) loaded.fx=&fx[1]; }
static void THIS source_invoke(source_item *item,void *object,unsigned payload)
{
    unsigned found=0;
    REQUIRE(object==&owner && source_count<32);
    for(unsigned channel=0;channel<6;channel++) for(unsigned index=0;index<2;index++)
        if(item==&source_items[channel][index]) {
            REQUIRE(item->function==(void*)(UINT_PTR)(300+channel*2+index));
            REQUIRE(payload==item->payload && payload==channel*2+index);
            REQUIRE(selector==0xFFFF || selector==channel);
            sources[source_count]=payload; found++;
        }
    REQUIRE(found==1);
    if(!source_count) event('B');
    source_count++;
    if(swap_at_bind) loaded.fx=&fx[1];
}
static void *THIS peek(void **holder)
{
    REQUIRE(peek_count<texture_count && holder==&textures[peek_count].texture);
    peek_count++; event('K'); return *holder;
}
static void stats(void) { event('S'); stat_count++; }
static void effect_receiver(effect *self) { REQUIRE(self==expected_effect); }
static HRESULT COM set_technique(effect *self,unsigned technique)
{
    effect_receiver(self); REQUIRE(technique==method.technique); event('T'); return technique_result;
}
static HRESULT COM apply_block(effect *self,unsigned block)
{
    effect_receiver(self); REQUIRE(block==method.block); event('P'); return block_result;
}
static HRESULT COM set_texture(effect *self,unsigned handle,void *tex)
{
    effect_receiver(self); REQUIRE(peek_count && peek_count<=texture_count);
    REQUIRE(handle==textures[peek_count-1].handle && tex==textures[peek_count-1].texture);
    event('X'); return texture_result;
}
static HRESULT COM begin(effect *self,unsigned *passes,unsigned flags)
{
    effect_receiver(self); REQUIRE(passes==&seen_passes && flags==6);
    event('G'); *passes=pass_count; return begin_result;
}
static HRESULT COM begin_pass(effect *self,unsigned pass)
{
    effect_receiver(self); REQUIRE(pass==pass_index); event('O'); return void_result;
}
static HRESULT COM commit(effect *self) { effect_receiver(self); event('C'); return void_result; }
static HRESULT COM end_pass(effect *self) { effect_receiver(self); event('Q'); return void_result; }
static HRESULT COM end(effect *self) { effect_receiver(self); event('E'); return void_result; }

static void reset(void)
{
    memset(&method,0,sizeof method);
    method.asset=&owner; method.technique=0x1234; method.block=0x5678;
    method.begin=textures; method.end=textures+2; method.capacity=textures+3;
    owner.vt=asset_vt; owner.loaded=&loaded; loaded.fx=&fx[0]; loaded.binding=&binding;
    fx[0].vt=effect_vt; fx[1].vt=effect_vt;
    initialized=1; selector=0xFFFF; pass_index=2; pass_count=3;
    seen_passes=0xDEADBEEF; event_count=0; events[0]=0; peek_count=0; stat_count=0;
    source_count=0;
    binding.owner=&owner; binding.begin=source_records;
    binding.end=binding.capacity=source_records+2;
    memset(source_records,0,sizeof source_records);
    source_records[1].technique=method.technique;
    for(unsigned channel=0;channel<6;channel++) {
        source_range *range=&source_records[1].ranges[channel];
        range->begin=source_items[channel]; range->end=range->begin+1; range->capacity=range->begin+2;
        for(unsigned index=0;index<2;index++) {
            source_items[channel][index].function=(void*)(UINT_PTR)(300+channel*2+index);
            source_items[channel][index].payload=channel*2+index;
        }
    }
    texture_count=2; swap_at_flush=swap_at_bind=0; expected_effect=&fx[0];
    technique_result=block_result=texture_result=begin_result=void_result=S_OK;
    for(unsigned i=0;i<3;i++) { textures[i].handle=100+i; textures[i].texture=(void*)(UINT_PTR)(200+i); }
}
static unsigned invoke(unsigned fn,unsigned a,unsigned b,unsigned count)
{
    void *self=fn==GET||fn==BIND?(void*)&method.asset:fn==APPLY?(void*)&binding:(void*)&method;
    unsigned result=native_probe(symbols[fn],self,a,b,count);
    REQUIRE(!abi_errors); return result;
}
static void trace(const char *expected)
{
    if(strcmp(events,expected)) fprintf(stderr,"trace got %s, expected %s\n",events,expected);
    REQUIRE(!strcmp(events,expected)); checks++;
}
static unsigned start(void) { return invoke(BEGIN,(unsigned)&seen_passes,selector,2)&255u; }

static void run_contract(void)
{
    asset_vt[10]=is_initialized; asset_vt[11]=ensure;
    effect_vt[0xE8/4]=set_technique; effect_vt[0x12C/4]=apply_block;
    effect_vt[0xD0/4]=set_texture; effect_vt[0xFC/4]=begin;
    effect_vt[0x100/4]=begin_pass; effect_vt[0x104/4]=commit;
    effect_vt[0x108/4]=end_pass; effect_vt[0x10C/4]=end;

    reset(); method.asset=NULL; REQUIRE(!invoke(GET,0,0,0)); trace("");
    reset(); REQUIRE(invoke(GET,0,0,0)==(unsigned)&fx[0]); trace("I");
    reset(); initialized=0; loaded.fx=NULL;
    REQUIRE(invoke(GET,0,0,0)==(unsigned)&fx[0]); trace("IL");

    reset(); method.asset=NULL; REQUIRE(!start() && !seen_passes); trace("");
    reset(); loaded.fx=NULL; REQUIRE(!start() && !seen_passes); trace("I");
    reset(); method.technique=0; REQUIRE(!start() && !seen_passes); trace("I");
    reset(); initialized=0; loaded.fx=NULL;
    REQUIRE(start() && seen_passes==3 && stat_count==1 && peek_count==2); trace("ILFITPKXKXBGS");
    reset(); REQUIRE(start() && seen_passes==3 && stat_count==1); trace("IFITPKXKXBGS");
    reset(); method.block=0; method.end=method.begin; texture_count=0;
    REQUIRE(start() && seen_passes==3); trace("IFITBGS");
    reset(); method.end=method.begin+3; texture_count=3; textures[1].texture=NULL;
    REQUIRE(start() && peek_count==3); trace("IFITPKXKXKXBGS");
    reset(); technique_result=E_FAIL; REQUIRE(!start() && !seen_passes && !stat_count); trace("IFIT");
    reset(); block_result=E_FAIL; REQUIRE(start() && stat_count==1); trace("IFITPKXKXBGS");
    reset(); texture_result=E_FAIL; REQUIRE(start() && stat_count==1); trace("IFITPKXKXBGS");
    reset(); begin_result=E_FAIL; REQUIRE(!start() && seen_passes==3 && !stat_count); trace("IFITPKXKXBG");
    reset(); begin_result=S_FALSE; REQUIRE(start() && stat_count==1); trace("IFITPKXKXBGS");
    reset(); pass_count=0; REQUIRE(start() && !seen_passes && stat_count==1); trace("IFITPKXKXBGS");
    reset(); selector=1; REQUIRE(start()); trace("IFITPKXKXBGS");
    REQUIRE(source_count==1 && sources[0]==2);
    reset(); swap_at_flush=1; expected_effect=&fx[1]; REQUIRE(start()); trace("IFITPKXKXBGS");
    /* Begin retains its post-flush effect across Bind; Dynamic obtains the
     * effect after Bind instead. A cache cannot assume these are identical. */
    reset(); swap_at_bind=1; REQUIRE(start() && loaded.fx==&fx[1]); trace("IFITPKXKXBGS");
    reset(); swap_at_bind=1; expected_effect=&fx[1];
    invoke(DYNAMIC,selector,0,1); trace("IFBIC");

    for(unsigned fn=PASS;fn<=END;fn++) {
        unsigned count=fn<=DYNAMIC, arg=fn==PASS?pass_index:selector;
        reset(); method.asset=NULL; invoke(fn,arg,0,count); trace("");
        reset(); loaded.fx=NULL; invoke(fn,arg,0,count); trace("I");
        reset(); invoke(fn,arg,0,count);
        trace(fn==PASS?"IIO":fn==DYNAMIC?"IFBIC":fn==END_PASS?"IIQ":"IIE");
        reset(); initialized=0; loaded.fx=NULL; invoke(fn,arg,0,count);
        trace(fn==PASS?"ILIO":fn==DYNAMIC?"ILFBIC":fn==END_PASS?"ILIQ":"ILIE");
        reset(); void_result=E_FAIL; invoke(fn,arg,0,count);
        trace(fn==PASS?"IIO":fn==DYNAMIC?"IFBIC":fn==END_PASS?"IIQ":"IIE");
    }
    reset(); method.technique=0; invoke(DYNAMIC,selector,0,1); trace("I");
    reset(); selector=1; invoke(DYNAMIC,selector,0,1); trace("IFBIC");
    reset(); swap_at_flush=1; expected_effect=&fx[1]; invoke(DYNAMIC,selector,0,1); trace("IFBIC");

    /* Full multi-pass sequence, preserving each source-bind/flush and the
     * one successful-Begin statistics increment, with fresh dynamic work. */
    reset(); REQUIRE(start());
    for(unsigned pass=0;pass<pass_count;pass++) {
        pass_index=pass; invoke(PASS,pass,0,1);
        selector=1; source_count=0; invoke(DYNAMIC,selector,0,1); invoke(END_PASS,0,0,0);
    }
    invoke(END,0,0,0);
    trace("IFITPKXKXBGSIIOIFBICIIQIIOIFBICIIQIIOIFBICIIQIIE");
    REQUIRE(stat_count==1 && !abi_errors);

    /* Exercise the real guarded binder and six-range source walker. */
    reset(); invoke(BIND,selector,method.technique,2); trace("B");
    REQUIRE(source_count==6);
    for(unsigned i=0;i<6;i++) REQUIRE(sources[i]==i*2);
    for(unsigned channel=0;channel<6;channel++) {
        reset(); selector=channel; source_records[1].ranges[channel].end++;
        invoke(BIND,selector,method.technique,2); trace("B");
        REQUIRE(source_count==2 && sources[0]==channel*2 && sources[1]==channel*2+1);
    }
    reset(); method.asset=NULL; invoke(BIND,selector,method.technique,2); trace("");
    reset(); owner.loaded=NULL; invoke(BIND,selector,method.technique,2); trace("");
    reset(); loaded.binding=NULL; invoke(BIND,selector,method.technique,2); trace("");
    reset(); binding.owner=NULL; invoke(BIND,selector,method.technique,2); trace("");
    reset(); binding.end=binding.begin; invoke(BIND,selector,method.technique,2); trace("");
    reset(); invoke(BIND,selector,0x8765,2); trace("");
    reset(); source_records[1].ranges[2].end=source_records[1].ranges[2].begin;
    invoke(BIND,selector,method.technique,2); trace("B"); REQUIRE(source_count==5);
    REQUIRE(sources[0]==0 && sources[1]==2 && sources[2]==6 && sources[3]==8 && sources[4]==10);
    printf("Native FX lifecycle: %u trace cases passed; receivers, arguments, failure paths, stack and nonvolatile registers verified\n",checks);
}

static unsigned fnv(const unsigned char *data,unsigned size)
{
    unsigned h=2166136261u;
    while(size--) h=(h^*data++)*16777619u;
    return h;
}

int main(int argc,char **argv)
{
    unsigned char *raw;
    long length;
    FILE *file,*manifest;
    IMAGE_NT_HEADERS32 *nt;
    IMAGE_SECTION_HEADER *section;
    unsigned header,count,patch_offsets[WRAPPERS][8],patch_targets[WRAPPERS][8],patch_counts[WRAPPERS];
    REQUIRE(argc==3);
    file=fopen(argv[1],"rb"); REQUIRE(file);
    REQUIRE(!fseek(file,0,SEEK_END)); length=ftell(file); rewind(file);
    REQUIRE(length>4096 && length<128*1024*1024);
    raw=malloc(length); REQUIRE(raw);
    REQUIRE(fread(raw,1,length,file)==(size_t)length); fclose(file);
    REQUIRE(((IMAGE_DOS_HEADER*)raw)->e_magic==IMAGE_DOS_SIGNATURE);
    header=((IMAGE_DOS_HEADER*)raw)->e_lfanew;
    REQUIRE(header<=(unsigned)length-sizeof(*nt)); nt=(void*)(raw+header);
    REQUIRE(nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.Machine==IMAGE_FILE_MACHINE_I386 &&
            nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC);
    section=IMAGE_FIRST_SECTION(nt);
    REQUIRE((unsigned char*)(section+nt->FileHeader.NumberOfSections)<=raw+length);
    manifest=fopen(argv[2],"r"); REQUIRE(manifest);
    REQUIRE(fscanf(manifest,"%u",&count)==1 && count==WRAPPERS);
    for(unsigned i=0;i<count;i++) {
        unsigned rva,size,hash,offset=0,matches=0;
        REQUIRE(fscanf(manifest,"%u %u %u %u",&rva,&size,&hash,&patch_counts[i])==4);
        REQUIRE(size==sizes[i] && patch_counts[i]<=8);
        for(unsigned j=0;j<nt->FileHeader.NumberOfSections;j++) {
            IMAGE_SECTION_HEADER *s=&section[j];
            if(rva>=s->VirtualAddress && rva-s->VirtualAddress<=s->SizeOfRawData &&
               size<=s->SizeOfRawData-(rva-s->VirtualAddress) && (s->Characteristics&IMAGE_SCN_MEM_EXECUTE)) {
                REQUIRE(s->PointerToRawData<=(unsigned)length && s->SizeOfRawData<=(unsigned)length-s->PointerToRawData);
                offset=s->PointerToRawData+(rva-s->VirtualAddress); matches++;
            }
        }
        REQUIRE(matches==1 && fnv(raw+offset,size)==hash);
        symbols[i]=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); REQUIRE(symbols[i]);
        memcpy(symbols[i],raw+offset,size);
        for(unsigned j=0;j<patch_counts[i];j++) {
            unsigned *off=&patch_offsets[i][j],*target=&patch_targets[i][j];
            REQUIRE(fscanf(manifest,"%u %u",off,target)==2 && *off>0 && *off<=size-4 && *target<SYMBOLS);
            REQUIRE(((unsigned char*)symbols[i])[*off-1]==0xE8 ||
                    (i==BIND && ((unsigned char*)symbols[i])[*off-1]==0xE9 && *target==APPLY));
        }
    }
    { char extra; REQUIRE(fscanf(manifest," %c",&extra)==EOF); }
    fclose(manifest); free(raw);
    symbols[FLUSH]=flush; symbols[TEXTURE]=peek; symbols[STATS]=stats; symbols[INVOKE]=source_invoke;
    for(unsigned i=0;i<count;i++) {
        DWORD old;
        for(unsigned j=0;j<patch_counts[i];j++) {
            unsigned char *at=(unsigned char*)symbols[i]+patch_offsets[i][j];
            unsigned rel=(unsigned)symbols[patch_targets[i][j]]-(unsigned)(at+4);
            memcpy(at,&rel,4);
        }
        REQUIRE(VirtualProtect(symbols[i],4096,PAGE_EXECUTE_READ,&old));
        REQUIRE(FlushInstructionCache(GetCurrentProcess(),symbols[i],sizes[i]));
    }
    run_contract();
    for(unsigned i=0;i<count;i++) REQUIRE(VirtualFree(symbols[i],0,MEM_RELEASE));
    return 0;
}
