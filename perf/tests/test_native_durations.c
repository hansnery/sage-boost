/* Native duration conversion in private fixture memory. No game startup or
 * imports. Fixture numeric scanning/ceil/conversion/overflow reporting are
 * explicit boundaries; no x87 or MXCSR control word is written. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>

#define REQUIRE(c) do { if(!(c)) { fprintf(stderr,"duration check refused line %d: %s\n",__LINE__,#c); exit(1); } } while(0)

static unsigned input_ms,input_real_word,fault,tokens,scans,prepared;
static unsigned ini_marker,throw_type,out[3],rate_words[10];
static const char token[]="fixture",format[]="fixture overflow";
static void *exception_object,*bodies[5];
static float factor,factors[4];
static const float bias=4294967296.0f,rate_constants[3]={0.001f,1000.0f,1.0f};
static jmp_buf jump;
static unsigned long long calls,throws,compensations,refusals;
volatile unsigned duration_abi_errors;

static const char *__attribute__((thiscall)) get_token(void *self,const char *separators)
{ REQUIRE(self==&ini_marker && !separators); tokens++; return token; }
static unsigned __attribute__((thiscall)) scan_uint(void *self,const char *text)
{ REQUIRE(self==&ini_marker && text==token); scans++; return input_ms; }
static float __attribute__((thiscall)) scan_real(void *self,const char *text)
{
    REQUIRE(self==&ini_marker && text==token); scans++;
    float value; memcpy(&value,&input_real_word,4); return value;
}

/* Mathematical ceil for the positive finite doubles this fixture admits.
 * Integer bit operations avoid a CRT ceil implementation changing x87 mode. */
static double __cdecl fixture_ceil(double value)
{
    uint64_t word; memcpy(&word,&value,8);
    REQUIRE(!(word>>63) && ((word>>52)&2047)!=2047);
    int exponent=(int)((word>>52)&2047)-1023;
    if(exponent<0)word=word && fault!=1?UINT64_C(0x3ff0000000000000):0;
    else if(exponent<52) {
        uint64_t mask=(UINT64_C(1)<<(52-exponent))-1;
        if((word&mask) && fault!=1)word+=mask+1;
        word&=~mask;
    }
    memcpy(&value,&word,8); return value;
}
static void *ceil_slot=(void *)fixture_ceil;

void duration_ftol(void);
/* The preceding ceil returns an exact nonnegative integer below 2^31 in
 * these native cases, so FISTP is exact under any rounding mode. It consumes
 * ST0 and returns the int64 in EDX:EAX without touching the control word. */
__asm__(".text\n.globl _duration_ftol\n_duration_ftol:\n"
        "subl $8,%esp\nfistpll (%esp)\npopl %eax\npopl %edx\nret\n");

static void __cdecl prepare_exception(void *object,unsigned count,const char *fmt,unsigned ms,unsigned limit)
{ REQUIRE(count==3 && fmt==format && ms==input_ms && limit==255); exception_object=object; prepared++; }
static void __attribute__((noreturn)) __stdcall throw_exception(void *object,void *type)
{ REQUIRE(object==exception_object && type==&throw_type && prepared==1); longjmp(jump,1); }

void duration_checked(void *,void *,void *);
__asm__(
".text\n.globl _duration_checked\n_duration_checked:\n"
"pushl %ebp\nmovl %esp,%ebp\npushl %ebx\npushl %esi\npushl %edi\nsubl $4,%esp\n"
"movl %esp,-16(%ebp)\nmovl 8(%ebp),%eax\npushl $0\npushl 16(%ebp)\npushl $0\npushl 12(%ebp)\n"
"movl $0x13579bdf,%ebx\nmovl $0x2468ace0,%esi\nmovl $0x76543210,%edi\ncall *%eax\naddl $16,%esp\n"
"cmpl -16(%ebp),%esp\nje 1f\nincl _duration_abi_errors\n1:\n"
"cmpl $0x13579bdf,%ebx\nje 2f\nincl _duration_abi_errors\n2:\n"
"cmpl $0x2468ace0,%esi\nje 3f\nincl _duration_abi_errors\n3:\n"
"cmpl $0x76543210,%edi\nje 4f\nincl _duration_abi_errors\n4:\n"
"leal -12(%ebp),%esp\npopl %edi\npopl %esi\npopl %ebx\npopl %ebp\nret\n");

static unsigned fnv(const unsigned char *p,unsigned n)
{ unsigned h=2166136261u; while(n--)h=(h^*p++)*16777619u; return h; }

static void load(const char *path,const char *manifest)
{
    void *symbols[33]={ (void *)get_token,(void *)scan_uint,(void *)scan_real,&factor,(void *)&bias,
                       &ceil_slot,(void *)duration_ftol,(void *)prepare_exception,(void *)throw_exception,
                       (void *)format,&throw_type };
    for(unsigned i=0;i<3;i++)symbols[20+i]=(void *)(rate_constants+i);
    for(unsigned i=0;i<10;i++)symbols[23+i]=rate_words+i;
    FILE *f=fopen(path,"rb"); REQUIRE(f && !fseek(f,0,SEEK_END));
    long length=ftell(f); rewind(f); REQUIRE(length>4096 && length<128*1024*1024);
    unsigned char *raw=malloc(length); REQUIRE(raw && fread(raw,1,length,f)==(size_t)length); fclose(f);
    REQUIRE(((IMAGE_DOS_HEADER *)raw)->e_magic==IMAGE_DOS_SIGNATURE);
    unsigned pe=((IMAGE_DOS_HEADER *)raw)->e_lfanew;
    REQUIRE(pe<=(unsigned)length-sizeof(IMAGE_NT_HEADERS32));
    IMAGE_NT_HEADERS32 *nt=(void *)(raw+pe);
    REQUIRE(nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.Machine==IMAGE_FILE_MACHINE_I386 &&
            nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC);
    IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);
    REQUIRE((unsigned char *)(sections+nt->FileHeader.NumberOfSections)<=raw+length);
    const unsigned sizes[5]={38,70,71,119,191},counts[5]={3,6,6,10,13};
    f=fopen(manifest,"r"); REQUIRE(f);
    for(unsigned body=0;body<5;body++) {
        unsigned rva,size,hash,count,source=0,matches=0,fields[13];
        REQUIRE(fscanf(f,"%u %u %u %u",&rva,&size,&hash,&count)==4 && size==sizes[body] && count==counts[body]);
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) {
            IMAGE_SECTION_HEADER *s=sections+i;
            if(rva>=s->VirtualAddress && rva-s->VirtualAddress<=s->SizeOfRawData &&
               size<=s->SizeOfRawData-(rva-s->VirtualAddress) && (s->Characteristics&IMAGE_SCN_MEM_EXECUTE)) {
                REQUIRE(s->PointerToRawData<=(unsigned)length && s->SizeOfRawData<=(unsigned)length-s->PointerToRawData);
                source=s->PointerToRawData+rva-s->VirtualAddress; matches++;
            }
        }
        REQUIRE(matches==1 && fnv(raw+source,size)==hash);
        unsigned char *code=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); REQUIRE(code);
        memcpy(code,raw+source,size);
        for(unsigned i=0;i<count;i++) {
            unsigned field,absolute,symbol;
            REQUIRE(fscanf(f,"%u %u %u",&field,&absolute,&symbol)==3 && field<=size-4 && absolute<=1 && symbol<33 && symbols[symbol]);
            for(unsigned j=0;j<i;j++)REQUIRE(field+4<=fields[j] || fields[j]+4<=field);
            fields[i]=field;
            unsigned value=(unsigned)symbols[symbol]-(absolute?0:(unsigned)(code+field+4));
            memcpy(code+field,&value,4);
        }
        DWORD old; REQUIRE(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));
        REQUIRE(FlushInstructionCache(GetCurrentProcess(),code,4096)); bodies[body]=code;
    }
    char extra; REQUIRE(fscanf(f," %c",&extra)==EOF); fclose(f); free(raw);
}

static unsigned parse(unsigned kind,unsigned *thrown)
{
    tokens=scans=prepared=0; exception_object=0;
    out[0]=out[1]=out[2]=0xa5a5a5a5; calls++; *thrown=0;
    if(setjmp(jump)) {
        REQUIRE(kind==3 && out[1]==0xa5a5a5a5); *thrown=1; throws++;
    } else {
        duration_checked(bodies[kind],&ini_marker,out+1);
        REQUIRE(!prepared && !duration_abi_errors);
    }
    REQUIRE(out[0]==0xa5a5a5a5 && out[2]==0xa5a5a5a5 && tokens==1 && scans==1);
    unsigned mask=kind==3?255:kind==2?65535:0xffffffff;
    if(!*thrown)REQUIRE((out[1]&~mask)==(0xa5a5a5a5&~mask));
    return out[1]&mask;
}

static int compensate(unsigned baseline,unsigned capacity,unsigned *value)
{
    if(baseline>capacity/2) {
        if(fault==3)*value=(baseline*2)&capacity;
        return 0;
    }
    unsigned result=baseline*2;
    if(fault==2) {
        float saved=factor; unsigned thrown;
        factor=factors[3]; result=parse(1,&thrown); REQUIRE(!thrown); factor=saved;
    }
    *value=result; return 1;
}

static void policy(unsigned baseline,unsigned *result,unsigned *ok16,unsigned *ok8)
{
    const unsigned caps[3]={0xffffffff,65535,255};
    for(unsigned i=0;i<3;i++) {
        unsigned value=0xdeadbeef;
        int ok=compensate(baseline,caps[i],&value);
        REQUIRE(ok==(baseline<=caps[i]/2));
        if(ok) { REQUIRE(value==baseline*2); compensations++; }
        else { REQUIRE(value==0xdeadbeef); refusals++; }
        if(i==0) { REQUIRE(ok); *result=value; }
        else if(i==1)*ok16=(unsigned)ok;
        else *ok8=(unsigned)ok;
    }
}

static void integers(unsigned mode,unsigned ms,int emit)
{
    input_ms=ms; factor=factors[mode]; unsigned thrown;
    unsigned u32=parse(1,&thrown); REQUIRE(!thrown);
    unsigned u16=parse(2,&thrown); REQUIRE(!thrown && u16==(u32&65535));
    unsigned u8=parse(3,&thrown); REQUIRE(thrown==(u32>255));
    if(!thrown)REQUIRE(u8==u32);
    unsigned doubled=0,ok16=0,ok8=0;
    if(!mode)policy(u32,&doubled,&ok16,&ok8);
    if(emit)printf("INTEGER,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",mode,ms,u32,u16,u8,thrown,doubled,ok16,ok8);
}

int main(int argc,char **argv)
{
    REQUIRE(argc==4); unsigned requested_fault=(unsigned)atoi(argv[3]); REQUIRE(requested_fault<=3);
    const double ceil_inputs[]={0.0,0.25,0.999999,1.0,1.0000000000000002,1.5,4294967295.25};
    const double ceil_expected[]={0.0,1.0,1.0,1.0,2.0,2.0,4294967296.0};
    for(unsigned i=0;i<7;i++)REQUIRE(fixture_ceil(ceil_inputs[i])==ceil_expected[i]);
    load(argv[1],argv[2]);
    factors[0]=0.005f;
    ((void (__cdecl *)(int,int))bodies[4])(5,30); REQUIRE(rate_words[0]==5 && rate_words[1]==30);
    memcpy(factors+1,rate_words+2,4);
    ((void (__cdecl *)(int,int))bodies[4])(10,60); REQUIRE(rate_words[0]==10 && rate_words[1]==60);
    memcpy(factors+2,rate_words+2,4);
    factors[3]=factors[0]*2.0f;
    for(unsigned i=0;i<4;i++) { unsigned word; memcpy(&word,factors+i,4); printf("FACTOR,%u,%08x\n",i,word); }
    fault=requested_fault;
    const unsigned samples[25]={0,1,99,100,199,200,201,249,250,399,400,401,999,1000,1001,
        25400,25401,51000,51001,6553400,6553600,13107000,13107200,2147483648u,4294967295u};
    for(unsigned mode=0;mode<4;mode++) {
        for(unsigned i=0;i<25;i++)integers(mode,samples[i],1);
        for(unsigned ms=0;ms<=60000;ms++)integers(mode,ms,0);
        const float real_samples[8]={0.0f,0.125f,1.0f,199.5f,200.0f,250.25f,1000.0f,16777216.0f};
        for(unsigned i=0;i<8;i++) {
            factor=factors[mode]; memcpy(&input_real_word,real_samples+i,4);
            unsigned thrown,result=parse(0,&thrown); REQUIRE(!thrown);
            printf("REAL,%u,%08x,%08x\n",mode,input_real_word,result);
        }
    }
    for(unsigned i=0;i<5;i++)REQUIRE(VirtualFree(bodies[i],0,MEM_RELEASE));
    printf("COUNTS,%llu,%llu,%llu,%llu\n",calls,throws,compensations,refusals);
    printf("PASS,ABI,%u\n",duration_abi_errors); return 0;
}
